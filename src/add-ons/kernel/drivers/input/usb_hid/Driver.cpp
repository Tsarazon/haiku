/*
 * Copyright 2008-2011 Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT license.
 */


//!	Driver for USB Human Interface Devices.


#include "DeviceList.h"
#include "Driver.h"
#include "HIDDevice.h"
#include "ProtocolHandler.h"
#include "QuirkyDevices.h"

#include <lock.h>
#include <util/AutoLock.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define USB_HID_DRIVER_MODULE_NAME "drivers/input/usb_hid/driver_v1"
#define USB_HID_DEVICE_MODULE_NAME "drivers/input/usb_hid/device_v1"


struct device_cookie {
	ProtocolHandler*	handler;
	uint32				cookie;
};


usb_module_info *gUSBModule = NULL;
device_manager_info *gDeviceManager = NULL;
DeviceList *gDeviceList = NULL;
static int32 sParentCookie = 0;
static mutex sDriverLock;
static bool sInitialized = false;


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
	device_node*	node;
	usb_device		usb_dev;
	int32			parentCookie;
	bool			devicesFound;
} usb_hid_driver_info;


// Ensure global state is initialized once
static void
ensure_globals_initialized()
{
	if (sInitialized)
		return;

	gDeviceList = new(std::nothrow) DeviceList();
	mutex_init(&sDriverLock, "usb hid driver lock");
	sInitialized = true;
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
usb_hid_supports_device(device_node *parent)
{
	TRACE("supports_device()\n");
	const char *bus;

	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;

	if (strcmp(bus, "usb"))
		return 0.0;

	// check for HID class
	uint8 deviceClass = 0;
	device_attr *attr = NULL;
	while (gDeviceManager->get_next_attr(parent, &attr) == B_OK) {
		if (attr->type != B_UINT8_TYPE)
			continue;

		if (!strcmp(attr->name, USB_DEVICE_CLASS)) {
			deviceClass = attr->value.ui8;
			if (deviceClass == USB_INTERFACE_CLASS_HID) {
				TRACE("USB HID device found!\n");

				// check blacklist
				uint16 vendorId = 0, productId = 0;
				gDeviceManager->get_attr_uint16(parent,
					B_DEVICE_VENDOR_ID, &vendorId, false);
				gDeviceManager->get_attr_uint16(parent,
					B_DEVICE_ID, &productId, false);
				for (int32 i = 0; i < gBlackListedDeviceCount; i++) {
					if ((gBlackListedDevices[i].vendor == 0
							|| vendorId == gBlackListedDevices[i].vendor)
						&& (gBlackListedDevices[i].product == 0
							|| productId
								== gBlackListedDevices[i].product)) {
						TRACE("blacklisted device 0x%04x:0x%04x\n",
							vendorId, productId);
						return 0.0;
					}
				}

				return 0.6;
			}
		}
	}

	// also check for quirky devices by vendor/product ID
	uint16 vendorId = 0, productId = 0;
	if (gDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID,
			&vendorId, false) == B_OK
		&& gDeviceManager->get_attr_uint16(parent, B_DEVICE_ID,
			&productId, false) == B_OK) {
		for (int32 j = 0; j < gQuirkyDeviceCount; j++) {
			if ((gQuirkyDevices[j].vendor_id == 0
					|| vendorId == gQuirkyDevices[j].vendor_id)
				&& (gQuirkyDevices[j].product_id == 0
					|| productId == gQuirkyDevices[j].product_id)) {
				TRACE("quirky HID device found!\n");
				return 0.6;
			}
		}
	}

	return 0.0;
}


