/*
 * Copyright 2024, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Intel Gen9+ GPU Firmware Loading
 *
 * Supports loading of:
 *   - DMC (Display Microcontroller) - display power states
 *   - GuC (Graphics Microcontroller) - GPU scheduling (optional)
 *   - HuC (HEVC Microcontroller) - video decoding (optional)
 *
 * Firmware files are located in:
 *   /system/data/firmware/intel_extreme/
 *
 * Reference: Intel PRM Vol 9 - GuC, HuC, DMC Firmware
 */
#ifndef INTEL_FIRMWARE_H
#define INTEL_FIRMWARE_H


#include "intel_extreme.h"


/*
 * Firmware types
 */
enum firmware_type {
	INTEL_FW_DMC,		// Display Microcontroller
	INTEL_FW_GUC,		// Graphics Microcontroller
	INTEL_FW_HUC		// HEVC Microcontroller
};


/*
 * Firmware blob header (common for GuC/HuC)
 *
 * See drivers/gpu/drm/i915/gt/uc/intel_uc_fw_abi.h
 */
struct intel_uc_fw_header {
	uint32	header_size_dw;
	uint32	header_version;
	uint32	type;
	uint32	size_dw;
	uint32	major_version;
	uint32	minor_version;
	uint32	patch_version;
	uint32	sw_reserved[3];
	uint32	reserved[14];
} _PACKED;


/*
 * DMC firmware header
 *
 * See drivers/gpu/drm/i915/display/intel_dmc.c
 */
struct intel_dmc_header_v1 {
	uint32	header_len;
	uint8	header_ver;
	uint8	dmcc_ver;
	uint16	project;
	uint32	fw_offset;
	uint32	fw_size;
	uint32	reserved1[2];
	uint32	mmio_count;
	uint32	reserved2[7];
	uint32	mmioaddr[8];
	uint32	mmiodata[8];
} _PACKED;


/*
 * Firmware state tracking
 */
struct intel_firmware_info {
	bool		dmc_loaded;
	bool		guc_loaded;
	bool		huc_loaded;
	uint32		dmc_version;
	uint32		guc_version;
	uint32		huc_version;
};


/*
 * DMC firmware registers
 *
 * PRM: Display Engine Registers - DMC
 */
#define DMC_PROGRAM_BASE		0x80000

/* Gen9 Skylake */
#define SKL_DMC_DC_STATE_EN		0x45504
#define SKL_DMC_SSP_BASE		0x8F074
#define SKL_DMC_HTP_SKL			0x8F004

/* Gen11 Ice Lake */
#define ICL_DMC_DC_STATE_EN		0x45504

/* Gen12 Tiger Lake */
#define TGL_DMC_DC_STATE_EN		0x45504
#define TGL_DMC_DEBUG_DC5		0x101090
#define TGL_DMC_DEBUG_DC6		0x101094

/* DC state bits */
#define DC_STATE_EN				(1 << 0)
#define DC_STATE_DC5_ENABLE		(1 << 0)
#define DC_STATE_DC6_ENABLE		(1 << 1)
#define DC_STATE_DC9_ENABLE		(1 << 3)


/*
 * GuC firmware registers
 *
 * PRM: GT Registers - GuC
 */
#define GUC_STATUS				0xC000
#define   GUC_STATUS_BOOTROM_MASK		(0x7 << 1)
#define   GUC_STATUS_BOOTROM_COMPLETED	(0x1 << 1)
#define   GUC_STATUS_UKERNEL_MASK		(0xf << 4)
#define   GUC_STATUS_UKERNEL_READY		(0x1 << 4)

#define GUC_WOPCM_SIZE			0xC050
#define GUC_WOPCM_OFFSET		0xC340
#define GUC_SHIM_CONTROL		0xC064

#define SOFT_SCRATCH(n)			(0xC180 + (n) * 4)
#define SOFT_SCRATCH_COUNT		16

/* GuC WOPCM (Write-Once Protected Content Memory) */
#define GUC_WOPCM_TOP			(512 * 1024)
#define GUC_WOPCM_OFFSET_VALUE	0x80000

/* DMA registers for firmware upload */
#define DMA_ADDR_0_LOW			0xC300
#define DMA_ADDR_0_HIGH			0xC304
#define DMA_ADDR_1_LOW			0xC308
#define DMA_ADDR_1_HIGH			0xC30C
#define DMA_CTRL				0xC310
#define   DMA_CTRL_SRC_IS_GGTT	(1 << 0)
#define   DMA_CTRL_DST_IS_WOPCM	(1 << 1)
#define   DMA_CTRL_START		(1 << 31)

#define DMA_GUC_WOPCM_OFFSET	0xC340
#define DMA_STATUS				0xC344


/*
 * Function prototypes
 */

/* Main firmware loading functions */
status_t intel_firmware_init(intel_info* info);
void intel_firmware_uninit(intel_info* info);

/* Individual firmware loaders */
status_t intel_load_dmc_firmware(intel_info* info);
status_t intel_load_guc_firmware(intel_info* info);
status_t intel_load_huc_firmware(intel_info* info);

/* Helper to get firmware name for device */
const char* intel_get_dmc_firmware_name(intel_info* info);
const char* intel_get_guc_firmware_name(intel_info* info);
const char* intel_get_huc_firmware_name(intel_info* info);

/* Low-level file loading */
status_t intel_load_firmware_blob(const char* name, void** data, size_t* size);
void intel_free_firmware_blob(void* data);


#endif /* INTEL_FIRMWARE_H */