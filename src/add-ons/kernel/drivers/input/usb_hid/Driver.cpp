/*
 * Copyright 2008-2011 Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT license.
 */


//!	Driver for USB Human Interface Devices.


#include "DeviceList.h"
#include "Driver.h"
#include "HIDDevice.h"
#include "ProtocolHandler.h"

#include <lock.h>
#include <util/AutoLock.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define USB_HID_DRIVER_MODULE_NAME "drivers/input/usb_hid/dk_driver_v1"


struct device_cookie {
	ProtocolHandler*	handler;
	uint32				cookie;
};


usb_module_info *gUSBModule = NULL;
dk_keeper_info *gDeviceKeeper = NULL;
DeviceList *gDeviceList = NULL;
static int32 sParentCookie = 0;
static mutex sDriverLock;


usb_support_descriptor gBlackListedDevices[] = {
	{
		// temperature sensor which declares itself as a usb hid device
		0, 0, 0, 0x0c45, 0x7401
	},
	{
		// Silicon Labs EC3 JTAG/C2 probe, declares itself as usb hid
		0, 0, 0, 0x10c4, 0x8044
	}
};

int32 gBlackListedDeviceCount
	= sizeof(gBlackListedDevices) / sizeof(gBlackListedDevices[0]);


typedef struct {
	dk_node*	node;
	usb_device		usb_dev;
	int32			parentCookie;
	bool			devicesFound;
} usb_hid_driver_info;


//	#pragma mark - module lifecycle


static status_t
usb_hid_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			gDeviceList = new(std::nothrow) DeviceList();
			if (gDeviceList == NULL)
				return B_NO_MEMORY;
			mutex_init(&sDriverLock, "usb hid driver lock");
			return B_OK;

		case B_MODULE_UNINIT:
			mutex_destroy(&sDriverLock);
			delete gDeviceList;
			gDeviceList = NULL;
			return B_OK;
	}

	return B_ERROR;
}


//	#pragma mark - device module API


static status_t
usb_hid_open(void *_info, const char *name, int openMode, void **_cookie)
{
	TRACE("open(%s, %d)\n", name, openMode);

	device_cookie *cookie = new(std::nothrow) device_cookie();
	if (cookie == NULL)
		return B_NO_MEMORY;

	MutexLocker locker(sDriverLock);

	ProtocolHandler *handler = (ProtocolHandler *)gDeviceList->FindDevice(name);
	TRACE("  name %s: handler %p\n", name, handler);

	cookie->handler = handler;
	cookie->cookie = 0;

	status_t result = handler == NULL ? B_ENTRY_NOT_FOUND : B_OK;
	if (result == B_OK)
		result = handler->Open(openMode, &cookie->cookie);

	if (result != B_OK) {
		delete cookie;
		return result;
	}

	*_cookie = cookie;
	return B_OK;
}


static status_t
usb_hid_read(void *_cookie, off_t position, void *buffer, size_t *numBytes)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("read(%p, %" B_PRIu64 ", %p, %p (%lu)\n", cookie, position, buffer,
		numBytes, numBytes != NULL ? *numBytes : 0);
	return cookie->handler->Read(&cookie->cookie, position, buffer, numBytes);
}


static status_t
usb_hid_write(void *_cookie, off_t position, const void *buffer,
	size_t *numBytes)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("write(%p, %" B_PRIu64 ", %p, %p (%lu)\n", cookie, position,
		buffer, numBytes, numBytes != NULL ? *numBytes : 0);
	return cookie->handler->Write(&cookie->cookie, position, buffer, numBytes);
}


static status_t
usb_hid_control(void *_cookie, uint32 op, void *buffer, size_t length)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("control(%p, %" B_PRIu32 ", %p, %" B_PRIuSIZE ")\n", cookie, op,
		buffer, length);
	return cookie->handler->Control(&cookie->cookie, op, buffer, length);
}


static status_t
usb_hid_close(void *_cookie)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("close(%p)\n", cookie);
	return cookie->handler->Close(&cookie->cookie);
}


static status_t
usb_hid_free(void *_cookie)
{
	device_cookie *cookie = (device_cookie *)_cookie;
	TRACE("free(%p)\n", cookie);

	mutex_lock(&sDriverLock);

	HIDDevice *device = cookie->handler->Device();
	if (device->IsOpen()) {
		// another handler of this device is still open so we can't free it
	} else if (device->IsRemoved()) {
		// the parent device is removed already and none of its handlers are
		// open anymore so we can free it here
		delete device;
	}

	mutex_unlock(&sDriverLock);

	delete cookie;
	return B_OK;
}


