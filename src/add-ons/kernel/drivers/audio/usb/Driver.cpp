/*
 *	Driver for USB Audio Device Class devices.
 *	Copyright (c) 2009-13 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 *
 */

#include "Driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AutoLock.h>
#include <usb/USB_audio.h>

#include <bus/USB.h>

#include "Device.h"
#include "Settings.h"


#define DEVICE_BASE_NAME "audio/hmulti/usb/"

usb_module_info* gUSBModule = NULL;
device_manager_info* gDeviceManager = NULL;


#define USB_AUDIO_DRIVER_MODULE_NAME "drivers/audio/usb_audio/driver_v1"
#define USB_AUDIO_DEVICE_MODULE_NAME "drivers/audio/usb_audio/device_v1"
#define USB_AUDIO_DEVICE_ID_GENERATOR "usb_audio/device_id"


//	#pragma mark - device module API


static status_t
usb_audio_init_device(void* _info, void** _cookie)
{
	TRACE(INF, "init_device()\n");
	usb_audio_driver_info* info = (usb_audio_driver_info*)_info;

	device_node* parent = gDeviceManager->get_parent_node(info->node);
	gDeviceManager->get_driver(parent, (driver_module_info**)&info->usb,
		(void**)&info->usb_device);
	gDeviceManager->put_node(parent);

	usb_device device;
	if (gDeviceManager->get_attr_uint32(info->node, USB_DEVICE_ID_ITEM,
			&device, true) != B_OK)
		return B_ERROR;

	Device* audioDevice = new(std::nothrow) Device(device);
	if (audioDevice == NULL)
		return ENODEV;

	status_t status = audioDevice->InitCheck();
	if (status < B_OK) {
		delete audioDevice;
		return status;
	}

	status = audioDevice->SetupDevice(false);
	if (status < B_OK) {
		delete audioDevice;
		return status;
	}

	info->device = audioDevice;
	*_cookie = info;
	return B_OK;
}


static void
usb_audio_uninit_device(void* _cookie)
{
	TRACE(INF, "uninit_device()\n");
	usb_audio_driver_info* info = (usb_audio_driver_info*)_cookie;
	delete info->device;
	info->device = NULL;
}


static void
usb_audio_device_removed(void* _cookie)
{
	TRACE(INF, "device_removed()\n");
	usb_audio_driver_info* info = (usb_audio_driver_info*)_cookie;
	if (info->device != NULL)
		info->device->Removed();
}


static status_t
usb_audio_open(void* _info, const char* path, int openMode, void** _cookie)
{
	TRACE(INF, "open()\n");
	usb_audio_driver_info* info = (usb_audio_driver_info*)_info;

	status_t status = info->device->Open(openMode);
	if (status != B_OK)
		return status;

	*_cookie = info->device;
	return B_OK;
}


static status_t
usb_audio_read(void* cookie, off_t position, void* buffer, size_t* numBytes)
{
	Device* device = (Device*)cookie;
	return device->Read((uint8*)buffer, numBytes);
}


static status_t
usb_audio_write(void* cookie, off_t position, const void* buffer,
	size_t* numBytes)
{
	Device* device = (Device*)cookie;
	return device->Write((const uint8*)buffer, numBytes);
}


static status_t
usb_audio_control(void* cookie, uint32 op, void* buffer, size_t length)
{
	Device* device = (Device*)cookie;
	return device->Control(op, buffer, length);
}


static status_t
usb_audio_close(void* cookie)
{
	Device* device = (Device*)cookie;
	return device->Close();
}


static status_t
usb_audio_free(void* cookie)
{
	Device* device = (Device*)cookie;
	return device->Free();
}


//	#pragma mark - driver module API


static float
usb_audio_supports_device(device_node *parent)
{
	TRACE(INF, "supports_device()\n");
	const char *bus;

	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;

	if (strcmp(bus, "usb"))
		return 0.0;

	// USB Audio Class: class 1
	uint8 deviceClass = 0;
	device_attr *attr = NULL;
	while (gDeviceManager->get_next_attr(parent, &attr) == B_OK) {
		if (attr->type != B_UINT8_TYPE)
			continue;

		if (!strcmp(attr->name, USB_DEVICE_CLASS))
			deviceClass = attr->value.ui8;
		if (deviceClass == USB_AUDIO_INTERFACE_AUDIO_CLASS) {
			TRACE(INF, "USB Audio device found!\n");
			return 0.6;
		}
	}

	return 0.0;
}


static status_t
usb_audio_register_device(device_node *node)
{
	TRACE(INF, "register_device()\n");

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "USB Audio"} },
		{ NULL }
	};

	return gDeviceManager->register_node(node, USB_AUDIO_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
usb_audio_init_driver(device_node *node, void **cookie)
{
	TRACE(INF, "init_driver()\n");

	usb_audio_driver_info* info = (usb_audio_driver_info*)malloc(
		sizeof(usb_audio_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));
	info->node = node;

	load_settings();

	*cookie = info;
	return B_OK;
}


static void
usb_audio_uninit_driver(void *_cookie)
{
	TRACE(INF, "uninit_driver()\n");
	usb_audio_driver_info* info = (usb_audio_driver_info*)_cookie;

	release_settings();
	free(info);
}


static status_t
usb_audio_register_child_devices(void* _cookie)
{
	TRACE(INF, "register_child_devices()\n");
	usb_audio_driver_info* info = (usb_audio_driver_info*)_cookie;

	int32 id = gDeviceManager->create_id(USB_AUDIO_DEVICE_ID_GENERATOR);
	if (id < 0)
		return id;

	char name[64];
	snprintf(name, sizeof(name), DEVICE_BASE_NAME "%" B_PRId32, id);

	return gDeviceManager->publish_device(info->node, name,
		USB_AUDIO_DEVICE_MODULE_NAME);
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{ B_USB_MODULE_NAME, (module_info**)&gUSBModule },
	{ NULL }
};

struct device_module_info sUsbAudioDevice = {
	{
		USB_AUDIO_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	usb_audio_init_device,
	usb_audio_uninit_device,
	usb_audio_device_removed,

	usb_audio_open,
	usb_audio_close,
	usb_audio_free,
	usb_audio_read,
	usb_audio_write,
	NULL,	// io
	usb_audio_control,

	NULL,	// select
	NULL,	// deselect
};

struct driver_module_info sUsbAudioDriver = {
	{
		USB_AUDIO_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	usb_audio_supports_device,
	usb_audio_register_device,
	usb_audio_init_driver,
	usb_audio_uninit_driver,
	usb_audio_register_child_devices,
	NULL,	// rescan
	NULL,	// removed
};

module_info* modules[] = {
	(module_info*)&sUsbAudioDriver,
	(module_info*)&sUsbAudioDevice,
	NULL
};
