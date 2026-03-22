/*
 * Copyright 2007-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ithamar Adema, ithamar AT unet DOT nl
 */


#include "driver.h"

#include <device_manager.h>
#include <stdlib.h>
#include <string.h>


#define HDA_DRIVER_MODULE_NAME "drivers/audio/hda/driver_v1"
#define HDA_DEVICE_MODULE_NAME "drivers/audio/hda/device_v1"


hda_controller gCards[MAX_CARDS];
uint32 gNumCards;
pci_device_module_info* gPci;
pci_device* gPciDev;
static device_manager_info* sDeviceManager;


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


static status_t
add_card_from_node(device_node *node)
{
	if (gNumCards >= MAX_CARDS)
		return B_NO_MORE_FDS;

	// Get pci_device_module_info from parent node
	device_node *parent = sDeviceManager->get_parent_node(node);
	sDeviceManager->get_driver(parent, (driver_module_info **)&gPci,
		(void **)&gPciDev);
	sDeviceManager->put_node(parent);

	pci_info info;
	gPci->get_pci_info(gPciDev, &info);

	memset(&gCards[gNumCards], 0, sizeof(hda_controller));
	gCards[gNumCards].pci_info = info;
	gCards[gNumCards].opened = 0;

	char path[B_PATH_NAME_LENGTH];
	sprintf(path, DEVFS_PATH_FORMAT, gNumCards);
	gCards[gNumCards].devfs_path = strdup(path);

	dprintf("HDA: Detected controller @ PCI:%d:%d:%d, IRQ:%d, "
		"type %04x/%04x (%04x/%04x)\n",
		info.bus, info.device, info.function,
		info.u.h0.interrupt_line, info.vendor_id, info.device_id,
		info.u.h0.subsystem_vendor_id, info.u.h0.subsystem_id);

	gNumCards++;
	return B_OK;
}


//	#pragma mark - driver module API


static float
hda_supports_device(device_node *parent)
{
	const char *bus;

	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;

	if (strcmp(bus, "pci"))
		return 0.0;

	uint8 classBase = 0, classSub = 0;
	sDeviceManager->get_attr_uint8(parent, B_DEVICE_TYPE, &classBase, false);
	sDeviceManager->get_attr_uint8(parent, B_DEVICE_SUB_TYPE, &classSub,
		false);

	// match PCI HD Audio class
	if (classBase == PCI_multimedia && classSub == PCI_hd_audio)
		return 0.6;

	// also match specific device IDs
	uint16 vendorId = 0, deviceId = 0;
	sDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID,
		&vendorId, false);
	sDeviceManager->get_attr_uint16(parent, B_DEVICE_ID,
		&deviceId, false);

	if (is_supported_device(vendorId, deviceId))
		return 0.6;

	return 0.0;
}


static status_t
hda_register_device(device_node *node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "HD Audio Controller"} },
		{ NULL }
	};

	return sDeviceManager->register_node(node, HDA_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
hda_init_driver(device_node *node, void **cookie)
{
	status_t status = add_card_from_node(node);
	if (status != B_OK)
		return status;

	*cookie = node;
	return B_OK;
}


static void
hda_uninit_driver(void *_cookie)
{
	// cleanup happens globally, not per-device
}


static status_t
hda_register_child_devices(void *_cookie)
{
	device_node *node = (device_node *)_cookie;

	for (uint32 i = 0; i < gNumCards; i++) {
		if (gCards[i].devfs_path != NULL) {
			sDeviceManager->publish_device(node, gCards[i].devfs_path,
				HDA_DEVICE_MODULE_NAME);
		}
	}

	return B_OK;
}


//	#pragma mark - device module API


static status_t
hda_init_device(void *_info, void **_cookie)
{
	*_cookie = _info;
	return B_OK;
}


static void
hda_uninit_device(void *_cookie)
{
}


//	#pragma mark -

// The device hooks (open/close/free/read/write/control) are defined
// in device.cpp as gDriverHooks. We wrap them for device_module_info.

static status_t
hda_open(void *_info, const char *path, int openMode, void **_cookie)
{
	return gDriverHooks.open(path, openMode, _cookie);
}


static status_t
hda_close(void *cookie)
{
	return gDriverHooks.close(cookie);
}


static status_t
hda_free(void *cookie)
{
	return gDriverHooks.free(cookie);
}


static status_t
hda_read(void *cookie, off_t position, void *buffer, size_t *numBytes)
{
	return gDriverHooks.read(cookie, position, buffer, numBytes);
}


static status_t
hda_write(void *cookie, off_t position, const void *buffer, size_t *numBytes)
{
	return gDriverHooks.write(cookie, position, buffer, numBytes);
}


static status_t
hda_control(void *cookie, uint32 op, void *buffer, size_t length)
{
	return gDriverHooks.control(cookie, op, buffer, length);
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&sDeviceManager },
	{ NULL }
};

struct device_module_info sHdaDevice = {
	{
		HDA_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	hda_init_device,
	hda_uninit_device,
	NULL,	// device_removed

	hda_open,
	hda_close,
	hda_free,
	hda_read,
	hda_write,
	NULL,	// io
	hda_control,

	NULL,	// select
	NULL,	// deselect
};

struct driver_module_info sHdaDriver = {
	{
		HDA_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	hda_supports_device,
	hda_register_device,
	hda_init_driver,
	hda_uninit_driver,
	hda_register_child_devices,
	NULL,	// rescan
	NULL,	// removed
};

module_info* modules[] = {
	(module_info*)&sHdaDriver,
	(module_info*)&sHdaDevice,
	NULL
};
