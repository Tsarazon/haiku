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
dk_keeper_info* gDeviceKeeper = NULL;


#define USB_AUDIO_DRIVER_MODULE_NAME "drivers/audio/usb_audio/dk_driver_v1"
#define USB_AUDIO_DEVICE_ID_GENERATOR "usb_audio/device_id"


//	#pragma mark - dk_device_ops


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


static void
usb_audio_device_removed(void* _info)
{
	TRACE(INF, "device_removed()\n");
	usb_audio_driver_info* info = (usb_audio_driver_info*)_info;
	if (info->device != NULL)
		info->device->Removed();
}


static dk_device_ops sDeviceOps = {
	usb_audio_open,
	usb_audio_close,
	usb_audio_free,
	usb_audio_read,
	usb_audio_write,
	NULL,	// io
	usb_audio_control,
	NULL,	// select
	NULL,	// deselect
	usb_audio_device_removed,
};


//	#pragma mark - dk_driver_info


static const dk_match_rule sUsbAudioMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "usb"),
	DK_MATCH_END
};

static const dk_match_dict sUsbAudioMatchDict = {
	sUsbAudioMatchRules,
	0
};


static float
usb_audio_probe(dk_node *node)
{
	TRACE(INF, "probe()\n");

	// USB Audio Class: class 1
	uint8 deviceClass = 0;
	if (gDeviceKeeper->get_property_uint8(node, KOSM_USB_DEVICE_CLASS,
			&deviceClass, false) == B_OK
		&& deviceClass == USB_AUDIO_INTERFACE_AUDIO_CLASS) {
		TRACE(INF, "USB Audio device found!\n");
		return 0.6;
	}

	uint8 ifClass = 0;
	if (gDeviceKeeper->get_property_uint8(node, KOSM_USB_INTERFACE_CLASS,
			&ifClass, false) == B_OK
		&& ifClass == USB_AUDIO_INTERFACE_AUDIO_CLASS) {
		TRACE(INF, "USB Audio interface found!\n");
		return 0.6;
	}

	return 0.0;
}


static status_t
usb_audio_attach(dk_node *node, void **cookie)
{
	TRACE(INF, "attach()\n");

	usb_audio_driver_info* info = (usb_audio_driver_info*)malloc(
		sizeof(usb_audio_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));
	info->node = node;
	info->id = -1;

	load_settings();

	// Get USB device ID from node properties
	usb_device usbDeviceId;
	if (gDeviceKeeper->get_property_uint32(node, USB_DEVICE_ID_ITEM,
			&usbDeviceId, true) != B_OK) {
		release_settings();
		free(info);
		return B_ERROR;
	}

	Device* audioDevice = new(std::nothrow) Device(usbDeviceId);
	if (audioDevice == NULL) {
		release_settings();
		free(info);
		return ENODEV;
	}

	status_t status = audioDevice->InitCheck();
	if (status < B_OK) {
		delete audioDevice;
		release_settings();
		free(info);
		return status;
	}

	status = audioDevice->SetupDevice(false);
	if (status < B_OK) {
		delete audioDevice;
		release_settings();
		free(info);
		return status;
	}

	info->device = audioDevice;

	info->id = gDeviceKeeper->create_id(USB_AUDIO_DEVICE_ID_GENERATOR);
	if (info->id < 0) {
		status = info->id;
		delete audioDevice;
		release_settings();
		free(info);
		return status;
	}

	snprintf(info->publishedPath, sizeof(info->publishedPath),
		DEVICE_BASE_NAME "%" B_PRId32, info->id);
	status = gDeviceKeeper->publish_device(node, info->publishedPath,
		&sDeviceOps);
	if (status != B_OK) {
		gDeviceKeeper->free_id(USB_AUDIO_DEVICE_ID_GENERATOR, info->id);
		delete audioDevice;
		release_settings();
		free(info);
		return status;
	}

	*cookie = info;
	return B_OK;
}


static void
usb_audio_detach(void *_cookie)
{
	TRACE(INF, "detach()\n");
	usb_audio_driver_info* info = (usb_audio_driver_info*)_cookie;
	if (info == NULL)
		return;

	if (info->publishedPath[0] != '\0')
		gDeviceKeeper->unpublish_device(info->node, info->publishedPath);
	if (info->id >= 0)
		gDeviceKeeper->free_id(USB_AUDIO_DEVICE_ID_GENERATOR, info->id);

	delete info->device;
	info->device = NULL;
	release_settings();
	free(info);
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ B_USB_MODULE_NAME, (module_info**)&gUSBModule },
	{ NULL }
};

struct dk_driver_info sUsbAudioDriver = {
	.info = {
		USB_AUDIO_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.match   = &sUsbAudioMatchDict,
	.probe   = usb_audio_probe,
	.attach  = usb_audio_attach,
	.detach  = usb_audio_detach,
	.ops     = &sDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sUsbAudioDriver,
	NULL
};
