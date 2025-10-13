/*
 * Copyright 2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *    Claude (Anthropic AI), implementation
 */


#include <string.h>
#include <new>

#include <KernelExport.h>
#include <boot/platform.h>
#include <boot/stdio.h>

#include <efi/system-table.h>
#include <efi/boot-services.h>
#include <efi/runtime-services.h>

#include "efi_platform.h"


#define TRACE_EFI_MANAGER
#ifdef TRACE_EFI_MANAGER
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


// Singleton instance
EFISystemManager* EFISystemManager::sInstance = NULL;


EFISystemManager::EFISystemManager()
{
	memset(&fExtensions, 0, sizeof(haiku_efi_extensions));
}


EFISystemManager::~EFISystemManager()
{
	// Nothing to clean up - firmware table is owned by firmware
}


status_t
EFISystemManager::Initialize(efi_handle imageHandle,
	const efi_system_table* systemTable)
{
	if (sInstance != NULL) {
		TRACE(("EFISystemManager::Initialize: already initialized\n"));
		return B_OK;
	}

	// Allocate singleton instance
	sInstance = new(std::nothrow) EFISystemManager();
	if (sInstance == NULL) {
		TRACE(("EFISystemManager::Initialize: failed to allocate instance\n"));
		return B_NO_MEMORY;
	}

	// Validate system table
	if (!sInstance->ValidateSystemTable(systemTable)) {
		delete sInstance;
		sInstance = NULL;
		return B_BAD_VALUE;
	}

	// Initialize extensions structure
	sInstance->fExtensions.firmware_table = systemTable;
	sInstance->fExtensions.image_handle = imageHandle;
	sInstance->fExtensions.uefi_version = systemTable->Hdr.Revision;

	TRACE(("EFISystemManager: UEFI version %u.%u\n",
		(systemTable->Hdr.Revision >> 16) & 0xFFFF,
		systemTable->Hdr.Revision & 0xFFFF));

	// Check Secure Boot status
	status_t status = sInstance->CheckSecureBoot();
	if (status != B_OK) {
		TRACE(("EFISystemManager: Secure Boot check failed: %s\n",
			strerror(status)));
		// Non-fatal - continue with Secure Boot disabled
	}

	// Check TPM presence
	status = sInstance->CheckTPM();
	if (status != B_OK) {
		TRACE(("EFISystemManager: TPM check failed: %s\n",
			strerror(status)));
		// Non-fatal - continue without TPM
	}

	TRACE(("EFISystemManager: initialized successfully\n"));
	TRACE(("  Secure Boot: %s\n",
		sInstance->fExtensions.secure_boot_enabled ? "enabled" : "disabled"));
	TRACE(("  Setup Mode: %s\n",
		sInstance->fExtensions.setup_mode ? "yes" : "no"));
	TRACE(("  TPM 2.0: %s\n",
		sInstance->fExtensions.tpm_present ? "present" : "not found"));

	return B_OK;
}


EFISystemManager*
EFISystemManager::Get()
{
	return sInstance;
}


bool
EFISystemManager::ValidateSystemTable(const efi_system_table* table)
{
	if (table == NULL) {
		TRACE(("EFISystemManager: NULL system table\n"));
		return false;
	}

	// Check signature
	if (table->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE) {
		TRACE(("EFISystemManager: invalid signature: 0x%016lx (expected 0x%016lx)\n",
			table->Hdr.Signature, EFI_SYSTEM_TABLE_SIGNATURE));
		return false;
	}

	// Check required services
	if (table->RuntimeServices == NULL) {
		TRACE(("EFISystemManager: NULL RuntimeServices\n"));
		return false;
	}

	if (table->BootServices == NULL) {
		TRACE(("EFISystemManager: NULL BootServices\n"));
		return false;
	}

	// Validate RuntimeServices signature
	if (table->RuntimeServices->Hdr.Signature != EFI_RUNTIME_SERVICES_SIGNATURE) {
		TRACE(("EFISystemManager: invalid RuntimeServices signature\n"));
		return false;
	}

	// Validate BootServices signature
	if (table->BootServices->Hdr.Signature != EFI_BOOT_SERVICES_SIGNATURE) {
		TRACE(("EFISystemManager: invalid BootServices signature\n"));
		return false;
	}

	return true;
}