//	#pragma mark - driver module API


static float
usb_hid_supports_device(dk_node *parent)
{
	TRACE("supports_device()\n");
	char bus[64];

	if (gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1.0f;

	if (strcmp(bus, "usb") != 0)
		return -1.0f;

	// check for HID class
	uint8 deviceClass = 0;
	if (gDeviceKeeper->get_property_uint8(parent, KOSM_USB_DEVICE_CLASS,
			&deviceClass, false) == B_OK
		&& deviceClass == USB_INTERFACE_CLASS_HID) {
		TRACE("USB HID device found!\n");

		// check blacklist
		uint16 vendorId = 0, productId = 0;
		gDeviceKeeper->get_property_uint16(parent,
			KOSM_USB_VENDOR_ID, &vendorId, false);
		gDeviceKeeper->get_property_uint16(parent,
			KOSM_USB_PRODUCT_ID, &productId, false);
		for (int32 i = 0; i < gBlackListedDeviceCount; i++) {
			if ((gBlackListedDevices[i].vendor == 0
					|| vendorId == gBlackListedDevices[i].vendor)
				&& (gBlackListedDevices[i].product == 0
					|| productId
						== gBlackListedDevices[i].product)) {
				TRACE("blacklisted device 0x%04x:0x%04x\n",
					vendorId, productId);
				return -1.0f;
			}
		}

		return 0.6f;
	}

	return -1.0f;
}



static dk_device_ops sUsbHidDeviceOps = {
	usb_hid_open,
	usb_hid_close,
	usb_hid_free,
	usb_hid_read,
	usb_hid_write,
	NULL,	// io
	usb_hid_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


static status_t
usb_hid_init_driver(dk_node *node, void **cookie)
{
	TRACE("init_driver()\n");

	usb_hid_driver_info *info = (usb_hid_driver_info *)malloc(
		sizeof(usb_hid_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));
	info->node = node;

	// get USB device ID
	if (gDeviceKeeper->get_property_uint32(node, USB_DEVICE_ID_ITEM,
			&info->usb_dev, true) != B_OK) {
		free(info);
		return B_ERROR;
	}

	const usb_device_descriptor *deviceDescriptor
		= gUSBModule->get_device_descriptor(info->usb_dev);

	TRACE("vendor id: 0x%04x; product id: 0x%04x\n",
		deviceDescriptor->vendor_id, deviceDescriptor->product_id);

	// blacklist check
	for (int32 i = 0; i < gBlackListedDeviceCount; i++) {
		usb_support_descriptor &entry = gBlackListedDevices[i];
		if ((entry.vendor != 0
				&& deviceDescriptor->vendor_id != entry.vendor)
			|| (entry.product != 0
				&& deviceDescriptor->product_id != entry.product)) {
			continue;
		}
		free(info);
		return B_ERROR;
	}

	const usb_configuration_info *config
		= gUSBModule->get_nth_configuration(info->usb_dev,
			USB_DEFAULT_CONFIGURATION);
	if (config == NULL) {
		TRACE_ALWAYS("cannot get default configuration\n");
		free(info);
		return B_ERROR;
	}

	status_t result = gUSBModule->set_configuration(info->usb_dev, config);
	if (result != B_OK) {
		TRACE_ALWAYS("set_configuration() failed 0x%08" B_PRIx32 "\n",
			result);
		free(info);
		return result;
	}

	config = gUSBModule->get_configuration(info->usb_dev);
	if (config == NULL) {
		TRACE_ALWAYS("cannot get current configuration\n");
		free(info);
		return B_ERROR;
	}

	info->devicesFound = false;
	info->parentCookie = atomic_add(&sParentCookie, 1);

	for (size_t i = 0; i < config->interface_count; i++) {
		const usb_interface_info *interface = config->interface[i].active;
		if (interface == NULL || interface->descr == NULL)
			continue;

		uint8 interfaceClass = interface->descr->interface_class;
		TRACE("interface %" B_PRIuSIZE ": class: %u; subclass: %u; "
			"protocol: %u\n", i, interfaceClass,
			interface->descr->interface_subclass,
			interface->descr->interface_protocol);

		if (interfaceClass == USB_INTERFACE_CLASS_HID) {
			mutex_lock(&sDriverLock);
			HIDDevice *hidDevice
				= new(std::nothrow) HIDDevice(info->usb_dev, config, i);

			if (hidDevice != NULL && hidDevice->InitCheck() == B_OK) {
				hidDevice->SetParentCookie(info->parentCookie);

				for (uint32 j = 0;; j++) {
					ProtocolHandler *handler
						= hidDevice->ProtocolHandlerAt(j);
					if (handler == NULL)
						break;

					int32 index = 0;
					char pathBuffer[B_DEV_NAME_LENGTH];
					const char *basePath = handler->BasePath();
					while (true) {
						sprintf(pathBuffer, "%s%" B_PRId32, basePath,
							index++);
						if (gDeviceList->FindDevice(pathBuffer) == NULL) {
							handler->SetPublishPath(strdup(pathBuffer));
							break;
						}
					}

					gDeviceList->AddDevice(handler->PublishPath(), handler);
					info->devicesFound = true;
				}
			} else
				delete hidDevice;

			mutex_unlock(&sDriverLock);
		}
	}

	if (!info->devicesFound) {
		free(info);
		return B_ERROR;
	}

	// Publish all protocol handler device paths
	mutex_lock(&sDriverLock);
	for (int32 i = 0; i < gDeviceList->CountDevices(); i++) {
		ProtocolHandler *handler
			= (ProtocolHandler *)gDeviceList->DeviceAt(i);
		if (!handler)
			continue;

		HIDDevice *device = handler->Device();
		if (device->ParentCookie() != info->parentCookie)
			continue;

		const char *path = handler->PublishPath();
		if (path != NULL) {
			gDeviceKeeper->publish_device(node, path,
				&sUsbHidDeviceOps);
		}
	}
	mutex_unlock(&sDriverLock);

	*cookie = info;
	return B_OK;
}


static void
usb_hid_uninit_driver(void *_cookie)
{
	TRACE("uninit_driver()\n");
	usb_hid_driver_info *info = (usb_hid_driver_info *)_cookie;
	int32 parentCookie = info->parentCookie;

	mutex_lock(&sDriverLock);

	// For each HIDDevice owned by this attach instance: unpublish all its
	// devfs entries, remove all its handlers from gDeviceList, then either
	// delete the device or mark it Removed (if still open from userland).
	//
	// The outer loop restarts the scan after each device because
	// RemoveDevice shifts gDeviceList indices. ProtocolHandlerAt walks
	// HIDDevice's internal list (unaffected by gDeviceList mutations),
	// so inner iteration is stable.
	while (true) {
		HIDDevice *device = NULL;
		for (int32 i = 0; i < gDeviceList->CountDevices(); i++) {
			ProtocolHandler *handler
				= (ProtocolHandler *)gDeviceList->DeviceAt(i);
			if (handler == NULL)
				continue;
			if (handler->Device()->ParentCookie() == parentCookie) {
				device = handler->Device();
				break;
			}
		}
		if (device == NULL)
			break;

		for (uint32 j = 0;; j++) {
			ProtocolHandler *handler = device->ProtocolHandlerAt(j);
			if (handler == NULL)
				break;

			const char *publishPath = handler->PublishPath();
			if (publishPath != NULL) {
				gDeviceKeeper->unpublish_device(info->node, publishPath);
			}
			gDeviceList->RemoveDevice(NULL, handler);
		}

		if (device->IsOpen()) {
			// Still open from userland — mark removed to unblock pending I/O;
			// the device will be deleted in usb_hid_free when the last
			// handle is closed.
			device->Removed();
		} else {
			delete device;
		}
	}

	mutex_unlock(&sDriverLock);
	free(info);
}




//	#pragma mark -


static const dk_match_rule sUsbHidMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "usb"),
	DK_MATCH_END
};

static const dk_match_dict sUsbHidMatchDict = {
	sUsbHidMatchRules,
	0
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&gDeviceKeeper },
	{ B_USB_MODULE_NAME, (module_info **)&gUSBModule },
	{ NULL }
};

static dk_driver_info sUsbHidDriver = {
	.info = {
		USB_HID_DRIVER_MODULE_NAME,
		0,
		usb_hid_std_ops
	},
	.match   = &sUsbHidMatchDict,
	.probe   = usb_hid_supports_device,
	.attach  = usb_hid_init_driver,
	.detach  = usb_hid_uninit_driver,
	.ops     = &sUsbHidDeviceOps,
};

module_info *modules[] = {
	(module_info *)&sUsbHidDriver,
	NULL
};
