/*
 *	Davicom DM9601 USB 1.1 Ethernet Driver.
 *	Copyright (c) 2008, 2011 Siarzhuk Zharski <imker@gmx.li>
 *	Copyright (c) 2009 Adrien Destugues <pulkomandy@gmail.com>
 *	Distributed under the terms of the MIT license.
 *
 *	Heavily based on code of the
 *	Driver for USB Ethernet Control Model devices
 *	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
 *	Distributed under the terms of the MIT license.
 */


#include "Driver.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bus/USB.h>
#include <lock.h>

#include "DavicomDevice.h"
#include "Settings.h"


#define DEVICE_BASE_NAME "net/usb_davicom/"

usb_module_info *gUSBModule = NULL;
dk_keeper_info *gDeviceKeeper = NULL;


#define USB_DAVICOM_DRIVER_MODULE_NAME "drivers/network/usb_davicom/dk_driver_v1"
#define USB_DAVICOM_DEVICE_ID_GENERATOR	"usb_davicom/device_id"


// IMPORTANT: keep entries sorted by ids to let the
// binary search lookup procedure work correctly !!!
DeviceInfo gSupportedDevices[] = {
	{ { 0x01e1, 0x9601 }, "Noname DM9601" },
	{ { 0x07aa, 0x9601 }, "Corega FEther USB-TXC" },
	{ { 0x0a46, 0x0268 }, "ShanTou ST268 USB NIC" },
	{ { 0x0a46, 0x6688 }, "ZT6688 USB NIC" },
	{ { 0x0a46, 0x8515 }, "ADMtek ADM8515 USB NIC" },
	{ { 0x0a46, 0x9000 }, "DM9000E" },
	{ { 0x0a46, 0x9601 }, "Davicom DM9601" },
	{ { 0x0a47, 0x9601 }, "Hirose USB-100" },
	{ { 0x0fe6, 0x8101 }, "Sunrising SR9600" },
	{ { 0x0fe6, 0x9700 }, "Kontron DM9601" }
};


static bool
is_supported_device(uint16 vendorId, uint16 productId)
{
	uint32 id = vendorId << 16 | productId;
	int left = -1;
	int right = B_COUNT_OF(gSupportedDevices);
	while ((right - left) > 1) {
		int i = (left + right) / 2;
		((gSupportedDevices[i].Key() < id) ? left : right) = i;
	}
	return right < (int)B_COUNT_OF(gSupportedDevices)
		&& gSupportedDevices[right].Key() == id;
}


//	#pragma mark - device ops


static void
usb_davicom_device_removed(void* _cookie)
{
	TRACE("device_removed()\n");
	usb_davicom_driver_info* info = (usb_davicom_driver_info*)_cookie;
	if (info->device != NULL)
		info->device->Removed();
}


static status_t
usb_davicom_open(void* _info, const char* path, int openMode, void** _cookie)
{
	TRACE("open()\n");
	usb_davicom_driver_info* info = (usb_davicom_driver_info*)_info;

	status_t status = info->device->Open(openMode);
	if (status != B_OK)
		return status;

	*_cookie = info->device;
	return B_OK;
}


static status_t
usb_davicom_read(void *cookie, off_t position, void *buffer, size_t *numBytes)
{
	DavicomDevice *device = (DavicomDevice *)cookie;
	return device->Read((uint8 *)buffer, numBytes);
}


static status_t
usb_davicom_write(void *cookie, off_t position, const void *buffer,
	size_t *numBytes)
{
	DavicomDevice *device = (DavicomDevice *)cookie;
	return device->Write((const uint8 *)buffer, numBytes);
}


static status_t
usb_davicom_control(void *cookie, uint32 op, void *buffer, size_t length)
{
	DavicomDevice *device = (DavicomDevice *)cookie;
	return device->Control(op, buffer, length);
}


static status_t
usb_davicom_close(void *cookie)
{
	DavicomDevice *device = (DavicomDevice *)cookie;
	return device->Close();
}


static status_t
usb_davicom_free(void *cookie)
{
	DavicomDevice *device = (DavicomDevice *)cookie;
	return device->Free();
}


static dk_device_ops sUsbDavicomDeviceOps = {
	usb_davicom_open,
	usb_davicom_close,
	usb_davicom_free,
	usb_davicom_read,
	usb_davicom_write,
	NULL,	// io
	usb_davicom_control,
	NULL,	// select
	NULL,	// deselect
	usb_davicom_device_removed,
};


//	#pragma mark - driver module API