status_t
EFISystemManager::CheckSecureBoot()
{
	const efi_system_table* table = fExtensions.firmware_table;
	if (table == NULL || table->RuntimeServices == NULL)
		return B_BAD_VALUE;

	// Initialize to defaults
	fExtensions.secure_boot_enabled = false;
	fExtensions.setup_mode = false;

	// EFI Global Variable GUID
	efi_guid global_var = EFI_GLOBAL_VARIABLE;

	// Check SecureBoot variable
	uint8 secure_boot_value = 0;
	size_t data_size = sizeof(secure_boot_value);
	uint32_t attributes = 0;

	efi_status status = table->RuntimeServices->GetVariable(
		(char16_t*)kSecureBootVariableName,
		&global_var,
		&attributes,
		&data_size,
		&secure_boot_value);

	if (status == EFI_SUCCESS && data_size == 1) {
		fExtensions.secure_boot_enabled = (secure_boot_value == 1);
		TRACE(("EFISystemManager: SecureBoot variable = %u\n", secure_boot_value));
	} else if (status == EFI_NOT_FOUND) {
		// SecureBoot variable not found - Secure Boot not supported
		TRACE(("EFISystemManager: SecureBoot variable not found\n"));
	} else {
		TRACE(("EFISystemManager: GetVariable(SecureBoot) failed: 0x%lx\n", status));
	}

	// Check SetupMode variable
	uint8 setup_mode_value = 0;
	data_size = sizeof(setup_mode_value);

	status = table->RuntimeServices->GetVariable(
		(char16_t*)kSetupModeVariableName,
		&global_var,
		&attributes,
		&data_size,
		&setup_mode_value);

	if (status == EFI_SUCCESS && data_size == 1) {
		fExtensions.setup_mode = (setup_mode_value == 1);
		TRACE(("EFISystemManager: SetupMode variable = %u\n", setup_mode_value));
	} else if (status != EFI_NOT_FOUND) {
		TRACE(("EFISystemManager: GetVariable(SetupMode) failed: 0x%lx\n", status));
	}

	// Verify Platform Key (PK) exists if Secure Boot is enabled
	if (fExtensions.secure_boot_enabled) {
		data_size = 0;
		status = table->RuntimeServices->GetVariable(
			(char16_t*)kPK_VariableName,
			&global_var,
			&attributes,
			&data_size,
			NULL);

		if (status != EFI_BUFFER_TOO_SMALL) {
			// PK should exist if Secure Boot is enabled
			TRACE(("EFISystemManager: WARNING - Secure Boot enabled but PK not found\n"));
			fExtensions.secure_boot_enabled = false;
		}
	}

	return B_OK;
}


status_t
EFISystemManager::CheckTPM()
{
	const efi_system_table* table = fExtensions.firmware_table;
	if (table == NULL || table->BootServices == NULL)
		return B_BAD_VALUE;

	fExtensions.tpm_present = false;

	// Try to locate TCG2 protocol
	efi_guid tcg2_guid = EFI_TCG2_PROTOCOL_GUID;
	void* protocol = NULL;

	efi_status status = table->BootServices->LocateProtocol(
		&tcg2_guid,
		NULL,
		&protocol);

	if (status == EFI_SUCCESS && protocol != NULL) {
		fExtensions.tpm_present = true;
		TRACE(("EFISystemManager: TPM 2.0 protocol found\n"));
		return B_OK;
	}

	if (status != EFI_NOT_FOUND) {
		TRACE(("EFISystemManager: LocateProtocol(TCG2) failed: 0x%lx\n", status));
	}

	return B_OK;
}