static status_t
usb_hid_register_device(device_node *node)
{
	TRACE("register_device()\n");

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "USB HID"} },
		{ NULL }
	};

	return gDeviceManager->register_node(node, USB_HID_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
usb_hid_init_driver(device_node *node, void **cookie)
{
	TRACE("init_driver()\n");

	ensure_globals_initialized();

	usb_hid_driver_info *info = (usb_hid_driver_info *)malloc(
		sizeof(usb_hid_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));
	info->node = node;

	// get USB device ID
	if (gDeviceManager->get_attr_uint32(node, USB_DEVICE_ID_ITEM,
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

		// check for quirky devices first
		int32 quirkyIndex = -1;
		for (int32 j = 0; j < gQuirkyDeviceCount; j++) {
			usb_hid_quirky_device &quirky = gQuirkyDevices[j];
			if ((quirky.vendor_id != 0
					&& deviceDescriptor->vendor_id != quirky.vendor_id)
				|| (quirky.product_id != 0
					&& deviceDescriptor->product_id != quirky.product_id)
				|| (quirky.device_class != 0
					&& interfaceClass != quirky.device_class)
				|| (quirky.device_subclass != 0
					&& interface->descr->interface_subclass
						!= quirky.device_subclass)
				|| (quirky.device_protocol != 0
					&& interface->descr->interface_protocol
						!= quirky.device_protocol)) {
				continue;
			}

			quirkyIndex = j;
			break;
		}

		if (quirkyIndex >= 0 || interfaceClass == USB_INTERFACE_CLASS_HID) {
			mutex_lock(&sDriverLock);
			HIDDevice *hidDevice
				= new(std::nothrow) HIDDevice(info->usb_dev, config, i,
					quirkyIndex);

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

	*cookie = info;
	return B_OK;
}


static void
usb_hid_uninit_driver(void *_cookie)
{
	TRACE("uninit_driver()\n");
	usb_hid_driver_info *info = (usb_hid_driver_info *)_cookie;

	mutex_lock(&sDriverLock);
	int32 parentCookie = info->parentCookie;

	for (int32 i = 0; i < gDeviceList->CountDevices(); i++) {
		ProtocolHandler *handler
			= (ProtocolHandler *)gDeviceList->DeviceAt(i);
		if (!handler)
			continue;

		HIDDevice *device = handler->Device();
		if (device->ParentCookie() != parentCookie)
			continue;

		for (uint32 j = 0;; j++) {
			handler = device->ProtocolHandlerAt(j);
			if (handler == NULL)
				break;

			gDeviceList->RemoveDevice(NULL, handler);
			i--;
		}

		if (device->IsOpen()) {
			device->Removed();
		} else
			delete device;
	}

	mutex_unlock(&sDriverLock);
	free(info);
}


static status_t
usb_hid_register_child_devices(void *_cookie)
{
	TRACE("register_child_devices()\n");
	usb_hid_driver_info *info = (usb_hid_driver_info *)_cookie;

	// publish all protocol handler device paths
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
			status_t status = gDeviceManager->publish_device(info->node,
				path, USB_HID_DEVICE_MODULE_NAME);
			if (status != B_OK) {
				TRACE_ALWAYS("publish_device(%s) failed: %" B_PRIx32 "\n",
					path, status);
			}
		}
	}
	mutex_unlock(&sDriverLock);

	return B_OK;
}


static void
usb_hid_driver_device_removed(void *_cookie)
{
	TRACE("driver_device_removed()\n");
	// handled in uninit_driver
}


//	#pragma mark - device init/uninit


static status_t
usb_hid_init_device(void *_info, void **_cookie)
{
	*_cookie = _info;
	return B_OK;
}


static void
usb_hid_uninit_device(void *_cookie)
{
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&gDeviceManager },
	{ B_USB_MODULE_NAME, (module_info **)&gUSBModule },
	{ NULL }
};

struct device_module_info sUsbHidDevice = {
	{
		USB_HID_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	usb_hid_init_device,
	usb_hid_uninit_device,
	NULL,	// device_removed

	usb_hid_open,
	usb_hid_close,
	usb_hid_free,
	usb_hid_read,
	usb_hid_write,
	NULL,	// io
	usb_hid_control,

	NULL,	// select
	NULL,	// deselect
};

struct driver_module_info sUsbHidDriver = {
	{
		USB_HID_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	usb_hid_supports_device,
	usb_hid_register_device,
	usb_hid_init_driver,
	usb_hid_uninit_driver,
	usb_hid_register_child_devices,
	NULL,	// rescan
	usb_hid_driver_device_removed,
};

module_info *modules[] = {
	(module_info *)&sUsbHidDriver,
	(module_info *)&sUsbHidDevice,
	NULL
};