static float
usb_davicom_supports_device(dk_node *parent)
{
	TRACE("supports_device()\n");
	char bus[64];

	if (gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1;

	if (strcmp(bus, "usb"))
		return 0.0;

	uint16 vendorId, productId;
	if (gDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_VENDOR_ID,
			&vendorId, false) != B_OK)
		return 0.0;
	if (gDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_ID,
			&productId, false) != B_OK)
		return 0.0;

	if (!is_supported_device(vendorId, productId))
		return 0.0;

	TRACE("Davicom device found! vendor: 0x%04x, product: 0x%04x\n",
		vendorId, productId);

	return 0.6;
}


static status_t
usb_davicom_attach(dk_node *node, void **cookie)
{
	TRACE("attach()\n");

	usb_davicom_driver_info* info = (usb_davicom_driver_info*)malloc(
		sizeof(usb_davicom_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));
	info->node = node;
	info->id = -1;

	load_settings();

	// USB peripheral drivers consume gUSBModule directly; there is no
	// typed per-device bus interface to retrieve.
	usb_device device;
	if (gDeviceKeeper->get_property_uint32(node, USB_DEVICE_ID_ITEM,
			&device, true) != B_OK) {
		release_settings();
		free(info);
		return B_ERROR;
	}

	// Look up device info
	const usb_device_descriptor* deviceDescriptor
		= gUSBModule->get_device_descriptor(device);
	if (deviceDescriptor == NULL) {
		TRACE_ALWAYS("Error getting USB device descriptor.\n");
		release_settings();
		free(info);
		return B_ERROR;
	}

	uint32 id = deviceDescriptor->vendor_id << 16
		| deviceDescriptor->product_id;
	int left = -1;
	int right = B_COUNT_OF(gSupportedDevices);
	while ((right - left) > 1) {
		int i = (left + right) / 2;
		((gSupportedDevices[i].Key() < id) ? left : right) = i;
	}

	if (right >= (int)B_COUNT_OF(gSupportedDevices)
			|| gSupportedDevices[right].Key() != id) {
		TRACE_ALWAYS("Device not found in supported list.\n");
		release_settings();
		free(info);
		return B_ERROR;
	}

	DavicomDevice* davicomDevice
		= new(std::nothrow) DavicomDevice(device, gSupportedDevices[right]);
	if (davicomDevice == NULL) {
		release_settings();
		free(info);
		return B_NO_MEMORY;
	}

	status_t status = davicomDevice->InitCheck();
	if (status < B_OK) {
		delete davicomDevice;
		release_settings();
		free(info);
		return status;
	}

	status = davicomDevice->SetupDevice(false);
	if (status < B_OK) {
		delete davicomDevice;
		release_settings();
		free(info);
		return status;
	}

	info->device = davicomDevice;

	info->id = gDeviceKeeper->create_id(USB_DAVICOM_DEVICE_ID_GENERATOR);
	if (info->id < 0) {
		status = info->id;
		delete davicomDevice;
		release_settings();
		free(info);
		return status;
	}

	snprintf(info->publishedPath, sizeof(info->publishedPath),
		DEVICE_BASE_NAME "%" B_PRId32, info->id);
	status = gDeviceKeeper->publish_device(node, info->publishedPath,
		&sUsbDavicomDeviceOps);
	if (status != B_OK) {
		gDeviceKeeper->free_id(USB_DAVICOM_DEVICE_ID_GENERATOR, info->id);
		delete davicomDevice;
		release_settings();
		free(info);
		return status;
	}

	*cookie = info;
	return B_OK;
}


static void
usb_davicom_detach(void *_cookie)
{
	TRACE("detach()\n");
	usb_davicom_driver_info* info = (usb_davicom_driver_info*)_cookie;
	if (info == NULL)
		return;

	if (info->publishedPath[0] != '\0')
		gDeviceKeeper->unpublish_device(info->node, info->publishedPath);
	if (info->id >= 0)
		gDeviceKeeper->free_id(USB_DAVICOM_DEVICE_ID_GENERATOR, info->id);

	delete info->device;
	release_settings();
	free(info);
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ B_USB_MODULE_NAME, (module_info**)&gUSBModule },
	{ NULL }
};

static const dk_match_rule sUsbDavicomMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "usb"),
	DK_MATCH_END
};

static const dk_match_dict sUsbDavicomMatchDict = {
	sUsbDavicomMatchRules,
	0
};


static dk_driver_info sUsbDavicomDriver = {
	.info = {
		USB_DAVICOM_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.match   = &sUsbDavicomMatchDict,
	.probe   = usb_davicom_supports_device,
	.attach  = usb_davicom_attach,
	.detach  = usb_davicom_detach,
	.ops     = &sUsbDavicomDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sUsbDavicomDriver,
	NULL
};