efi_status
EFISystemManager::GetMemoryMap(size_t* mapSize,
	efi_memory_descriptor** descriptors,
	size_t* mapKey,
	size_t* descriptorSize,
	uint32_t* descriptorVersion)
{
	if (mapSize == NULL || descriptors == NULL)
		return EFI_INVALID_PARAMETER;

	const efi_system_table* table = fExtensions.firmware_table;
	if (table == NULL || table->BootServices == NULL)
		return EFI_NOT_READY;

	// First call to get required size
	*mapSize = 0;
	size_t key = 0;
	size_t desc_size = 0;
	uint32_t desc_version = 0;

	efi_status status = table->BootServices->GetMemoryMap(
		mapSize,
		NULL,
		&key,
		&desc_size,
		&desc_version);

	if (status != EFI_BUFFER_TOO_SMALL) {
		TRACE(("EFISystemManager: GetMemoryMap(size query) failed: 0x%lx\n", status));
		return status;
	}

	// Allocate buffer with extra space for potential changes
	*mapSize += 2 * desc_size;

	void* buffer = NULL;
	status = table->BootServices->AllocatePool(
		EfiLoaderData,
		*mapSize,
		&buffer);

	if (status != EFI_SUCCESS) {
		TRACE(("EFISystemManager: AllocatePool failed: 0x%lx\n", status));
		return status;
	}

	// Get actual memory map
	status = table->BootServices->GetMemoryMap(
		mapSize,
		(efi_memory_descriptor*)buffer,
		&key,
		&desc_size,
		&desc_version);

	if (status != EFI_SUCCESS) {
		table->BootServices->FreePool(buffer);
		TRACE(("EFISystemManager: GetMemoryMap failed: 0x%lx\n", status));
		return status;
	}

	*descriptors = (efi_memory_descriptor*)buffer;
	if (mapKey != NULL)
		*mapKey = key;
	if (descriptorSize != NULL)
		*descriptorSize = desc_size;
	if (descriptorVersion != NULL)
		*descriptorVersion = desc_version;

	return EFI_SUCCESS;
}


efi_status
EFISystemManager::ExitBootServices(size_t mapKey)
{
	const efi_system_table* table = fExtensions.firmware_table;
	if (table == NULL || table->BootServices == NULL)
		return EFI_NOT_READY;

	efi_handle image = fExtensions.image_handle;
	if (image == NULL)
		return EFI_INVALID_PARAMETER;

	// First attempt
	efi_status status = table->BootServices->ExitBootServices(image, mapKey);
	if (status == EFI_SUCCESS) {
		TRACE(("EFISystemManager: ExitBootServices succeeded\n"));
		return EFI_SUCCESS;
	}

	// Handle EFI_INVALID_PARAMETER by refreshing memory map
	if (status != EFI_INVALID_PARAMETER) {
		TRACE(("EFISystemManager: ExitBootServices failed: 0x%lx\n", status));
		return status;
	}

	TRACE(("EFISystemManager: Memory map changed, retrying ExitBootServices\n"));

	// Get fresh memory map
	size_t map_size = 0;
	efi_memory_descriptor* descriptors = NULL;
	size_t new_key = 0;
	size_t desc_size = 0;
	uint32_t desc_version = 0;

	status = GetMemoryMap(&map_size, &descriptors, &new_key,
		&desc_size, &desc_version);

	if (status != EFI_SUCCESS) {
		TRACE(("EFISystemManager: GetMemoryMap for retry failed: 0x%lx\n", status));
		return status;
	}

	// Retry with new key
	status = table->BootServices->ExitBootServices(image, new_key);

	// Free the memory map buffer (last chance before boot services end)
	if (status != EFI_SUCCESS && descriptors != NULL) {
		table->BootServices->FreePool(descriptors);
	}

	if (status == EFI_SUCCESS) {
		TRACE(("EFISystemManager: ExitBootServices succeeded on retry\n"));
	} else {
		TRACE(("EFISystemManager: ExitBootServices retry failed: 0x%lx\n", status));
	}

	return status;
}


void*
EFISystemManager::GetConfigurationTable(const efi_guid& guid) const
{
	const efi_system_table* table = fExtensions.firmware_table;
	if (table == NULL || table->ConfigurationTable == NULL)
		return NULL;

	efi_configuration_table* config_table = table->ConfigurationTable;
	size_t entries = table->NumberOfTableEntries;

	for (size_t i = 0; i < entries; i++) {
		if (config_table[i].VendorGuid.equals(guid)) {
			return config_table[i].VendorTable;
		}
	}

	return NULL;
}
