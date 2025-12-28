/*
 * Copyright 2024, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Intel Gen9+ GPU Firmware Loading
 *
 * Authors:
 *		[Your Name]
 *
 * Reference:
 *   - Intel PRM Vol 9: GuC/HuC Firmware Interface
 *   - Linux i915 driver: drivers/gpu/drm/i915/gt/uc/
 *   - Linux i915 driver: drivers/gpu/drm/i915/display/intel_dmc.c
 */


#include "firmware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <OS.h>
#include <KernelExport.h>

/* Kernel syscalls for file operations */
#include <syscalls.h>

#include "driver.h"


#undef TRACE
#define TRACE(x...) dprintf("intel_extreme: " x)
#define ERROR(x...) dprintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


/* Firmware base path */
#define FIRMWARE_PATH "/boot/system/data/firmware/intel_extreme/"


/*
 * Get DMC firmware filename for this GPU
 */
const char*
intel_get_dmc_firmware_name(intel_info* info)
{
	if (info->device_type.InGroup(INTEL_GROUP_SKL))
		return "skl_dmc_ver1_27.bin";
	if (info->device_type.InGroup(INTEL_GROUP_KBL))
		return "kbl_dmc_ver1_04.bin";
	if (info->device_type.InGroup(INTEL_GROUP_BXT))
		return "bxt_dmc_ver1_07.bin";
	if (info->device_type.InGroup(INTEL_GROUP_GLK))
		return "glk_dmc_ver1_04.bin";
	if (info->device_type.InGroup(INTEL_GROUP_ICL))
		return "icl_dmc_ver1_09.bin";
	if (info->device_type.InGroup(INTEL_GROUP_TGL))
		return "tgl_dmc_ver2_12.bin";
	if (info->device_type.InGroup(INTEL_GROUP_RKL))
		return "tgl_dmc_ver2_12.bin";  // RKL uses TGL DMC
	if (info->device_type.InGroup(INTEL_GROUP_ADL))
		return "adlp_dmc_ver2_16.bin";

	return NULL;
}


/*
 * Get GuC firmware filename for this GPU
 */
const char*
intel_get_guc_firmware_name(intel_info* info)
{
	if (info->device_type.InGroup(INTEL_GROUP_SKL))
		return "skl_guc_70.1.1.bin";
	if (info->device_type.InGroup(INTEL_GROUP_KBL))
		return "kbl_guc_70.1.1.bin";
	if (info->device_type.InGroup(INTEL_GROUP_BXT))
		return "bxt_guc_70.1.1.bin";
	if (info->device_type.InGroup(INTEL_GROUP_GLK))
		return "glk_guc_70.1.1.bin";
	if (info->device_type.InGroup(INTEL_GROUP_CFL))
		return "cml_guc_70.1.1.bin";  // CFL uses CML GuC
	if (info->device_type.InGroup(INTEL_GROUP_ICL))
		return "icl_guc_70.1.1.bin";
	if (info->device_type.InGroup(INTEL_GROUP_EHL))
		return "ehl_guc_70.1.1.bin";
	if (info->device_type.InGroup(INTEL_GROUP_TGL))
		return "tgl_guc_70.1.1.bin";
	if (info->device_type.InGroup(INTEL_GROUP_RKL))
		return "tgl_guc_70.1.1.bin";  // RKL uses TGL GuC
	if (info->device_type.InGroup(INTEL_GROUP_ADL))
		return "adlp_guc_70.1.1.bin";
	if (info->device_type.InGroup(INTEL_GROUP_DG1))
		return "dg1_guc_70.1.1.bin";

	return NULL;
}


/*
 * Get HuC firmware filename for this GPU
 */
const char*
intel_get_huc_firmware_name(intel_info* info)
{
	if (info->device_type.InGroup(INTEL_GROUP_SKL))
		return "skl_huc_2.0.0.bin";
	if (info->device_type.InGroup(INTEL_GROUP_KBL))
		return "kbl_huc_4.0.0.bin";
	if (info->device_type.InGroup(INTEL_GROUP_BXT))
		return "bxt_huc_2.0.0.bin";
	if (info->device_type.InGroup(INTEL_GROUP_GLK))
		return "glk_huc_4.0.0.bin";
	if (info->device_type.InGroup(INTEL_GROUP_CFL))
		return "cml_huc_4.0.0.bin";
	if (info->device_type.InGroup(INTEL_GROUP_ICL))
		return "icl_huc_9.0.0.bin";
	if (info->device_type.InGroup(INTEL_GROUP_EHL))
		return "ehl_huc_9.0.0.bin";
	if (info->device_type.InGroup(INTEL_GROUP_TGL))
		return "tgl_huc_7.9.3.bin";
	if (info->device_type.InGroup(INTEL_GROUP_DG1))
		return "dg1_huc_7.9.3.bin";

	return NULL;
}


