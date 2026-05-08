/*
 * Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2016, Jessica Hamilton, jessica.l.hamilton@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include <device_keeper.h>
#include <OS.h>
#include <KernelExport.h>
#include <SupportDefs.h>
#include <bus/PCI.h>
#include <frame_buffer_console.h>
#include <boot_item.h>
#include <vesa_info.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "driver.h"
#include "device.h"


#define TRACE_DRIVER
#ifdef TRACE_DRIVER
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define MAX_CARDS 1

#define VESA_DRIVER_MODULE_NAME "drivers/graphics/vesa/dk_driver_v1"


char* gDeviceNames[MAX_CARDS + 1];
vesa_info* gDeviceInfo[MAX_CARDS];
isa_module_info* gISA;
pci_device_ops* gPCI;
pci_device* gPCIDev;
mutex gLock;

static dk_keeper_info* sDeviceKeeper;
static bool sPublished = false;


//	#pragma mark - dk_driver_info


static const dk_match_rule sVesaMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "pci"),
	DK_MATCH_END
};

static const dk_match_dict sVesaMatchDict = {
	sVesaMatchRules,
	0
};


static float
vesa_probe(dk_node *node)
{
	TRACE((DEVICE_NAME ": probe()\n"));

	// need VESA mode info AND frame buffer boot info
	if (get_boot_item(VESA_MODES_BOOT_INFO, NULL) == NULL
		|| get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL) == NULL)
		return 0.0;

	if (sPublished)
		return 0.0;

	// match PCI display devices with low priority
	uint8 classBase = 0;
	sDeviceKeeper->get_property_uint8(node, KOSM_DEVICE_TYPE,
		&classBase, false);
	if (classBase == PCI_display) {
		TRACE((DEVICE_NAME ": VESA device found!\n"));
		return 0.1;
	}

	return 0.0;
}


static status_t
vesa_attach(dk_node *node, void **cookie)
{
	TRACE((DEVICE_NAME ": attach()\n"));

	// Get PCI ops from parent bus manager
	status_t status = sDeviceKeeper->get_interface(node,
		PCI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void **)&gPCI, (void **)&gPCIDev);
	if (status != B_OK) {
		TRACE((DEVICE_NAME ": get_interface(PCI) failed: %s\n",
			strerror(status)));
		return status;
	}

	gDeviceInfo[0] = (vesa_info*)malloc(sizeof(vesa_info));
	if (gDeviceInfo[0] == NULL)
		return B_NO_MEMORY;

	memset(gDeviceInfo[0], 0, sizeof(vesa_info));

	// ISA may not be available on all architectures
	status = get_module(B_ISA_MODULE_NAME, (module_info**)&gISA);
	if (status != B_OK) {
		TRACE((DEVICE_NAME ": ISA bus unavailable\n"));
		gISA = NULL;
	}

	gDeviceNames[0] = strdup("graphics/vesa");
	if (gDeviceNames[0] == NULL) {
		if (gISA != NULL)
			put_module(B_ISA_MODULE_NAME);
		free(gDeviceInfo[0]);
		return B_NO_MEMORY;
	}

	gDeviceNames[1] = NULL;
	mutex_init(&gLock, "vesa lock");

	// Publish device — ops defined in device.cpp
	status = sDeviceKeeper->publish_device(node, gDeviceNames[0], &gDeviceOps);
	if (status != B_OK) {
		TRACE((DEVICE_NAME ": publish_device failed: %s\n",
			strerror(status)));
		mutex_destroy(&gLock);
		free(gDeviceNames[0]);
		gDeviceNames[0] = NULL;
		if (gISA != NULL)
			put_module(B_ISA_MODULE_NAME);
		free(gDeviceInfo[0]);
		gDeviceInfo[0] = NULL;
		return status;
	}

	sPublished = true;
	*cookie = node;
	return B_OK;
}


static void
vesa_detach(void *_cookie)
{
	TRACE((DEVICE_NAME ": detach()\n"));

	dk_node* node = (dk_node*)_cookie;

	char* name;
	for (int32 index = 0; (name = gDeviceNames[index]) != NULL; index++) {
		sDeviceKeeper->unpublish_device(node, name);
		free(gDeviceInfo[index]);
		free(name);
		gDeviceNames[index] = NULL;
		gDeviceInfo[index] = NULL;
	}

	if (gISA != NULL)
		put_module(B_ISA_MODULE_NAME);
	mutex_destroy(&gLock);

	sPublished = false;
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&sDeviceKeeper },
	{ NULL }
};

struct dk_driver_info sVesaDriver = {
	.info   = { VESA_DRIVER_MODULE_NAME, 0, NULL },
	.match  = &sVesaMatchDict,
	.probe  = vesa_probe,
	.attach = vesa_attach,
	.detach = vesa_detach,
	.ops    = &gDeviceOps,
};

module_info *modules[] = {
	(module_info *)&sVesaDriver,
	NULL
};
