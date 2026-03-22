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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bus/USB.h>
#include <lock.h>

#include "DavicomDevice.h"
#include "Settings.h"


#define DEVICE_BASE_NAME "net/usb_davicom/"

usb_module_info *gUSBModule = NULL;
device_manager_info *gDeviceManager = NULL;


#define USB_DAVICOM_DRIVER_MODULE_NAME "drivers/network/usb_davicom/driver_v1"
#define USB_DAVICOM_DEVICE_MODULE_NAME "drivers/network/usb_davicom/device_v1"
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


//	#pragma mark - device module API


static status_t
usb_davicom_init_device(void* _info, void** _cookie)
{
	TRACE("init_device()\n");
	usb_davicom_driver_info* info = (usb_davicom_driver_info*)_info;

	device_node* parent = gDeviceManager->get_parent_node(info->node);
	gDeviceManager->get_driver(parent, (driver_module_info**)&info->usb,
		(void**)&info->usb_device);
	gDeviceManager->put_node(parent);

	usb_device device;
	if (gDeviceManager->get_attr_uint32(info->node, USB_DEVICE_ID_ITEM,
			&device, true) != B_OK)
		return B_ERROR;

	// look up device info for this vendor/product pair
	const usb_device_descriptor* deviceDescriptor
		= gUSBModule->get_device_descriptor(device);
	if (deviceDescriptor == NULL) {
		TRACE_ALWAYS("Error getting USB device descriptor.\n");
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
		return B_ERROR;
	}

	DavicomDevice* davicomDevice
		= new DavicomDevice(device, gSupportedDevices[right]);
	status_t status = davicomDevice->InitCheck();
	if (status < B_OK) {
		delete davicomDevice;
		return status;
	}

	status = davicomDevice->SetupDevice(false);
	if (status < B_OK) {
		delete davicomDevice;
		return status;
	}

	info->device = davicomDevice;
	*_cookie = info;
	return B_OK;
}


static void
usb_davicom_uninit_device(void* _cookie)
{
	TRACE("uninit_device()\n");
	usb_davicom_driver_info* info = (usb_davicom_driver_info*)_cookie;
	delete info->device;
	info->device = NULL;
}


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


//	#pragma mark - driver module API


static float
usb_davicom_supports_device(device_node *parent)
{
	TRACE("supports_device()\n");
	const char *bus;

	// make sure parent is really the usb bus manager
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;

	if (strcmp(bus, "usb"))
		return 0.0;

	// check vendor/product ID against our supported device table
	uint16 vendorId, productId;
	if (gDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID,
			&vendorId, false) != B_OK)
		return 0.0;
	if (gDeviceManager->get_attr_uint16(parent, B_DEVICE_ID,
			&productId, false) != B_OK)
		return 0.0;

	if (!is_supported_device(vendorId, productId))
		return 0.0;

	TRACE("Davicom device found! vendor: 0x%04x, product: 0x%04x\n",
		vendorId, productId);

	return 0.6;
}


static status_t
usb_davicom_register_device(device_node *node)
{
	TRACE("register_device()\n");

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "USB Davicom DM9601"} },
		{ NULL }
	};

	return gDeviceManager->register_node(node, USB_DAVICOM_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
usb_davicom_init_driver(device_node *node, void **cookie)
{
	TRACE("init_driver()\n");

	usb_davicom_driver_info* info = (usb_davicom_driver_info*)malloc(
		sizeof(usb_davicom_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));
	info->node = node;

	load_settings();

	*cookie = info;
	return B_OK;
}


static void
usb_davicom_uninit_driver(void *_cookie)
{
	TRACE("uninit_driver()\n");
	usb_davicom_driver_info* info = (usb_davicom_driver_info*)_cookie;

	release_settings();
	free(info);
}


static status_t
usb_davicom_register_child_devices(void* _cookie)
{
	TRACE("register_child_devices()\n");
	usb_davicom_driver_info* info = (usb_davicom_driver_info*)_cookie;

	int32 id = gDeviceManager->create_id(USB_DAVICOM_DEVICE_ID_GENERATOR);
	if (id < 0)
		return id;

	char name[64];
	snprintf(name, sizeof(name), DEVICE_BASE_NAME "%" B_PRId32, id);

	return gDeviceManager->publish_device(info->node, name,
		USB_DAVICOM_DEVICE_MODULE_NAME);
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{ B_USB_MODULE_NAME, (module_info**)&gUSBModule },
	{ NULL }
};

struct device_module_info sUsbDavicomDevice = {
	{
		USB_DAVICOM_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	usb_davicom_init_device,
	usb_davicom_uninit_device,
	usb_davicom_device_removed,

	usb_davicom_open,
	usb_davicom_close,
	usb_davicom_free,
	usb_davicom_read,
	usb_davicom_write,
	NULL,	// io
	usb_davicom_control,

	NULL,	// select
	NULL,	// deselect
};

struct driver_module_info sUsbDavicomDriver = {
	{
		USB_DAVICOM_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	usb_davicom_supports_device,
	usb_davicom_register_device,
	usb_davicom_init_driver,
	usb_davicom_uninit_driver,
	usb_davicom_register_child_devices,
	NULL,	// rescan
	NULL,	// removed
};

module_info* modules[] = {
	(module_info*)&sUsbDavicomDriver,
	(module_info*)&sUsbDavicomDevice,
	NULL
};
