// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <SupportDefs.h>

#include <efi/types.h>
#include <efi/boot-services.h>
#include <efi/runtime-services.h>
#include <efi/protocol/simple-text-input.h>
#include <efi/protocol/simple-text-output.h>

#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249
#define EFI_2_60_SYSTEM_TABLE_REVISION ((2<<16) | (60))
#define EFI_2_50_SYSTEM_TABLE_REVISION ((2<<16) | (50))
#define EFI_2_40_SYSTEM_TABLE_REVISION ((2<<16) | (40))
#define EFI_2_31_SYSTEM_TABLE_REVISION ((2<<16) | (31))
#define EFI_2_30_SYSTEM_TABLE_REVISION ((2<<16) | (30))
#define EFI_2_20_SYSTEM_TABLE_REVISION ((2<<16) | (20))
#define EFI_2_10_SYSTEM_TABLE_REVISION ((2<<16) | (10))
#define EFI_2_00_SYSTEM_TABLE_REVISION ((2<<16) | (00))
#define EFI_1_10_SYSTEM_TABLE_REVISION ((1<<16) | (10))
#define EFI_1_02_SYSTEM_TABLE_REVISION ((1<<16) | (02))
#define EFI_SPECIFICATION_VERSION EFI_SYSTEM_TABLE_REVISION
#define EFI_SYSTEM_TABLE_REVISION EFI_2_60_SYSTEM_TABLE_REVISION

typedef struct {
    efi_guid VendorGuid;
    void* VendorTable;
} efi_configuration_table;

typedef struct efi_system_table {
    efi_table_header Hdr;
    char16_t* FirmwareVendor;
    uint32_t FirmwareRevision;
    efi_handle ConsoleInHandle;
    efi_simple_text_input_protocol* ConIn;
    efi_handle ConsoleOutHandle;
    efi_simple_text_output_protocol* ConOut;
    efi_handle StandardErrorHandle;
    efi_simple_text_output_protocol* StdErr;
    efi_runtime_services *RuntimeServices;
    efi_boot_services* BootServices;
    size_t NumberOfTableEntries;
    efi_configuration_table *ConfigurationTable;
} efi_system_table;

// UEFI 2.8+ definitions
#define EFI_2_80_SYSTEM_TABLE_REVISION ((2<<16) | (80))
#define EFI_2_70_SYSTEM_TABLE_REVISION ((2<<16) | (70))

// UEFI Global Variable GUIDs for Secure Boot
#define EFI_IMAGE_SECURITY_DATABASE_GUID \
    {0xd719b2cb, 0x3d3a, 0x4596, {0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f}}

// Secure Boot variable names (UTF-16)
static const char16_t kSecureBootVariableName[] = u"SecureBoot";
static const char16_t kSetupModeVariableName[] = u"SetupMode";
static const char16_t kPK_VariableName[] = u"PK";

// TPM 2.0 Protocol GUID
#define EFI_TCG2_PROTOCOL_GUID \
    {0x607f766c, 0x7455, 0x42be, {0x93, 0x0b, 0xe4, 0xd7, 0x6d, 0xb2, 0x72, 0x0f}}

#ifdef __cplusplus

// Haiku EFI extensions structure - wraps firmware table
typedef struct haiku_efi_extensions {
    const efi_system_table* firmware_table;  // READ-ONLY pointer to firmware table
    uint32_t uefi_version;                   // Decoded UEFI version
    bool secure_boot_enabled;                // Cached Secure Boot state
    bool tpm_present;                        // TPM 2.0 device present
    bool setup_mode;                         // UEFI Setup Mode active
    efi_handle image_handle;                 // Our bootloader image handle
} haiku_efi_extensions;

// EFI System Manager - C++ wrapper for UEFI 2.8+ features
// Private header - internal use only
class EFISystemManager {
public:
    // Initialize the EFI System Manager
    // Must be called early in bootloader initialization
    static status_t Initialize(efi_handle imageHandle,
                               const efi_system_table* systemTable);

    // Get the singleton instance
    static EFISystemManager* Get();

    // Check if Secure Boot is enabled
    bool IsSecureBootEnabled() const { return fExtensions.secure_boot_enabled; }

    // Check if system is in Setup Mode
    bool IsSetupMode() const { return fExtensions.setup_mode; }

    // Check if TPM 2.0 is present
    bool IsTPMPresent() const { return fExtensions.tpm_present; }

    // Get firmware table (read-only)
    const efi_system_table* GetSystemTable() const {
        return fExtensions.firmware_table;
    }

    // Get UEFI version (major << 16 | minor)
    uint32_t GetUEFIVersion() const { return fExtensions.uefi_version; }

    // Get current memory map
    // Caller must free returned descriptor array with FreePool
    efi_status GetMemoryMap(size_t* mapSize,
                           efi_memory_descriptor** descriptors,
                           size_t* mapKey,
                           size_t* descriptorSize,
                           uint32_t* descriptorVersion);

    // Exit Boot Services with proper error recovery
    // Handles EFI_INVALID_PARAMETER by refreshing memory map
    efi_status ExitBootServices(size_t mapKey);

    // Get configuration table by GUID
    void* GetConfigurationTable(const efi_guid& guid) const;

private:
    EFISystemManager();
    ~EFISystemManager();

    // Non-copyable
    EFISystemManager(const EFISystemManager&);
    EFISystemManager& operator=(const EFISystemManager&);

    // Check Secure Boot status via RuntimeServices->GetVariable
    status_t CheckSecureBoot();

    // Check for TPM 2.0 presence
    status_t CheckTPM();

    // Validate firmware table signature
    bool ValidateSystemTable(const efi_system_table* table);

    // Haiku extensions wrapper
    haiku_efi_extensions fExtensions;

    // Singleton instance
    static EFISystemManager* sInstance;
};

#endif // __cplusplus
