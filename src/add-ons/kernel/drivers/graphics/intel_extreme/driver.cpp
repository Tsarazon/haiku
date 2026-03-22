/*
 * Copyright 2006-2009, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/*
 * Intel Extreme Graphics Driver - Kernel Driver Component
 *
 * SUPPORTED: Gen 6+ (Sandy Bridge 2011 and newer)
 * Device detection checks kSupportedDevices[] array
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
#include <bus/PCI.h>
#include <SupportDefs.h>

#undef TRACE
#define TRACE(x...) dprintf("intel_extreme: " x)
#define ERROR(x...) dprintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define MAX_CARDS 4


// list of supported devices
const struct supported_device {
	uint32		device_id;
	int32		type;
	const char*	name;
} kSupportedDevices[] = {
	// Gen 6+ only (Sandy Bridge 2011 and newer)
	{0x0102, INTEL_MODEL_SNBG, "SandyBridge Desktop GT1"},
	{0x0112, INTEL_MODEL_SNBG, "SandyBridge Desktop GT2"},
	{0x0122, INTEL_MODEL_SNBG, "SandyBridge Desktop GT2+"},
	{0x0106, INTEL_MODEL_SNBGM, "SandyBridge Mobile GT1"},
	{0x0116, INTEL_MODEL_SNBGM, "SandyBridge Mobile GT2"},
	{0x0126, INTEL_MODEL_SNBGM, "SandyBridge Mobile GT2+"},
	{0x010a, INTEL_MODEL_SNBGS, "SandyBridge Server"},

	{0x0152, INTEL_MODEL_IVBG, "IvyBridge Desktop GT1"},
	{0x0162, INTEL_MODEL_IVBG, "IvyBridge Desktop GT2"},
	{0x0156, INTEL_MODEL_IVBGM, "IvyBridge Mobile GT1"},
	{0x0166, INTEL_MODEL_IVBGM, "IvyBridge Mobile GT2"},
	{0x0152, INTEL_MODEL_IVBGS, "IvyBridge Server"},
	{0x015a, INTEL_MODEL_IVBGS, "IvyBridge Server GT1"},
	{0x016a, INTEL_MODEL_IVBGS, "IvyBridge Server GT2"},

	{0x0a06, INTEL_MODEL_HASM, "Haswell ULT GT1 Mobile"},
	{0x0412, INTEL_MODEL_HAS, "Haswell GT2 Desktop"},
	{0x0416, INTEL_MODEL_HASM, "Haswell GT2 Mobile"},
	{0x0a16, INTEL_MODEL_HASM, "Haswell ULT GT2 Mobile"},
	{0x0a2e, INTEL_MODEL_HASM, "Haswell ULT GT3 Mobile"},
	{0x0d26, INTEL_MODEL_HASM, "Haswell CRW GT3 Mobile"},

	{0x0f30, INTEL_MODEL_VLVM, "ValleyView Mobile"},
	{0x0f31, INTEL_MODEL_VLVM, "ValleyView Mobile"},
	{0x0f32, INTEL_MODEL_VLVM, "ValleyView Mobile"},
	{0x0f33, INTEL_MODEL_VLVM, "ValleyView Mobile"},

	{0x1606, INTEL_MODEL_BDWM, "Broadwell GT1 ULT"},
	{0x160b, INTEL_MODEL_BDWM, "Broadwell GT1 Iris"},
	{0x160e, INTEL_MODEL_BDWM, "Broadwell GT1 ULX"},
	{0x1602, INTEL_MODEL_BDWM, "Broadwell GT1 ULT"},
	{0x160a, INTEL_MODEL_BDWS, "Broadwell GT1 Server"},
	{0x160d, INTEL_MODEL_BDW,  "Broadwell GT1 Workstation"},
	{0x1616, INTEL_MODEL_BDWM, "Broadwell GT2 ULT"},
	{0x161b, INTEL_MODEL_BDWM, "Broadwell GT2 ULT"},
	{0x161e, INTEL_MODEL_BDWM, "Broadwell GT2 ULX"},
	{0x1612, INTEL_MODEL_BDWM, "Broadwell GT2 Halo"},
	{0x161a, INTEL_MODEL_BDWS, "Broadwell GT2 Server"},
	{0x161d, INTEL_MODEL_BDW,  "Broadwell GT2 Workstation"},
	{0x1626, INTEL_MODEL_BDWM, "Broadwell GT3 ULT"},
	{0x162b, INTEL_MODEL_BDWM, "Broadwell GT3 Iris"},
	{0x162e, INTEL_MODEL_BDWM, "Broadwell GT3 ULX"},
	{0x1622, INTEL_MODEL_BDWM, "Broadwell GT3 ULT"},
	{0x162a, INTEL_MODEL_BDWS, "Broadwell GT3 Server"},
	{0x162d, INTEL_MODEL_BDW,  "Broadwell GT3 Workstation"},

	{0x1902, INTEL_MODEL_SKY,  "Skylake GT1"},
	{0x1906, INTEL_MODEL_SKYM, "Skylake GT1"},
	{0x190a, INTEL_MODEL_SKYS, "Skylake GT1"},
	{0x190b, INTEL_MODEL_SKY,  "Skylake GT1"},
	{0x190e, INTEL_MODEL_SKYM, "Skylake GT1"},
	{0x1912, INTEL_MODEL_SKY,  "Skylake GT2"}, //confirmed OK
	{0x1916, INTEL_MODEL_SKYM, "Skylake GT2"}, //confirmed native mode panel OK
	{0x191a, INTEL_MODEL_SKYS, "Skylake GT2"},
	{0x191b, INTEL_MODEL_SKY,  "Skylake GT2"},
	{0x191d, INTEL_MODEL_SKY,  "Skylake GT2"},
	{0x191e, INTEL_MODEL_SKYM, "Skylake GT2"},
	{0x1921, INTEL_MODEL_SKYM, "Skylake GT2F"},
	{0x1926, INTEL_MODEL_SKYM, "Skylake GT3"},
	{0x192a, INTEL_MODEL_SKYS, "Skylake GT3"},
	{0x192b, INTEL_MODEL_SKY,  "Skylake GT3"},

	{0x5a84, INTEL_MODEL_KBYM, "Apollo Lake GT1.5"},
	{0x5a85, INTEL_MODEL_KBYM, "Apollo Lake GT1"},

	{0x5906, INTEL_MODEL_KBY,  "Kabylake ULT GT1"},
	{0x5902, INTEL_MODEL_KBY,  "Kabylake DT GT1"},
	{0x5916, INTEL_MODEL_KBYM, "Kabylake ULT GT2"},
	{0x5921, INTEL_MODEL_KBYM, "Kabylake ULT GT2F"},
	{0x591c, INTEL_MODEL_KBY,  "Kabylake ULX GT2"},
	{0x591e, INTEL_MODEL_KBY,  "Kabylake ULX GT2"},
	{0x5912, INTEL_MODEL_KBY,  "Kabylake DT GT2"},
	{0x5917, INTEL_MODEL_KBYM, "Kabylake Mobile GT2"},
	{0x591b, INTEL_MODEL_KBYM, "Kabylake Halo GT2"},
	{0x591d, INTEL_MODEL_KBY,  "Kabylake WKS GT2"},
	{0x5926, INTEL_MODEL_KBY,  "Kabylake ULT GT3"},
	{0x5927, INTEL_MODEL_KBY,  "Kabylake ULT GT3"},

	{0x3185, INTEL_MODEL_KBYM, "GeminiLake GT1"},	// Same device id for desktop and mobile.
	{0x3184, INTEL_MODEL_KBYM, "GeminiLake GT1.5"},	// Same device id for desktop and mobile.

	{0x3e90, INTEL_MODEL_CFL,  "CoffeeLake GT1"},
	{0x3e93, INTEL_MODEL_CFL,  "CoffeeLake GT1"},
	{0x3e91, INTEL_MODEL_CFL,  "CoffeeLake GT2"},
	{0x3e92, INTEL_MODEL_CFL,  "CoffeeLake GT2"},
	{0x3e96, INTEL_MODEL_CFL,  "CoffeeLake GT2"},
	{0x3e98, INTEL_MODEL_CFL,  "CoffeeLake GT2"},
	{0x3e9a, INTEL_MODEL_CFL,  "CoffeeLake GT2"},
	{0x3e9b, INTEL_MODEL_CFLM, "CoffeeLake Halo GT2"},
	{0x3eab, INTEL_MODEL_CFLM, "CoffeeLake Halo GT2"},
	{0x3ea5, INTEL_MODEL_CFL,  "CoffeeLake GT3"},
	{0x3ea6, INTEL_MODEL_CFL,  "CoffeeLake GT3"},

	{0x8a56, INTEL_MODEL_CFLM, "IceLake GT1"},
	{0x8a5c, INTEL_MODEL_CFLM, "IceLake GT1.5"},
	{0x8a5a, INTEL_MODEL_CFLM, "IceLake GT1.5"},
	{0x8a51, INTEL_MODEL_CFLM, "IceLake GT2"},
	{0x8a52, INTEL_MODEL_CFLM, "IceLake GT2"},
	{0x8a53, INTEL_MODEL_CFLM, "IceLake GT2"},

	{0x9ba4, INTEL_MODEL_CML,	"CometLake GT1"},
	{0x9ba8, INTEL_MODEL_CML,	"CometLake GT1"},
	{0x9b21, INTEL_MODEL_CMLM,	"CometLake U GT1"},
	{0x9baa, INTEL_MODEL_CMLM,	"CometLake U GT1"},
	{0x9bc4, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9bc5, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9bc6, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9bc8, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9be6, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9bf6, INTEL_MODEL_CML,	"CometLake GT2"},
	{0x9b41, INTEL_MODEL_CMLM,	"CometLake U GT2"},
	{0x9bca, INTEL_MODEL_CMLM,	"CometLake U GT2"},
	{0x9bcc, INTEL_MODEL_CMLM,	"CometLake U GT2"},

	{0x4e55, INTEL_MODEL_JSL,	"JasperLake"},
	{0x4e61, INTEL_MODEL_JSL,	"JasperLake"},
	{0x4e71, INTEL_MODEL_JSLM,	"JasperLake"},

	{0x9a49, INTEL_MODEL_TGLM,	"TigerLake"},
	{0x9a78, INTEL_MODEL_TGLM,	"TigerLake"},
	{0x9a40, INTEL_MODEL_TGLM,	"TigerLake"},
	{0x9a60, INTEL_MODEL_TGLM,	"TigerLake"},
	{0x9a68, INTEL_MODEL_TGLM,	"TigerLake"},
	{0x9a70, INTEL_MODEL_TGLM,	"TigerLake"},

	{0x46a6, INTEL_MODEL_ALDM,  "Alder Lake-P GT2"},
	{0x46d1, INTEL_MODEL_ALDM,  "Alder Lake-N"},

};

#include <device_manager.h>

#define INTEL_DRIVER_MODULE_NAME "drivers/graphics/intel_extreme/driver_v1"
#define INTEL_DEVICE_MODULE_NAME "drivers/graphics/intel_extreme/device_v1"

static device_manager_info* sDeviceManager;
static bool sInitialized = false;

char* gDeviceNames[MAX_CARDS + 1];
intel_info* gDeviceInfo[MAX_CARDS];
pci_device_module_info* gPCI;
pci_device* gPCIDev;
agp_gart_module_info* gGART;
mutex gLock;


/* get_next_intel_extreme removed — device_manager handles discovery */


