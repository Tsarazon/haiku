/*
	Driver for USB RNDIS Network devices
	Copyright (C) 2022 Adrien Destugues <pulkomandy@pulkomandy.tk>
	Distributed under the terms of the MIT license.
*/
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lock.h>

#include <bus/USB.h>

#include "Driver.h"
#include "RNDISDevice.h"


#define DEVICE_BASE_NAME "net/usb_rndis/"

usb_module_info *gUSBModule = NULL;
dk_keeper_info *gDeviceKeeper = NULL;


#define USB_RNDIS_DRIVER_MODULE_NAME "drivers/network/usb_rndis/dk_driver_v1"
#define USB_RNDIS_DEVICE_ID_GENERATOR "usb_rndis/device_id"


// RNDIS class/subclass/protocol
#define USB_RNDIS_DEVICE_CLASS		0xE0
#define USB_RNDIS_SUBCLASS			0x01
#define USB_RNDIS_PROTOCOL			0x03


//	#pragma mark - dk_device_ops


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


static void
usb_rndis_device_removed(void* _info)
{
	TRACE("device_removed()\n");
	usb_rndis_driver_info* info = (usb_rndis_driver_info*)_info;
	if (info->device != NULL)
		info->device->Removed();
}


static dk_device_ops sDeviceOps = {
	usb_rndis_open,
	usb_rndis_close,
	usb_rndis_free,
	usb_rndis_read,
	usb_rndis_write,
	NULL,	// io
	usb_rndis_control,
	NULL,	// select
	NULL,	// deselect
	usb_rndis_device_removed,
};


//	#pragma mark - dk_driver_info


static const dk_match_rule sUsbRndisMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "usb"),
	DK_MATCH_END
};

static const dk_match_dict sUsbRndisMatchDict = {
	sUsbRndisMatchRules,
	0
};


static float
usb_rndis_probe(dk_node *node)
{
	TRACE("probe()\n");

	// RNDIS: class 0xE0, subclass 0x01, protocol 0x03
	uint8 deviceClass = 0, deviceSubclass = 0, deviceProtocol = 0;
	gDeviceKeeper->get_property_uint8(node, KOSM_USB_DEVICE_CLASS,
		&deviceClass, false);
	gDeviceKeeper->get_property_uint8(node, KOSM_USB_DEVICE_SUBCLASS,
		&deviceSubclass, false);
	gDeviceKeeper->get_property_uint8(node, KOSM_USB_DEVICE_PROTOCOL,
		&deviceProtocol, false);

	if (deviceClass == USB_RNDIS_DEVICE_CLASS
		&& deviceSubclass == USB_RNDIS_SUBCLASS
		&& deviceProtocol == USB_RNDIS_PROTOCOL) {
		TRACE("USB RNDIS device found!\n");
		return 0.6;
	}

	return 0.0;
}


static status_t
usb_rndis_attach(dk_node *node, void **cookie)
{
	TRACE("attach()\n");

	usb_rndis_driver_info* info = (usb_rndis_driver_info*)malloc(
		sizeof(usb_rndis_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));
	info->node = node;
	info->id = -1;

	usb_device usbDeviceId;
	if (gDeviceKeeper->get_property_uint32(node, USB_DEVICE_ID_ITEM,
			&usbDeviceId, true) != B_OK) {
		free(info);
		return B_ERROR;
	}

	RNDISDevice *rndisDevice = new(std::nothrow) RNDISDevice(usbDeviceId);
	if (rndisDevice == NULL) {
		free(info);
		return B_NO_MEMORY;
	}

	status_t status = rndisDevice->InitCheck();
	if (status < B_OK) {
		delete rndisDevice;
		free(info);
		return status;
	}

	info->device = rndisDevice;

	info->id = gDeviceKeeper->create_id(USB_RNDIS_DEVICE_ID_GENERATOR);
	if (info->id < 0) {
		status = info->id;
		delete rndisDevice;
		free(info);
		return status;
	}

	snprintf(info->publishedPath, sizeof(info->publishedPath),
		DEVICE_BASE_NAME "%" B_PRId32, info->id);
	status = gDeviceKeeper->publish_device(node, info->publishedPath,
		&sDeviceOps);
	if (status != B_OK) {
		gDeviceKeeper->free_id(USB_RNDIS_DEVICE_ID_GENERATOR, info->id);
		delete rndisDevice;
		free(info);
		return status;
	}

	*cookie = info;
	return B_OK;
}


static void
usb_rndis_detach(void *_cookie)
{
	TRACE("detach()\n");
	usb_rndis_driver_info* info = (usb_rndis_driver_info*)_cookie;
	if (info == NULL)
		return;

	if (info->publishedPath[0] != '\0')
		gDeviceKeeper->unpublish_device(info->node, info->publishedPath);
	if (info->id >= 0)
		gDeviceKeeper->free_id(USB_RNDIS_DEVICE_ID_GENERATOR, info->id);

	delete info->device;
	info->device = NULL;
	free(info);
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ B_USB_MODULE_NAME, (module_info**)&gUSBModule },
	{ NULL }
};

struct dk_driver_info sUsbRndisDriver = {
	.info = {
		USB_RNDIS_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.match   = &sUsbRndisMatchDict,
	.probe   = usb_rndis_probe,
	.attach  = usb_rndis_attach,
	.detach  = usb_rndis_detach,
	.ops     = &sDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sUsbRndisDriver,
	NULL
};
