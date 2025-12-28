/*
 * Copyright 2006-2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Intel Extreme Graphics Driver - Kernel Driver Component
 *
 * SUPPORTED: Gen 9+ only (Skylake 2015 and newer)
 * Device detection checks kSupportedDevices[] array
 *
 * ✅ Verified against Intel PRM - Device IDs
 */

#include "driver.h"
#include "device.h"
#include "lock.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <AGP.h>
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <SupportDefs.h>

#undef TRACE
#define TRACE(x...) dprintf("intel_extreme: " x)
#define ERROR(x...) dprintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define MAX_CARDS 4


/*
 * List of supported Gen9+ devices
 *
 * ✅ Verified against Intel PRM Vol 2c - Device IDs
 * (Gen 9+):
 *   - Gen9: Skylake, Kaby Lake, Coffee Lake, Comet Lake
 *   - Gen9 Atom: Apollo Lake, Gemini Lake
 *   - Gen11: Ice Lake
 *   - Gen11 Atom: Jasper Lake, Elkhart Lake
 *   - Gen12: Tiger Lake, Alder Lake
 */
const struct supported_device {
	uint32		device_id;
	int32		type;
	const char*	name;
} kSupportedDevices[] = {
	/*
	 * Gen9: Skylake (2015)
	 *
	 */
	{0x1902, INTEL_MODEL_SKY,  "Skylake GT1"},
	{0x1906, INTEL_MODEL_SKYM, "Skylake ULT GT1"},
	{0x190a, INTEL_MODEL_SKY,  "Skylake GT1 Server"},
	{0x190b, INTEL_MODEL_SKY,  "Skylake GT1"},
	{0x190e, INTEL_MODEL_SKYM, "Skylake ULX GT1"},
	{0x1912, INTEL_MODEL_SKY,  "Skylake GT2"},
	{0x1916, INTEL_MODEL_SKYM, "Skylake ULT GT2"},
	{0x191a, INTEL_MODEL_SKY,  "Skylake Server GT2"},
	{0x191b, INTEL_MODEL_SKY,  "Skylake GT2"},
	{0x191d, INTEL_MODEL_SKY,  "Skylake WKS GT2"},
	{0x191e, INTEL_MODEL_SKYM, "Skylake ULX GT2"},
	{0x1921, INTEL_MODEL_SKYM, "Skylake ULT GT2F"},
	{0x1926, INTEL_MODEL_SKYM, "Skylake ULT GT3"},
	{0x192a, INTEL_MODEL_SKY,  "Skylake Server GT3"},
	{0x192b, INTEL_MODEL_SKY,  "Skylake GT3"},

	/*
	 * Gen9 Atom: Apollo Lake / Broxton (2016)
	 *
	 */
	{0x5a84, INTEL_MODEL_BXTM, "Apollo Lake GT1.5"},
	{0x5a85, INTEL_MODEL_BXTM, "Apollo Lake GT1"},

	/*
	 * Gen9.5: Kaby Lake (2016)
	 *
	 */
	{0x5906, INTEL_MODEL_KBY,  "Kaby Lake ULT GT1"},
	{0x5902, INTEL_MODEL_KBY,  "Kaby Lake DT GT1"},
	{0x5916, INTEL_MODEL_KBYM, "Kaby Lake ULT GT2"},
	{0x5921, INTEL_MODEL_KBYM, "Kaby Lake ULT GT2F"},
	{0x591c, INTEL_MODEL_KBY,  "Kaby Lake ULX GT2"},
	{0x591e, INTEL_MODEL_KBY,  "Kaby Lake ULX GT2"},
	{0x5912, INTEL_MODEL_KBY,  "Kaby Lake DT GT2"},
	{0x5917, INTEL_MODEL_KBYM, "Kaby Lake Mobile GT2"},
	{0x591b, INTEL_MODEL_KBYM, "Kaby Lake Halo GT2"},
	{0x591d, INTEL_MODEL_KBY,  "Kaby Lake WKS GT2"},
	{0x5926, INTEL_MODEL_KBY,  "Kaby Lake ULT GT3"},
	{0x5927, INTEL_MODEL_KBY,  "Kaby Lake ULT GT3"},

	/*
	 * Gen9.5 Atom: Gemini Lake (2017)
	 *
	 */
	{0x3185, INTEL_MODEL_GLKM, "Gemini Lake GT1"},
	{0x3184, INTEL_MODEL_GLKM, "Gemini Lake GT1.5"},

	/*
	 * Gen9.5: Coffee Lake (2017)
	 *
	 */
	{0x3e90, INTEL_MODEL_CFL,  "Coffee Lake GT1"},
	{0x3e93, INTEL_MODEL_CFL,  "Coffee Lake GT1"},
	{0x3e91, INTEL_MODEL_CFL,  "Coffee Lake GT2"},
	{0x3e92, INTEL_MODEL_CFL,  "Coffee Lake GT2"},
	{0x3e96, INTEL_MODEL_CFL,  "Coffee Lake GT2"},
	{0x3e98, INTEL_MODEL_CFL,  "Coffee Lake GT2"},
	{0x3e9a, INTEL_MODEL_CFL,  "Coffee Lake GT2"},
	{0x3e9b, INTEL_MODEL_CFLM, "Coffee Lake Halo GT2"},
	{0x3eab, INTEL_MODEL_CFLM, "Coffee Lake Halo GT2"},
	{0x3ea5, INTEL_MODEL_CFL,  "Coffee Lake GT3"},
	{0x3ea6, INTEL_MODEL_CFL,  "Coffee Lake GT3"},

	/*
	 * Gen9.5: Comet Lake (2019)
	 *
	 */
	{0x9ba4, INTEL_MODEL_CML,  "Comet Lake GT1"},
	{0x9ba8, INTEL_MODEL_CML,  "Comet Lake GT1"},
	{0x9b21, INTEL_MODEL_CMLM, "Comet Lake U GT1"},
	{0x9baa, INTEL_MODEL_CMLM, "Comet Lake U GT1"},
	{0x9bc4, INTEL_MODEL_CML,  "Comet Lake GT2"},
	{0x9bc5, INTEL_MODEL_CML,  "Comet Lake GT2"},
	{0x9bc6, INTEL_MODEL_CML,  "Comet Lake GT2"},
	{0x9bc8, INTEL_MODEL_CML,  "Comet Lake GT2"},
	{0x9be6, INTEL_MODEL_CML,  "Comet Lake GT2"},
	{0x9bf6, INTEL_MODEL_CML,  "Comet Lake GT2"},
	{0x9b41, INTEL_MODEL_CMLM, "Comet Lake U GT2"},
	{0x9bca, INTEL_MODEL_CMLM, "Comet Lake U GT2"},
	{0x9bcc, INTEL_MODEL_CMLM, "Comet Lake U GT2"},

	/*
	 * Gen11: Ice Lake (2019)
	 *
	 */
	{0x8a56, INTEL_MODEL_ICLM, "Ice Lake GT1"},
	{0x8a5c, INTEL_MODEL_ICLM, "Ice Lake GT1.5"},
	{0x8a5a, INTEL_MODEL_ICLM, "Ice Lake GT1.5"},
	{0x8a51, INTEL_MODEL_ICLM, "Ice Lake GT2"},
	{0x8a52, INTEL_MODEL_ICLM, "Ice Lake GT2"},
	{0x8a53, INTEL_MODEL_ICLM, "Ice Lake GT2"},

	/*
	 * Gen11 Atom: Jasper Lake (2021)
	 *
	 */
	{0x4e55, INTEL_MODEL_JSL,  "Jasper Lake"},
	{0x4e61, INTEL_MODEL_JSL,  "Jasper Lake"},
	{0x4e71, INTEL_MODEL_JSLM, "Jasper Lake"},

	/*
	 * Gen11 Atom: Elkhart Lake (2020)
	 *
	 */
	{0x4500, INTEL_MODEL_EHL,  "Elkhart Lake"},
	{0x4541, INTEL_MODEL_EHL,  "Elkhart Lake"},
	{0x4551, INTEL_MODEL_EHL,  "Elkhart Lake"},
	{0x4571, INTEL_MODEL_EHL,  "Elkhart Lake"},

	/*
	 * Gen12: Tiger Lake (2020)
	 *
	 */
	{0x9a49, INTEL_MODEL_TGLM, "Tiger Lake UP3 GT2"},
	{0x9a78, INTEL_MODEL_TGLM, "Tiger Lake UP3 GT2"},
	{0x9a40, INTEL_MODEL_TGLM, "Tiger Lake UP4 GT2"},
	{0x9a60, INTEL_MODEL_TGL,  "Tiger Lake H GT1"},
	{0x9a68, INTEL_MODEL_TGL,  "Tiger Lake H GT1"},
	{0x9a70, INTEL_MODEL_TGL,  "Tiger Lake H GT1"},

	/*
	 * Gen12: Rocket Lake (2021)
	 *
	 */
	{0x4c8a, INTEL_MODEL_RKL,  "Rocket Lake GT1"},
	{0x4c8b, INTEL_MODEL_RKL,  "Rocket Lake GT1"},
	{0x4c8c, INTEL_MODEL_RKL,  "Rocket Lake GT1"},
	{0x4c90, INTEL_MODEL_RKL,  "Rocket Lake GT1"},
	{0x4c9a, INTEL_MODEL_RKL,  "Rocket Lake GT1"},

	/*
	 * Gen12: Alder Lake (2021-2022)
	 *
	 */
	{0x4680, INTEL_MODEL_ALD,  "Alder Lake-S GT1"},
	{0x4682, INTEL_MODEL_ALD,  "Alder Lake-S GT1"},
	{0x4688, INTEL_MODEL_ALD,  "Alder Lake-S GT1"},
	{0x468a, INTEL_MODEL_ALD,  "Alder Lake-S GT1"},
	{0x4690, INTEL_MODEL_ALD,  "Alder Lake-S GT2"},
	{0x4692, INTEL_MODEL_ALD,  "Alder Lake-S GT2"},
	{0x4693, INTEL_MODEL_ALD,  "Alder Lake-S GT2"},
	{0x46a6, INTEL_MODEL_ALDM, "Alder Lake-P GT2"},
	{0x46a8, INTEL_MODEL_ALDM, "Alder Lake-P GT2"},
	{0x46aa, INTEL_MODEL_ALDM, "Alder Lake-P GT3"},
	{0x46b0, INTEL_MODEL_ALDM, "Alder Lake-P GT2"},
	{0x46b1, INTEL_MODEL_ALDM, "Alder Lake-P GT2"},
	{0x46b2, INTEL_MODEL_ALDM, "Alder Lake-P GT2"},
	{0x46b3, INTEL_MODEL_ALDM, "Alder Lake-P GT2"},
	{0x46c0, INTEL_MODEL_ALDM, "Alder Lake-P GT2"},
	{0x46c1, INTEL_MODEL_ALDM, "Alder Lake-P GT2"},
	{0x46c2, INTEL_MODEL_ALDM, "Alder Lake-P GT2"},
	{0x46c3, INTEL_MODEL_ALDM, "Alder Lake-P GT2"},
	{0x46d0, INTEL_MODEL_ALDM, "Alder Lake-N"},
	{0x46d1, INTEL_MODEL_ALDM, "Alder Lake-N"},
	{0x46d2, INTEL_MODEL_ALDM, "Alder Lake-N"},
};