/*
 * Load firmware blob from filesystem
 *
 * This runs in kernel context, so we use kernel syscalls directly:
 *   _kern_open()  - open file
 *   _kern_read()  - read data
 *   _kern_read_stat() - get file info
 *   _kern_close() - close file
 */
status_t
intel_load_firmware_blob(const char* name, void** data, size_t* size)
{
	CALLED();

	if (name == NULL || data == NULL || size == NULL)
		return B_BAD_VALUE;

	/* Build full path */
	char path[B_PATH_NAME_LENGTH];
	snprintf(path, sizeof(path), FIRMWARE_PATH "%s", name);

	TRACE("Loading firmware: %s\n", path);

	/* Open file using kernel syscall */
	int fd = _kern_open(-1, path, O_RDONLY, 0);
	if (fd < 0) {
		ERROR("Could not open firmware file: %s\n", path);
		return B_ENTRY_NOT_FOUND;
	}

	/* Get file size using kernel syscall */
	struct stat st;
	status_t status = _kern_read_stat(fd, NULL, false, &st, sizeof(st));
	if (status != B_OK) {
		_kern_close(fd);
		return B_ERROR;
	}

	if (st.st_size == 0 || st.st_size > 4 * 1024 * 1024) {
		ERROR("Invalid firmware size: %ld bytes\n", (long)st.st_size);
		_kern_close(fd);
		return B_BAD_DATA;
	}

	/* Allocate memory */
	*size = st.st_size;
	*data = malloc(*size);
	if (*data == NULL) {
		_kern_close(fd);
		return B_NO_MEMORY;
	}

	/* Read firmware using kernel syscall (position 0 = start of file) */
	ssize_t bytesRead = _kern_read(fd, 0, *data, *size);
	_kern_close(fd);

	if (bytesRead != (ssize_t)*size) {
		ERROR("Failed to read firmware: got %ld, expected %ld\n",
			(long)bytesRead, (long)*size);
		free(*data);
		*data = NULL;
		return B_IO_ERROR;
	}

	TRACE("Loaded firmware %s: %ld bytes\n", name, (long)*size);
	return B_OK;
}


/*
 * Free firmware blob memory
 */
void
intel_free_firmware_blob(void* data)
{
	if (data != NULL)
		free(data);
}


/*
 * Upload DMC firmware to GPU
 *
 * DMC (Display Microcontroller) handles display power states (DC5, DC6)
 *
 * Reference: Linux i915 intel_dmc.c
 */
status_t
intel_load_dmc_firmware(intel_info* info)
{
	CALLED();

	const char* fwName = intel_get_dmc_firmware_name(info);
	if (fwName == NULL) {
		TRACE("No DMC firmware available for this GPU\n");
		return B_NOT_SUPPORTED;
	}

	void* fwData = NULL;
	size_t fwSize = 0;

	status_t status = intel_load_firmware_blob(fwName, &fwData, &fwSize);
	if (status != B_OK) {
		ERROR("Failed to load DMC firmware %s: %s\n", fwName, strerror(status));
		return status;
	}

	/* Validate header */
	if (fwSize < sizeof(intel_dmc_header_v1)) {
		ERROR("DMC firmware too small\n");
		intel_free_firmware_blob(fwData);
		return B_BAD_DATA;
	}

	struct intel_dmc_header_v1* header = (struct intel_dmc_header_v1*)fwData;

	TRACE("DMC firmware version: %d.%d\n",
		header->header_ver, header->dmcc_ver);

	/* Validate firmware offset and size */
	if (header->fw_offset + header->fw_size * 4 > fwSize) {
		ERROR("DMC firmware offset/size invalid\n");
		intel_free_firmware_blob(fwData);
		return B_BAD_DATA;
	}

	/* Get firmware payload */
	uint32* payload = (uint32*)((uint8*)fwData + header->fw_offset);
	uint32 payloadDwords = header->fw_size;

	TRACE("DMC payload: %u dwords at offset 0x%x\n",
		payloadDwords, header->fw_offset);

	/*
	 * Write firmware to DMC MMIO space
	 *
	 * PRM: DMC Program is loaded at DMC_PROGRAM_BASE (0x80000)
	 */
	for (uint32 i = 0; i < payloadDwords; i++) {
		write32(info, DMC_PROGRAM_BASE + i * 4, payload[i]);
	}

	/* Write MMIO pairs from header */
	for (uint32 i = 0; i < header->mmio_count && i < 8; i++) {
		if (header->mmioaddr[i] != 0) {
			TRACE("DMC MMIO: 0x%x = 0x%x\n",
				header->mmioaddr[i], header->mmiodata[i]);
			write32(info, header->mmioaddr[i], header->mmiodata[i]);
		}
	}

	/* Enable DC states */
	uint32 dcStateEn = read32(info, SKL_DMC_DC_STATE_EN);
	dcStateEn |= DC_STATE_DC5_ENABLE | DC_STATE_DC6_ENABLE;
	write32(info, SKL_DMC_DC_STATE_EN, dcStateEn);

	/* Store version in shared info */
	info->shared_info->dmc_version = (header->header_ver << 8) | header->dmcc_ver;

	intel_free_firmware_blob(fwData);

	TRACE("DMC firmware loaded successfully\n");
	return B_OK;
}


