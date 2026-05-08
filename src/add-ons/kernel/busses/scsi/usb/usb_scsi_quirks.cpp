/*
 * Copyright 2003-2025, Haiku, Inc. All rights reserved.
 * Originally by Siarzhuk Zharski <imker@gmx.li>
 * Distributed under the terms of the MIT License.
 *
 * USB Mass Storage device quirk database.
 *
 * Some USB storage devices have firmware bugs that require specific
 * workarounds. This module provides a static quirk table and
 * driver_settings support for user-specified overrides.
 *
 * Quirk flags are OR'd into usb_mass_device::properties.
 */

#include "usb_scsi.h"

#include <stdlib.h>
#include <string.h>
#include <driver_settings.h>


// ---- built-in quirk table ----

struct device_quirk {
	uint16	vendor_id;
	uint16	product_id;
	uint32	fixes;
};

// Known problematic devices. Sourced from Linux usb-storage unusual_devs.h,
// FreeBSD umass.c quirk table, and original Haiku usb_scsi.devices file.
static const device_quirk sQuirkTable[] = {
	// Genesys Logic USB card readers
	{ 0x05E3, 0x0723, FIX_NO_GETMAXLUN },
	// Sony DSC cameras — don't support TEST_UNIT_READY
	{ 0x054C, 0x0010, FIX_NO_TEST_UNIT | FIX_NO_INQUIRY },
	// Olympus cameras — MODE_SENSE_6 not supported
	{ 0x07B4, 0x0105, FIX_FORCE_MS_TO_10 },
	{ 0x07B4, 0x0109, FIX_FORCE_MS_TO_10 },
	// Datafab CF reader — no PREVENT_ALLOW
	{ 0x07C4, 0xA400, FIX_NO_PREVENT_MEDIA | FIX_NO_GETMAXLUN },
	// SanDisk SDDR-31 — no TEST_UNIT_READY
	{ 0x0781, 0x0002, FIX_NO_TEST_UNIT },
	// Trumpion Comotron — force read-only, no GET_MAX_LUN
	{ 0x090A, 0x1001, FIX_FORCE_READ_ONLY | FIX_NO_GETMAXLUN },
	// Iomega ZIP 100 — use 6-byte R/W and transform TEST_UNIT_READY
	{ 0x059B, 0x0001, FIX_FORCE_RW_TO_6 | FIX_TRANS_TEST_UNIT },
	// Freecom USB-IDE bridge — no GET_MAX_LUN
	{ 0x07AB, 0xFC01, FIX_NO_GETMAXLUN },
	// Sentinel
	{ 0, 0, 0 }
};


// ---- driver_settings overrides ----

// Format in ~/config/settings/kernel/drivers/usb_scsi:
//
//   device {
//       vendor  0x1234
//       product 0x5678
//       fixes   no_getmaxlun force_ms_to_10
//   }

struct quirk_override {
	uint16	vendor_id;
	uint16	product_id;
	uint32	fixes;
};

static quirk_override* sOverrides = NULL;
static int sOverrideCount = 0;


static uint32
parse_fix_name(const char* name)
{
	if (strcmp(name, "no_getmaxlun") == 0)		return FIX_NO_GETMAXLUN;
	if (strcmp(name, "force_rw6") == 0)			return FIX_FORCE_RW_TO_6;
	if (strcmp(name, "no_test_unit") == 0)		return FIX_NO_TEST_UNIT;
	if (strcmp(name, "no_inquiry") == 0)		return FIX_NO_INQUIRY;
	if (strcmp(name, "trans_test_unit") == 0)	return FIX_TRANS_TEST_UNIT;
	if (strcmp(name, "no_prevent_media") == 0)	return FIX_NO_PREVENT_MEDIA;
	if (strcmp(name, "force_ms_to_10") == 0)	return FIX_FORCE_MS_TO_10;
	if (strcmp(name, "force_read_only") == 0)	return FIX_FORCE_READ_ONLY;
	return 0;
}


void
usb_scsi_load_quirks()
{
	void* handle = load_driver_settings(MODULE_NAME);
	if (handle == NULL)
		return;

	// Count device sections
	const driver_settings* settings = get_driver_settings(handle);
	if (settings == NULL) {
		unload_driver_settings(handle);
		return;
	}

	int count = 0;
	for (int i = 0; i < settings->parameter_count; i++) {
		if (strcmp(settings->parameters[i].name, "device") == 0)
			count++;
	}

	if (count > 0) {
		sOverrides = (quirk_override*)calloc(count, sizeof(quirk_override));
		if (sOverrides != NULL) {
			int idx = 0;
			for (int i = 0; i < settings->parameter_count && idx < count;
					i++) {
				driver_parameter* param = &settings->parameters[i];
				if (strcmp(param->name, "device") != 0)
					continue;

				quirk_override* ov = &sOverrides[idx];
				for (int j = 0; j < param->parameter_count; j++) {
					driver_parameter* sub = &param->parameters[j];
					if (strcmp(sub->name, "vendor") == 0
						&& sub->value_count > 0) {
						ov->vendor_id = strtoul(sub->values[0], NULL, 0);
					} else if (strcmp(sub->name, "product") == 0
						&& sub->value_count > 0) {
						ov->product_id = strtoul(sub->values[0], NULL, 0);
					} else if (strcmp(sub->name, "fixes") == 0) {
						for (int k = 0; k < sub->value_count; k++)
							ov->fixes |= parse_fix_name(sub->values[k]);
					}
				}

				if (ov->vendor_id != 0 && ov->product_id != 0)
					idx++;
			}
			sOverrideCount = idx;
		}
	}

	unload_driver_settings(handle);
}


uint32
usb_scsi_lookup_quirks(uint16 vendorId, uint16 productId)
{
	// Check user overrides first (higher priority)
	for (int i = 0; i < sOverrideCount; i++) {
		if (sOverrides[i].vendor_id == vendorId
			&& sOverrides[i].product_id == productId)
			return sOverrides[i].fixes;
	}

	// Check built-in table
	for (int i = 0; sQuirkTable[i].vendor_id != 0; i++) {
		if (sQuirkTable[i].vendor_id == vendorId
			&& sQuirkTable[i].product_id == productId)
			return sQuirkTable[i].fixes;
	}

	return 0;
}