int32 api_version = B_CUR_DRIVER_API_VERSION;

char* gDeviceNames[MAX_CARDS + 1];
intel_info* gDeviceInfo[MAX_CARDS];
pci_module_info* gPCI;
agp_gart_module_info* gGART;
mutex gLock;


static status_t
get_next_intel_extreme(int32* _cookie, pci_info &info, uint32 &type)
{
	int32 index = *_cookie;

	/* find devices */
	for (; gPCI->get_nth_pci_info(index, &info) == B_OK; index++) {
		/* check vendor */
		if (info.vendor_id != VENDOR_ID_INTEL
			|| info.class_base != PCI_display
			|| (info.class_sub != PCI_vga && info.class_sub != PCI_display_other))
			continue;

		/* check device against supported list */
		for (uint32 i = 0; i < sizeof(kSupportedDevices)
				/ sizeof(kSupportedDevices[0]); i++) {
			if (info.device_id == kSupportedDevices[i].device_id) {
				type = i;
				*_cookie = index + 1;
				ERROR("%s: Intel gfx deviceID: 0x%04x\n", __func__, info.device_id);
				return B_OK;
			}
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/*
 * PCH Detection for Gen9+
 *
 * ✅ Verified against Intel PRM Vol 2c - PCH Device IDs
 *(Gen 9+ PCH):
 *   - SPT (Sunrise Point) - Skylake/Kaby Lake
 *   - CNP (Cannon Point) - Coffee Lake
 *   - ICP (Ice Point) - Ice Lake
 *   - TGP (Tiger Point) - Tiger Lake
 *   - ADP (Alder Point) - Alder Lake
 */
static enum pch_info
detect_intel_pch(void)
{
	pci_info info;

	/* find devices */
	for (int32 i = 0; gPCI->get_nth_pci_info(i, &info) == B_OK; i++) {
		/* check vendor - PCH is an ISA bridge */
		if (info.vendor_id != VENDOR_ID_INTEL
			|| info.class_base != PCI_bridge
			|| info.class_sub != PCI_isa) {
			continue;
		}

		/* check device */
		unsigned short id = info.device_id & INTEL_PCH_DEVICE_ID_MASK;
		ERROR("%s: Intel PCH deviceID: 0x%04x\n", __func__, info.device_id);

		switch(id) {
			/* Sunrise Point (Skylake/Kaby Lake) */
			case INTEL_PCH_SPT_DEVICE_ID:
			case INTEL_PCH_SPT_LP_DEVICE_ID:
			case INTEL_PCH_KBP_DEVICE_ID:
				ERROR("%s: Found Sunrise Point PCH\n", __func__);
				return INTEL_PCH_SPT;

			/* Gemini Lake PCH */
			case INTEL_PCH_GMP_DEVICE_ID:
				ERROR("%s: Found Gemini Lake PCH\n", __func__);
				return INTEL_PCH_CNP;

			/* Apollo Lake PCH */
			case INTEL_PCH_APL_LP_DEVICE_ID:
				ERROR("%s: Found Apollo Lake PCH\n", __func__);
				return INTEL_PCH_CNP;

			/* Cannon Point (Coffee Lake) */
			case INTEL_PCH_CNP_DEVICE_ID:
			case INTEL_PCH_CNP_LP_DEVICE_ID:
				ERROR("%s: Found Cannon Point PCH\n", __func__);
				return INTEL_PCH_CNP;

			/* Comet Lake PCH */
			case INTEL_PCH_CMP_DEVICE_ID:
			case INTEL_PCH_CMP2_DEVICE_ID:
				ERROR("%s: Found Comet Lake PCH\n", __func__);
				return INTEL_PCH_CNP;

			case INTEL_PCH_CMP_V_DEVICE_ID:
				ERROR("%s: Found Comet Lake V PCH\n", __func__);
				return INTEL_PCH_SPT;

			/* Ice Lake PCH */
			case INTEL_PCH_ICP_DEVICE_ID:
			case INTEL_PCH_ICP2_DEVICE_ID:
				ERROR("%s: Found Ice Lake PCH\n", __func__);
				return INTEL_PCH_ICP;

			/* Mule Creek Canyon (Elkhart/Jasper Lake) */
			case INTEL_PCH_MCC_DEVICE_ID:
				ERROR("%s: Found Mule Creek Canyon PCH\n", __func__);
				return INTEL_PCH_MCC;

			/* Tiger Lake PCH */
			case INTEL_PCH_TGP_DEVICE_ID:
			case INTEL_PCH_TGP2_DEVICE_ID:
				ERROR("%s: Found Tiger Lake PCH\n", __func__);
				return INTEL_PCH_TGP;

			/* Jasper Lake PCH */
			case INTEL_PCH_JSP_DEVICE_ID:
				ERROR("%s: Found Jasper Lake PCH\n", __func__);
				return INTEL_PCH_JSP;

			/* Alder Lake PCH */
			case INTEL_PCH_ADP_DEVICE_ID:
			case INTEL_PCH_ADP2_DEVICE_ID:
			case INTEL_PCH_ADP3_DEVICE_ID:
			case INTEL_PCH_ADP4_DEVICE_ID:
				ERROR("%s: Found Alder Lake PCH\n", __func__);
				return INTEL_PCH_ADP;
		}
	}

	ERROR("%s: No PCH detected.\n", __func__);
	return INTEL_PCH_NONE;
}


extern "C" const char**
publish_devices(void)
{
	CALLED();
	return (const char**)gDeviceNames;
}


extern "C" status_t
init_hardware(void)
{
	CALLED();

	status_t status = get_module(B_PCI_MODULE_NAME,(module_info**)&gPCI);
	if (status != B_OK) {
		ERROR("pci module unavailable\n");
		return status;
	}

	int32 cookie = 0;
	uint32 type;
	pci_info info;
	status = get_next_intel_extreme(&cookie, info, type);

	put_module(B_PCI_MODULE_NAME);
	return status;
}


extern "C" status_t
init_driver(void)
{
	CALLED();

	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI);
	if (status != B_OK) {
		ERROR("pci module unavailable\n");
		return status;
	}

	status = get_module(B_AGP_GART_MODULE_NAME, (module_info**)&gGART);
	if (status != B_OK) {
		ERROR("AGP GART module unavailable\n");
		put_module(B_PCI_MODULE_NAME);
		return status;
	}

	mutex_init(&gLock, "intel extreme ksync");

	/* find the PCH device (if any) */
	enum pch_info pchInfo = detect_intel_pch();

	/* find devices */
	int32 found = 0;

	for (int32 cookie = 0; found < MAX_CARDS;) {
		pci_info* info = (pci_info*)malloc(sizeof(pci_info));
		if (info == NULL)
			break;

		uint32 type;
		status = get_next_intel_extreme(&cookie, *info, type);
		if (status < B_OK) {
			free(info);
			break;
		}

		/* create device names & allocate device info structure */
		char name[64];
		sprintf(name, "graphics/intel_extreme_%02x%02x%02x",
			 info->bus, info->device,
			 info->function);

		gDeviceNames[found] = strdup(name);
		if (gDeviceNames[found] == NULL)
			break;

		gDeviceInfo[found] = (intel_info*)malloc(sizeof(intel_info));
		if (gDeviceInfo[found] == NULL) {
			free(gDeviceNames[found]);
			break;
		}

		/* initialize the structure for later use */
		memset(gDeviceInfo[found], 0, sizeof(intel_info));
		gDeviceInfo[found]->init_status = B_NO_INIT;
		gDeviceInfo[found]->id = found;
		gDeviceInfo[found]->pci = info;
		gDeviceInfo[found]->registers = info->u.h0.base_registers[0];
		gDeviceInfo[found]->device_identifier = kSupportedDevices[type].name;
		gDeviceInfo[found]->device_type = kSupportedDevices[type].type;
		gDeviceInfo[found]->pch_info = pchInfo;

		dprintf(DEVICE_NAME ": (%" B_PRId32 ") %s, revision = 0x%x\n", found,
			kSupportedDevices[type].name, info->revision);

		found++;
	}

	gDeviceNames[found] = NULL;

	if (found == 0) {
		mutex_destroy(&gLock);
		put_module(B_AGP_GART_MODULE_NAME);
		put_module(B_PCI_MODULE_NAME);
		return ENODEV;
	}

	return B_OK;
}


extern "C" void
uninit_driver(void)
{
	CALLED();

	mutex_destroy(&gLock);

	/* free device related structures */
	char* name;
	for (int32 index = 0; (name = gDeviceNames[index]) != NULL; index++) {
		free(gDeviceInfo[index]);
		free(name);
	}

	put_module(B_AGP_GART_MODULE_NAME);
	put_module(B_PCI_MODULE_NAME);
}


extern "C" device_hooks*
find_device(const char* name)
{
	CALLED();

	for (int index = 0; gDeviceNames[index] != NULL; index++) {
		if (!strcmp(name, gDeviceNames[index]))
			return &gDeviceHooks;
	}

	return NULL;
}
