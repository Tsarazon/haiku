/*
	Driver for USB Ethernet Control Model devices
	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
	Distributed under the terms of the MIT license.
*/
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lock.h>

#include <bus/USB.h>


#include "Driver.h"
#include "ECMDevice.h"


#define DEVICE_BASE_NAME "net/usb_ecm/"
usb_module_info *gUSBModule = NULL;
dk_keeper_info *gDeviceKeeper;


#define USB_ECM_DRIVER_MODULE_NAME "drivers/network/usb_ecm/dk_driver_v1"
#define USB_ECM_DEVICE_ID_GENERATOR	"usb_ecm/device_id"


//	#pragma mark - device ops


static void
usb_ecm_device_removed(void* _cookie)
{
	CALLED();
	usb_ecm_driver_info* info = (usb_ecm_driver_info*)_cookie;
	info->device->Removed();
}


static status_t
usb_ecm_open(void* _info, const char* path, int openMode, void** _cookie)
{
	CALLED();
	usb_ecm_driver_info* info = (usb_ecm_driver_info*)_info;

	status_t status = info->device->Open();
	if (status != B_OK)
		return status;

	*_cookie = info->device;
	return B_OK;
}


static status_t
usb_ecm_read(void *cookie, off_t position, void *buffer, size_t *numBytes)
{
	TRACE("read(%p, %" B_PRIdOFF", %p, %lu)\n", cookie, position, buffer, *numBytes);
	ECMDevice *device = (ECMDevice *)cookie;
	return device->Read((uint8 *)buffer, numBytes);
}


static status_t
usb_ecm_write(void *cookie, off_t position, const void *buffer,
	size_t *numBytes)
{
	TRACE("write(%p, %" B_PRIdOFF", %p, %lu)\n", cookie, position, buffer, *numBytes);
	ECMDevice *device = (ECMDevice *)cookie;
	return device->Write((const uint8 *)buffer, numBytes);
}


static status_t
usb_ecm_control(void *cookie, uint32 op, void *buffer, size_t length)
{
	TRACE("control(%p, %" B_PRIu32 ", %p, %lu)\n", cookie, op, buffer, length);
	ECMDevice *device = (ECMDevice *)cookie;
	return device->Control(op, buffer, length);
}


static status_t
usb_ecm_close(void *cookie)
{
	TRACE("close(%p)\n", cookie);
	ECMDevice *device = (ECMDevice *)cookie;
	return device->Close();
}


static status_t
usb_ecm_free(void *cookie)
{
	TRACE("free(%p)\n", cookie);
	ECMDevice *device = (ECMDevice *)cookie;
	return device->Free();
}


static dk_device_ops sUsbEcmDeviceOps = {
	usb_ecm_open,
	usb_ecm_close,
	usb_ecm_free,
	usb_ecm_read,
	usb_ecm_write,
	NULL,	// io
	usb_ecm_control,
	NULL,	// select
	NULL,	// deselect
	usb_ecm_device_removed,
};


//	#pragma mark - driver module API


static float
usb_ecm_supports_device(dk_node *parent)
{
	CALLED();
	char bus[64];

	if (gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1;

	if (strcmp(bus, "usb"))
		return 0.0;

	// check whether it's really an ECM device
	dk_property *attr = NULL;
	uint8 baseClass = 0, subclass = 0;
	uint64 version = 0;
	while (true) {
		status_t s = gDeviceKeeper->get_next_property(parent, &attr, &version);
		if (s == B_INTERRUPTED) {
			// store mutated mid-iteration — restart from scratch
			attr = NULL;
			baseClass = subclass = 0;
			continue;
		}
		if (s != B_OK)
			break;
		if (attr->type != B_UINT8_TYPE)
			continue;

		if (!strcmp(attr->name, USB_DEVICE_CLASS))
			baseClass = attr->value.ui8;
		if (!strcmp(attr->name, USB_DEVICE_SUBCLASS))
			subclass = attr->value.ui8;
		if (baseClass != 0 && subclass != 0) {
			if (baseClass == USB_INTERFACE_CLASS_CDC && subclass == USB_INTERFACE_SUBCLASS_ECM)
				break;
			baseClass = subclass = 0;
		}
	}

	if (baseClass != USB_INTERFACE_CLASS_CDC || subclass != USB_INTERFACE_SUBCLASS_ECM)
		return 0.0;

	TRACE("USB-ECM device found!\n");

	return 0.6;
}


static status_t
usb_ecm_attach(dk_node *node, void **cookie)
{
	CALLED();

	usb_ecm_driver_info* info = (usb_ecm_driver_info*)malloc(
		sizeof(usb_ecm_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));
	info->node = node;
	info->id = -1;

	// USB peripheral drivers consume the gUSBModule singleton (kernel
	// module dependency) and address the device via the USB_DEVICE_ID_ITEM
	// property set by the USB bus manager on the parent node. There is no
	// typed per-device bus interface to retrieve via get_interface().
	usb_device device;
	if (gDeviceKeeper->get_property_uint32(node, USB_DEVICE_ID_ITEM,
			&device, true) != B_OK) {
		free(info);
		return B_ERROR;
	}

	ECMDevice *ecmDevice = new(std::nothrow) ECMDevice(device);
	if (ecmDevice == NULL) {
		free(info);
		return B_NO_MEMORY;
	}

	status_t status = ecmDevice->InitCheck();
	if (status < B_OK) {
		delete ecmDevice;
		free(info);
		return status;
	}

	info->device = ecmDevice;

	info->id = gDeviceKeeper->create_id(USB_ECM_DEVICE_ID_GENERATOR);
	if (info->id < 0) {
		status = info->id;
		delete ecmDevice;
		free(info);
		return status;
	}

	snprintf(info->publishedPath, sizeof(info->publishedPath),
		DEVICE_BASE_NAME "%" B_PRId32, info->id);
	status = gDeviceKeeper->publish_device(node, info->publishedPath,
		&sUsbEcmDeviceOps);
	if (status != B_OK) {
		gDeviceKeeper->free_id(USB_ECM_DEVICE_ID_GENERATOR, info->id);
		delete ecmDevice;
		free(info);
		return status;
	}

	*cookie = info;
	return B_OK;
}


static void
usb_ecm_detach(void *_cookie)
{
	CALLED();
	usb_ecm_driver_info* info = (usb_ecm_driver_info*)_cookie;
	if (info == NULL)
		return;

	if (info->publishedPath[0] != '\0')
		gDeviceKeeper->unpublish_device(info->node, info->publishedPath);
	if (info->id >= 0)
		gDeviceKeeper->free_id(USB_ECM_DEVICE_ID_GENERATOR, info->id);

	delete info->device;
	free(info);
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ B_USB_MODULE_NAME, (module_info**)&gUSBModule},
	{ NULL }
};

static const dk_match_rule sUsbEcmMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "usb"),
	DK_MATCH_END
};

static const dk_match_dict sUsbEcmMatchDict = {
	sUsbEcmMatchRules,
	0
};


static dk_driver_info sUsbEcmDriver = {
	.info = {
		USB_ECM_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.match   = &sUsbEcmMatchDict,
	.probe   = usb_ecm_supports_device,
	.attach  = usb_ecm_attach,
	.detach  = usb_ecm_detach,
	.ops     = &sUsbEcmDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sUsbEcmDriver,
	NULL
};