static const struct {
	unsigned short	id;
	enum pch_info	info;
	const char*		name;
} kPCHDevices[] = {
	{ INTEL_PCH_IBX_DEVICE_ID,    INTEL_PCH_IBX, "Ibex Peak" },
	{ INTEL_PCH_CPT_DEVICE_ID,    INTEL_PCH_CPT, "CougarPoint" },
	{ INTEL_PCH_PPT_DEVICE_ID,    INTEL_PCH_CPT, "PantherPoint" },
	{ INTEL_PCH_LPT_DEVICE_ID,    INTEL_PCH_LPT, "LynxPoint" },
	{ INTEL_PCH_LPT_LP_DEVICE_ID, INTEL_PCH_LPT, "LynxPoint LP" },
	{ INTEL_PCH_WPT_DEVICE_ID,    INTEL_PCH_LPT, "WildcatPoint" },
	{ INTEL_PCH_WPT_LP_DEVICE_ID, INTEL_PCH_LPT, "WildcatPoint LP" },
	{ INTEL_PCH_SPT_DEVICE_ID,    INTEL_PCH_SPT, "SunrisePoint" },
	{ INTEL_PCH_SPT_LP_DEVICE_ID, INTEL_PCH_SPT, "SunrisePoint LP" },
	{ INTEL_PCH_KBP_DEVICE_ID,    INTEL_PCH_SPT, "Kaby Lake" },
	{ INTEL_PCH_GMP_DEVICE_ID,    INTEL_PCH_CNP, "Gemini Lake" },
	{ INTEL_PCH_CNP_DEVICE_ID,    INTEL_PCH_CNP, "Cannon Lake" },
	{ INTEL_PCH_CNP_LP_DEVICE_ID, INTEL_PCH_CNP, "Cannon Lake LP" },
	{ INTEL_PCH_APL_LP_DEVICE_ID, INTEL_PCH_CNP, "Apollo Lake" },
	{ INTEL_PCH_CMP_DEVICE_ID,    INTEL_PCH_CNP, "Comet Lake" },
	{ INTEL_PCH_CMP2_DEVICE_ID,   INTEL_PCH_CNP, "Comet Lake 2" },
	{ INTEL_PCH_CMP_V_DEVICE_ID,  INTEL_PCH_SPT, "Comet Lake V" },
	{ INTEL_PCH_ICP_DEVICE_ID,    INTEL_PCH_ICP, "Ice Lake" },
	{ INTEL_PCH_ICP2_DEVICE_ID,   INTEL_PCH_ICP, "Ice Lake 2" },
	{ INTEL_PCH_MCC_DEVICE_ID,    INTEL_PCH_MCC, "Mule Creek Canyon" },
	{ INTEL_PCH_TGP_DEVICE_ID,    INTEL_PCH_TGP, "Tiger Lake" },
	{ INTEL_PCH_TGP2_DEVICE_ID,   INTEL_PCH_TGP, "Tiger Lake 2" },
	{ INTEL_PCH_JSP_DEVICE_ID,    INTEL_PCH_JSP, "Jasper Lake" },
	{ INTEL_PCH_ADP_DEVICE_ID,    INTEL_PCH_ADP, "Alder Lake" },
	{ INTEL_PCH_ADP2_DEVICE_ID,   INTEL_PCH_ADP, "Alder Lake 2" },
	{ INTEL_PCH_ADP3_DEVICE_ID,   INTEL_PCH_ADP, "Alder Lake 3" },
	{ INTEL_PCH_ADP4_DEVICE_ID,   INTEL_PCH_ADP, "Alder Lake 4" },
	{ INTEL_PCH_ADP5_DEVICE_ID,   INTEL_PCH_ADP, "Alder Lake 5" },
};


