/*
	Driver for USB RNDIS Network devices
	Copyright (C) 2022 Adrien Destugues <pulkomandy@pulkomandy.tk>
	Distributed under the terms of the MIT license.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lock.h>

#include <bus/USB.h>

#include "Driver.h"
#include "RNDISDevice.h"


#define DEVICE_BASE_NAME "net/usb_rndis/"

usb_module_info *gUSBModule = NULL;
device_manager_info *gDeviceManager = NULL;


#define USB_RNDIS_DRIVER_MODULE_NAME "drivers/network/usb_rndis/driver_v1"
#define USB_RNDIS_DEVICE_MODULE_NAME "drivers/network/usb_rndis/device_v1"
#define USB_RNDIS_DEVICE_ID_GENERATOR "usb_rndis/device_id"


// RNDIS class/subclass/protocol
#define USB_RNDIS_DEVICE_CLASS		0xE0
#define USB_RNDIS_SUBCLASS			0x01
#define USB_RNDIS_PROTOCOL			0x03


//	#pragma mark - device module API


static status_t
usb_rndis_init_device(void* _info, void** _cookie)
{
	TRACE("init_device()\n");
	usb_rndis_driver_info* info = (usb_rndis_driver_info*)_info;

	device_node* parent = gDeviceManager->get_parent_node(info->node);
	gDeviceManager->get_driver(parent, (driver_module_info**)&info->usb,
		(void**)&info->usb_device);
	gDeviceManager->put_node(parent);

	usb_device device;
	if (gDeviceManager->get_attr_uint32(info->node, USB_DEVICE_ID_ITEM,
			&device, true) != B_OK)
		return B_ERROR;

	RNDISDevice *rndisDevice = new RNDISDevice(device);
	status_t status = rndisDevice->InitCheck();
	if (status < B_OK) {
		delete rndisDevice;
		return status;
	}

	info->device = rndisDevice;
	*_cookie = info;
	return B_OK;
}


static void
usb_rndis_uninit_device(void* _cookie)
{
	TRACE("uninit_device()\n");
	usb_rndis_driver_info* info = (usb_rndis_driver_info*)_cookie;
	delete info->device;
	info->device = NULL;
}


static void
usb_rndis_device_removed(void* _cookie)
{
	TRACE("device_removed()\n");
	usb_rndis_driver_info* info = (usb_rndis_driver_info*)_cookie;
	if (info->device != NULL)
		info->device->Removed();
}


static status_t
usb_rndis_open(void* _info, const char* path, int openMode, void** _cookie)
{
	TRACE("open()\n");
	usb_rndis_driver_info* info = (usb_rndis_driver_info*)_info;

	status_t status = info->device->Open();
	if (status != B_OK)
		return status;

	*_cookie = info->device;
	return B_OK;
}


static status_t
usb_rndis_read(void *cookie, off_t position, void *buffer, size_t *numBytes)
{
	TRACE("read(%p, %" B_PRIdOFF ", %p, %lu)\n", cookie, position, buffer,
		*numBytes);
	RNDISDevice *device = (RNDISDevice *)cookie;
	return device->Read((uint8 *)buffer, numBytes);
}


static status_t
usb_rndis_write(void *cookie, off_t position, const void *buffer,
	size_t *numBytes)
{
	TRACE("write(%p, %" B_PRIdOFF ", %p, %lu)\n", cookie, position, buffer,
		*numBytes);
	RNDISDevice *device = (RNDISDevice *)cookie;
	return device->Write((const uint8 *)buffer, numBytes);
}


static status_t
usb_rndis_control(void *cookie, uint32 op, void *buffer, size_t length)
{
	TRACE("control(%p, %" B_PRIu32 ", %p, %lu)\n", cookie, op, buffer,
		length);
	RNDISDevice *device = (RNDISDevice *)cookie;
	return device->Control(op, buffer, length);
}


static status_t
usb_rndis_close(void *cookie)
{
	TRACE("close(%p)\n", cookie);
	RNDISDevice *device = (RNDISDevice *)cookie;
	return device->Close();
}


static status_t
usb_rndis_free(void *cookie)
{
	TRACE("free(%p)\n", cookie);
	RNDISDevice *device = (RNDISDevice *)cookie;
	return device->Free();
}


//	#pragma mark - driver module API


static float
usb_rndis_supports_device(device_node *parent)
{
	TRACE("supports_device()\n");
	const char *bus;

	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;

	if (strcmp(bus, "usb"))
		return 0.0;

	// RNDIS: class 0xE0, subclass 0x01, protocol 0x03
	uint8 deviceClass = 0, deviceSubclass = 0, deviceProtocol = 0;
	device_attr *attr = NULL;
	while (gDeviceManager->get_next_attr(parent, &attr) == B_OK) {
		if (attr->type != B_UINT8_TYPE)
			continue;

		if (!strcmp(attr->name, USB_DEVICE_CLASS))
			deviceClass = attr->value.ui8;
		if (!strcmp(attr->name, USB_DEVICE_SUBCLASS))
			deviceSubclass = attr->value.ui8;
		if (!strcmp(attr->name, USB_DEVICE_PROTOCOL))
			deviceProtocol = attr->value.ui8;
	}

	if (deviceClass == USB_RNDIS_DEVICE_CLASS
		&& deviceSubclass == USB_RNDIS_SUBCLASS
		&& deviceProtocol == USB_RNDIS_PROTOCOL) {
		TRACE("USB RNDIS device found!\n");
		return 0.6;
	}

	return 0.0;
}


static status_t
usb_rndis_register_device(device_node *node)
{
	TRACE("register_device()\n");

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "USB RNDIS"} },
		{ NULL }
	};

	return gDeviceManager->register_node(node, USB_RNDIS_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
usb_rndis_init_driver(device_node *node, void **cookie)
{
	TRACE("init_driver()\n");

	usb_rndis_driver_info* info = (usb_rndis_driver_info*)malloc(
		sizeof(usb_rndis_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));
	info->node = node;

	*cookie = info;
	return B_OK;
}


static void
usb_rndis_uninit_driver(void *_cookie)
{
	TRACE("uninit_driver()\n");
	usb_rndis_driver_info* info = (usb_rndis_driver_info*)_cookie;
	free(info);
}


static status_t
usb_rndis_register_child_devices(void* _cookie)
{
	TRACE("register_child_devices()\n");
	usb_rndis_driver_info* info = (usb_rndis_driver_info*)_cookie;

	int32 id = gDeviceManager->create_id(USB_RNDIS_DEVICE_ID_GENERATOR);
	if (id < 0)
		return id;

	char name[64];
	snprintf(name, sizeof(name), DEVICE_BASE_NAME "%" B_PRId32, id);

	return gDeviceManager->publish_device(info->node, name,
		USB_RNDIS_DEVICE_MODULE_NAME);
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{ B_USB_MODULE_NAME, (module_info**)&gUSBModule },
	{ NULL }
};

struct device_module_info sUsbRndisDevice = {
	{
		USB_RNDIS_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	usb_rndis_init_device,
	usb_rndis_uninit_device,
	usb_rndis_device_removed,

	usb_rndis_open,
	usb_rndis_close,
	usb_rndis_free,
	usb_rndis_read,
	usb_rndis_write,
	NULL,	// io
	usb_rndis_control,

	NULL,	// select
	NULL,	// deselect
};

struct driver_module_info sUsbRndisDriver = {
	{
		USB_RNDIS_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	usb_rndis_supports_device,
	usb_rndis_register_device,
	usb_rndis_init_driver,
	usb_rndis_uninit_driver,
	usb_rndis_register_child_devices,
	NULL,	// rescan
	NULL,	// removed
};

module_info* modules[] = {
	(module_info*)&sUsbRndisDriver,
	(module_info*)&sUsbRndisDevice,
	NULL
};