/*
 * Upload GuC firmware to GPU
 *
 * GuC (Graphics Microcontroller) handles GPU scheduling and power management.
 * This is OPTIONAL - we can use host-based Execlist submission without it.
 *
 * Reference: Linux i915 intel_guc.c, intel_guc_fw.c
 */
status_t
intel_load_guc_firmware(intel_info* info)
{
	CALLED();

	const char* fwName = intel_get_guc_firmware_name(info);
	if (fwName == NULL) {
		TRACE("No GuC firmware available for this GPU\n");
		return B_NOT_SUPPORTED;
	}

	void* fwData = NULL;
	size_t fwSize = 0;

	status_t status = intel_load_firmware_blob(fwName, &fwData, &fwSize);
	if (status != B_OK) {
		ERROR("Failed to load GuC firmware %s: %s\n", fwName, strerror(status));
		return status;
	}

	/* Validate header */
	if (fwSize < sizeof(intel_uc_fw_header)) {
		ERROR("GuC firmware too small\n");
		intel_free_firmware_blob(fwData);
		return B_BAD_DATA;
	}

	struct intel_uc_fw_header* header = (struct intel_uc_fw_header*)fwData;

	TRACE("GuC firmware version: %d.%d.%d\n",
		header->major_version, header->minor_version, header->patch_version);

	/*
	 * TODO: Implement GuC firmware upload via DMA
	 *
	 * The upload process:
	 * 1. Allocate GGTT space for firmware
	 * 2. Copy firmware to GGTT-mapped memory
	 * 3. Configure WOPCM (Write-Once Protected Content Memory)
	 * 4. Start DMA transfer to GuC
	 * 5. Wait for GuC to boot
	 *
	 * For now, we skip GuC and use Execlist submission directly.
	 * This is fully supported on Gen9+ without GuC.
	 */

	TRACE("GuC firmware upload not yet implemented - using Execlist mode\n");

	/* Store version for reference */
	info->shared_info->guc_version =
		(header->major_version << 16) |
		(header->minor_version << 8) |
		header->patch_version;

	intel_free_firmware_blob(fwData);

	/* Return B_OK even though not loaded - GuC is optional */
	return B_OK;
}


/*
 * Upload HuC firmware to GPU
 *
 * HuC (HEVC Microcontroller) is used for HEVC/H.265 video decoding.
 * It must be loaded AFTER GuC, as GuC handles the HuC authentication.
 *
 * This is OPTIONAL - only needed for hardware video decoding.
 */
status_t
intel_load_huc_firmware(intel_info* info)
{
	CALLED();

	const char* fwName = intel_get_huc_firmware_name(info);
	if (fwName == NULL) {
		TRACE("No HuC firmware available for this GPU\n");
		return B_NOT_SUPPORTED;
	}

	/*
	 * HuC requires GuC to be loaded first for authentication.
	 * Since we're not loading GuC yet, skip HuC too.
	 */
	TRACE("HuC firmware requires GuC - skipping\n");

	return B_NOT_SUPPORTED;
}


/*
 * Initialize all firmware
 *
 * Called from intel_extreme_init() after MMIO is mapped.
 */
status_t
intel_firmware_init(intel_info* info)
{
	CALLED();

	/* Initialize firmware tracking */
	info->shared_info->dmc_version = 0;
	info->shared_info->guc_version = 0;
	info->shared_info->huc_version = 0;

	/*
	 * Load DMC firmware - provides display power states (DC5/DC6)
	 * This is optional but recommended for power savings.
	 */
	status_t dmcStatus = intel_load_dmc_firmware(info);
	if (dmcStatus == B_OK) {
		TRACE("DMC firmware loaded - deep power states enabled\n");
	} else {
		TRACE("DMC not loaded - display power states limited\n");
	}

	/*
	 * GuC/HuC are optional for display driver.
	 * Only load if we need GPU scheduling or video decode.
	 */
#if 0
	/* Disabled - using Execlist submission without GuC */
	intel_load_guc_firmware(info);
	intel_load_huc_firmware(info);
#endif

	return B_OK;
}


/*
 * Cleanup firmware
 */
void
intel_firmware_uninit(intel_info* info)
{
	CALLED();

	/* Disable DC states before shutdown */
	if (info->shared_info->dmc_version != 0) {
		uint32 dcStateEn = read32(info, SKL_DMC_DC_STATE_EN);
		dcStateEn &= ~(DC_STATE_DC5_ENABLE | DC_STATE_DC6_ENABLE);
		write32(info, SKL_DMC_DC_STATE_EN, dcStateEn);
	}
}