static enum pch_info
detect_intel_pch()
{
	// PCH detection scans all PCI devices for the ISA bridge.
	// This is a cross-device query requiring the legacy PCI module.
	pci_module_info* pciLegacy;
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&pciLegacy) != B_OK)
		return INTEL_PCH_NONE;

	enum pch_info result = INTEL_PCH_NONE;
	pci_info info;

	for (int32 i = 0; pciLegacy->get_nth_pci_info(i, &info) == B_OK; i++) {
		if (info.vendor_id != VENDOR_ID_INTEL
			|| info.class_base != PCI_bridge
			|| info.class_sub != PCI_isa)
			continue;

		unsigned short id = info.device_id & INTEL_PCH_DEVICE_ID_MASK;
		ERROR("%s: Intel PCH deviceID: 0x%04x\n", __func__, info.device_id);

		for (size_t j = 0; j < B_COUNT_OF(kPCHDevices); j++) {
			if (id == kPCHDevices[j].id) {
				ERROR("%s: Found %s PCH\n", __func__, kPCHDevices[j].name);
				result = kPCHDevices[j].info;
				break;
			}
		}

		if (result != INTEL_PCH_NONE)
			break;
	}

	if (result == INTEL_PCH_NONE)
		ERROR("%s: No PCH detected.\n", __func__);

	put_module(B_PCI_MODULE_NAME);
	return result;
}


