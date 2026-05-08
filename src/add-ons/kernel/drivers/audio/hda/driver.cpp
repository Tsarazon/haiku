/*
 * Copyright 2007-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ithamar Adema, ithamar AT unet DOT nl
 */


#include "driver.h"

#include <device_keeper.h>
#include <stdlib.h>
#include <string.h>


#define HDA_DRIVER_MODULE_NAME "drivers/audio/hda/dk_driver_v1"


hda_controller gCards[MAX_CARDS];
uint32 gNumCards;
static dk_keeper_info* sDeviceKeeper;
extern dk_device_ops gHdaDeviceOps;


static const struct {
	uint16	vendor;
	uint16	device;
} kSupportedDevices[] = {
	{ 0x8086, 0xa170},	// 100 Series HD Audio
	{ 0x8086, 0x9d71},	// 200 Series HD Audio
	{ 0x8086, 0xa348},	// 300 Series cAVS
	{ 0x8086, 0x9dc8},	// 300 Series HD Audio
	{ 0x8086, 0x06c8},	// 400 Series cAVS
	{ 0x8086, 0x02c8},	// 400 Series HD Audio
	{ 0x8086, 0xa0c8},	// 500 Series HD Audio
	{ 0x8086, 0x51c8},	// 600 Series HD Audio
	{ 0x8086, 0x4dc8},	// JasperLake HD Audio
	{ 0x8086, 0x43c8},	// Tiger Lake-H HD Audio
	{ 0x8086, 0xa171},	// CM238 HD Audio
	{ 0x8086, 0x3198},	// GeminiLake HD Audio
};


static bool
is_supported_device(uint16 vendorId, uint16 deviceId)
{
	for (size_t i = 0; i < B_COUNT_OF(kSupportedDevices); i++) {
		if (vendorId == kSupportedDevices[i].vendor
			&& deviceId == kSupportedDevices[i].device) {
			return true;
		}
	}
	return false;
}


//	#pragma mark - card slot management


/*! Find a free slot in gCards[] (devfs_path == NULL), or allocate a new
	slot at gNumCards. Returns the slot index, or -1 if full.
*/
static int32
allocate_card_slot()
{
	for (uint32 i = 0; i < gNumCards; i++) {
		if (gCards[i].devfs_path == NULL)
			return (int32)i;
	}

	if (gNumCards >= MAX_CARDS)
		return -1;

	return (int32)gNumCards++;
}


//	#pragma mark - dk_driver_info


static const dk_match_rule sHdaMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "pci"),
	DK_MATCH_END
};

static const dk_match_dict sHdaMatchDict = {
	sHdaMatchRules,
	0	// priority
};


static float
hda_probe(dk_node* node)
{
	// match PCI HD Audio class (multimedia / hd_audio subclass)
	uint8 classBase = 0, classSub = 0;
	sDeviceKeeper->get_property_uint8(node, KOSM_DEVICE_TYPE,
		&classBase, false);
	sDeviceKeeper->get_property_uint8(node, KOSM_DEVICE_SUB_TYPE,
		&classSub, false);

	if (classBase == PCI_multimedia && classSub == PCI_hd_audio)
		return 0.6f;

	// also match specific vendor/device IDs from the allowlist
	uint16 vendorId = 0, deviceId = 0;
	sDeviceKeeper->get_property_uint16(node, KOSM_DEVICE_VENDOR_ID,
		&vendorId, false);
	sDeviceKeeper->get_property_uint16(node, KOSM_DEVICE_ID,
		&deviceId, false);

	if (is_supported_device(vendorId, deviceId))
		return 0.6f;

	return 0.0f;
}


static status_t
hda_attach(dk_node* node, void** _cookie)
{
	// Get PCI bus ops from a matching ancestor.
	pci_device_ops* pciOps = NULL;
	pci_device* pciDev = NULL;
	status_t status = sDeviceKeeper->get_interface(node,
		PCI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&pciOps, (void**)&pciDev);
	if (status != B_OK) {
		dprintf("hda: get_interface(PCI) failed: %s\n", strerror(status));
		return status;
	}
	if (pciOps == NULL || pciDev == NULL)
		return B_ERROR;

	int32 slot = allocate_card_slot();
	if (slot < 0)
		return B_NO_MORE_FDS;

	hda_controller* controller = &gCards[slot];
	memset(controller, 0, sizeof(hda_controller));
	controller->pci_ops = pciOps;
	controller->pci_dev = pciDev;
	controller->node = node;
	controller->opened = 0;

	pciOps->get_pci_info(pciDev, &controller->pci_info);

	char path[B_PATH_NAME_LENGTH];
	snprintf(path, sizeof(path), DEVFS_PATH_FORMAT, (uint32)slot);
	controller->devfs_path = strdup(path);
	if (controller->devfs_path == NULL)
		return B_NO_MEMORY;

	dprintf("HDA: Detected controller @ PCI:%d:%d:%d, IRQ:%d, "
		"type %04x/%04x (%04x/%04x)\n",
		controller->pci_info.bus, controller->pci_info.device,
		controller->pci_info.function,
		controller->pci_info.u.h0.interrupt_line,
		controller->pci_info.vendor_id, controller->pci_info.device_id,
		controller->pci_info.u.h0.subsystem_vendor_id,
		controller->pci_info.u.h0.subsystem_id);

	// Publish only THIS card. DeviceKeeper invokes attach() once per
	// matched node, so each controller publishes exactly one devfs entry.
	status = sDeviceKeeper->publish_device(node, controller->devfs_path,
		&gHdaDeviceOps);
	if (status != B_OK) {
		dprintf("hda: publish_device(%s) failed: %s\n",
			controller->devfs_path, strerror(status));
		free((void*)controller->devfs_path);
		controller->devfs_path = NULL;
		return status;
	}

	*_cookie = controller;
	return B_OK;
}


static void
hda_detach(void* cookie)
{
	hda_controller* controller = (hda_controller*)cookie;
	if (controller == NULL || controller->devfs_path == NULL)
		return;

	sDeviceKeeper->unpublish_device(controller->node, controller->devfs_path);

	free((void*)controller->devfs_path);
	controller->devfs_path = NULL;
	controller->pci_ops = NULL;
	controller->pci_dev = NULL;
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&sDeviceKeeper },
	{ NULL }
};

struct dk_driver_info sHdaDriver = {
	.info   = { HDA_DRIVER_MODULE_NAME, 0, NULL },
	.match  = &sHdaMatchDict,
	.probe  = hda_probe,
	.attach = hda_attach,
	.detach = hda_detach,
	.ops    = &gHdaDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sHdaDriver,
	NULL
};