static int32 sNumFound = 0;

static status_t
add_intel_device_from_node(device_node *node)
{
	if (sNumFound >= MAX_CARDS)
		return B_NO_MORE_FDS;

	// Get pci_device_module_info from parent node
	device_node *parent = sDeviceManager->get_parent_node(node);
	sDeviceManager->get_driver(parent, (driver_module_info **)&gPCI,
		(void **)&gPCIDev);
	sDeviceManager->put_node(parent);

	if (!sInitialized) {
		status_t status = get_module(B_AGP_GART_MODULE_NAME,
			(module_info**)&gGART);
		if (status != B_OK)
			return status;

		mutex_init(&gLock, "intel extreme ksync");
		sInitialized = true;
	}

	pci_info* info = (pci_info*)malloc(sizeof(pci_info));
	if (info == NULL)
		return B_NO_MEMORY;

	gPCI->get_pci_info(gPCIDev, info);

	// Find device type in supported list
	uint32 type = 0;
	bool found = false;
	for (uint32 i = 0; i < sizeof(kSupportedDevices)
			/ sizeof(kSupportedDevices[0]); i++) {
		if (info->device_id == kSupportedDevices[i].device_id) {
			type = i;
			found = true;
			break;
		}
	}

	if (!found) {
		free(info);
		return B_ERROR;
	}

	char name[64];
	sprintf(name, "graphics/intel_extreme_%02x%02x%02x",
		info->bus, info->device, info->function);

	gDeviceNames[sNumFound] = strdup(name);
	if (gDeviceNames[sNumFound] == NULL) {
		free(info);
		return B_NO_MEMORY;
	}

	gDeviceInfo[sNumFound] = (intel_info*)malloc(sizeof(intel_info));
	if (gDeviceInfo[sNumFound] == NULL) {
		free(gDeviceNames[sNumFound]);
		free(info);
		return B_NO_MEMORY;
	}

	memset((void*)gDeviceInfo[sNumFound], 0, sizeof(intel_info));
	gDeviceInfo[sNumFound]->init_status = B_NO_INIT;
	gDeviceInfo[sNumFound]->id = sNumFound;
	gDeviceInfo[sNumFound]->pci = info;
	gDeviceInfo[sNumFound]->registers = info->u.h0.base_registers[0];
	gDeviceInfo[sNumFound]->device_identifier = kSupportedDevices[type].name;
	gDeviceInfo[sNumFound]->device_type = kSupportedDevices[type].type;
	gDeviceInfo[sNumFound]->pch_info = detect_intel_pch();

	dprintf(DEVICE_NAME ": (%" B_PRId32 ") %s, revision = 0x%x\n", sNumFound,
		kSupportedDevices[type].name, info->revision);

	sNumFound++;
	gDeviceNames[sNumFound] = NULL;
	return B_OK;
}


static float
intel_supports_device(device_node *parent)
{
	const char *bus;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;
	if (strcmp(bus, "pci"))
		return 0.0;

	uint16 vendorId = 0;
	uint8 classBase = 0;
	sDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID, &vendorId, false);
	sDeviceManager->get_attr_uint8(parent, B_DEVICE_TYPE, &classBase, false);

	if (vendorId == VENDOR_ID_INTEL && classBase == PCI_display)
		return 0.6;

	return 0.0;
}


static status_t
intel_register_device(device_node *node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Intel Extreme Graphics"} },
		{ NULL }
	};
	return sDeviceManager->register_node(node, INTEL_DRIVER_MODULE_NAME, attrs, NULL, NULL);
}


static status_t
intel_init_driver(device_node *node, void **cookie)
{
	status_t status = add_intel_device_from_node(node);
	if (status != B_OK)
		return status;
	*cookie = node;
	return B_OK;
}

static void intel_uninit_driver(void *_cookie) {}

static status_t
intel_register_child_devices(void *_cookie)
{
	device_node *node = (device_node *)_cookie;
	for (int i = 0; gDeviceNames[i] != NULL; i++)
		sDeviceManager->publish_device(node, gDeviceNames[i], INTEL_DEVICE_MODULE_NAME);
	return B_OK;
}

static status_t intel_init_device(void *i, void **c) { *c = i; return B_OK; }
static void intel_uninit_device(void *c) {}

static status_t intel_dm_open(void *i, const char *p, int m, void **c)
{ return gDeviceHooks.open(p, m, c); }
static status_t intel_dm_close(void *c) { return gDeviceHooks.close(c); }
static status_t intel_dm_free(void *c) { return gDeviceHooks.free(c); }
static status_t intel_dm_read(void *c, off_t p, void *b, size_t *l)
{ return gDeviceHooks.read(c, p, b, l); }
static status_t intel_dm_write(void *c, off_t p, const void *b, size_t *l)
{ return gDeviceHooks.write(c, p, b, l); }
static status_t intel_dm_control(void *c, uint32 o, void *b, size_t l)
{ return gDeviceHooks.control(c, o, b, l); }


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&sDeviceManager },
	{ NULL }
};

struct device_module_info sIntelDevice = {
	{ INTEL_DEVICE_MODULE_NAME, 0, NULL },
	intel_init_device, intel_uninit_device, NULL,
	intel_dm_open, intel_dm_close, intel_dm_free,
	intel_dm_read, intel_dm_write, NULL, intel_dm_control,
	NULL, NULL
};

struct driver_module_info sIntelDriver = {
	{ INTEL_DRIVER_MODULE_NAME, 0, NULL },
	intel_supports_device, intel_register_device,
	intel_init_driver, intel_uninit_driver,
	intel_register_child_devices, NULL, NULL
};

module_info *modules[] = {
	(module_info *)&sIntelDriver,
	(module_info *)&sIntelDevice,
	NULL
};
