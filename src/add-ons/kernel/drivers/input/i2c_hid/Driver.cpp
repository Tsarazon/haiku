/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2008-2011 Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT license.
 */


//!	Driver for I2C Human Interface Devices.


#include <ACPI.h>
#include <device_keeper.h>
#include <i2c.h>

#include "DeviceList.h"
#include "Driver.h"
#include "HIDDevice.h"
#include "ProtocolHandler.h"

#include <lock.h>
#include <util/AutoLock.h>

#include <new>
#include <stdio.h>
#include <string.h>



struct hid_driver_cookie;
static status_t i2c_hid_publish_devices(hid_driver_cookie *device);


struct hid_driver_cookie {
	dk_node*				node;
	i2c_device_interface*	i2c;
	i2c_device				i2c_cookie;
	uint32					descriptorAddress;
	HIDDevice*				hidDevice;
};

struct device_cookie {
	ProtocolHandler*	handler;
	uint32				cookie;
	hid_driver_cookie*	driver_cookie;
};


#define I2C_HID_DRIVER_NAME "drivers/input/i2c_hid/dk_driver_v1"

/* Base Namespace devices are published to */
#define I2C_HID_BASENAME "input/i2c_hid/%d"

// name of pnp generator of path ids
#define I2C_HID_PATHID_GENERATOR "i2c_hid/path_id"

#define ACPI_NAME_HID_DEVICE "PNP0C50"

static dk_keeper_info *sDeviceKeeper;
static acpi_module_info* gACPI;

DeviceList *gDeviceList = NULL;
static mutex sDriverLock;


static acpi_object_type*
acpi_evaluate_dsm(acpi_handle handle, const uint8 *guid, uint64 revision, uint64 function)
{
	acpi_data buffer;
	buffer.pointer = NULL;
	buffer.length = ACPI_ALLOCATE_BUFFER;

	acpi_object_type array[4];
	acpi_objects acpi_objects;
	acpi_objects.count = 4;
	acpi_objects.pointer = array;

	array[0].object_type = ACPI_TYPE_BUFFER;
	array[0].buffer.buffer = (void*)guid;
	array[0].buffer.length = 16;

	array[1].object_type = ACPI_TYPE_INTEGER;
	array[1].integer.integer = revision;

	array[2].object_type = ACPI_TYPE_INTEGER;
	array[2].integer.integer = function;

	array[3].object_type = ACPI_TYPE_PACKAGE;
	array[3].package.objects = NULL;
	array[3].package.count = 0;

	if (gACPI->evaluate_method(handle, "_DSM", &acpi_objects, &buffer) == B_OK)
		return (acpi_object_type*)buffer.pointer;
	return NULL;
}


// #pragma mark - notify hooks


/*
status_t
i2c_hid_device_removed(void *cookie)
{
	mutex_lock(&sDriverLock);
	int32 parentCookie = (int32)(addr_t)cookie;
	TRACE("device_removed(%" B_PRId32 ")\n", parentCookie);

	for (int32 i = 0; i < gDeviceList->CountDevices(); i++) {
		ProtocolHandler *handler = (ProtocolHandler *)gDeviceList->DeviceAt(i);
		if (!handler)
			continue;

		HIDDevice *device = handler->Device();
		if (device->ParentCookie() != parentCookie)
			continue;

		// remove all the handlers
		for (uint32 i = 0;; i++) {
			handler = device->ProtocolHandlerAt(i);
			if (handler == NULL)
				break;

			gDeviceList->RemoveDevice(NULL, handler);
		}

		// this handler's device belongs to the one removed
		if (device->IsOpen()) {
			// the device and it's handlers will be deleted in the free hook
			device->Removed();
		} else
			delete device;

		break;
	}

	mutex_unlock(&sDriverLock);
	return B_OK;
}*/


// #pragma mark - driver hooks


static status_t
i2c_hid_open(void *initCookie, const char *path, int flags, void **_cookie)
{
	TRACE("open(%s, %" B_PRIu32 ", %p)\n", path, flags, _cookie);

	device_cookie *cookie = new(std::nothrow) device_cookie();
	if (cookie == NULL)
		return B_NO_MEMORY;
	cookie->driver_cookie = (hid_driver_cookie*)initCookie;

	MutexLocker locker(sDriverLock);

	ProtocolHandler *handler = (ProtocolHandler *)gDeviceList->FindDevice(path);
	TRACE("  path %s: handler %p\n", path, handler);

	cookie->handler = handler;
	cookie->cookie = 0;

	status_t result = handler == NULL ? B_ENTRY_NOT_FOUND : B_OK;
	if (result == B_OK)
		result = handler->Open(flags, &cookie->cookie);

	if (result != B_OK) {
		delete cookie;
		return result;
	}

	*_cookie = cookie;

	return B_OK;
}


static status_t
i2c_hid_read(void *_cookie, off_t position, void *buffer, size_t *numBytes)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("read(%p, %" B_PRIu64 ", %p, %p (%" B_PRIuSIZE ")\n", cookie, position, buffer, numBytes,
		numBytes != NULL ? *numBytes : 0);
	return cookie->handler->Read(&cookie->cookie, position, buffer, numBytes);
}


static status_t
i2c_hid_write(void *_cookie, off_t position, const void *buffer,
	size_t *numBytes)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("write(%p, %" B_PRIu64 ", %p, %p (%" B_PRIuSIZE ")\n", cookie, position, buffer, numBytes,
		numBytes != NULL ? *numBytes : 0);
	return cookie->handler->Write(&cookie->cookie, position, buffer, numBytes);
}


static status_t
i2c_hid_control(void *_cookie, uint32 op, void *buffer, size_t length)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("control(%p, %" B_PRIu32 ", %p, %" B_PRIuSIZE ")\n", cookie, op, buffer, length);
	return cookie->handler->Control(&cookie->cookie, op, buffer, length);
}


static status_t
i2c_hid_close(void *_cookie)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("close(%p)\n", cookie);
	return cookie->handler->Close(&cookie->cookie);
}


static status_t
i2c_hid_free(void *_cookie)
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
i2c_hid_support(dk_node *parent)
{
	CALLED();

	// make sure parent is really the I2C bus manager
	char bus[64];
	if (sDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1.0f;

	if (strcmp(bus, "i2c") != 0)
		return -1.0f;
	TRACE("i2c_hid_support found an i2c device %p\n", parent);

	// check whether it's an HID device via ACPI
	char name[64];
	if (sDeviceKeeper->get_property_string(parent, KOSM_ACPI_DEVICE_HID, name,
		sizeof(name), NULL, false) == B_OK && strcmp(name, ACPI_NAME_HID_DEVICE) == 0) {
		TRACE("i2c_hid_support found an hid i2c device\n");
		return 0.6f;
	}

	if (sDeviceKeeper->get_property_string(parent, KOSM_ACPI_DEVICE_CID, name,
		sizeof(name), NULL, false) == B_OK && strcmp(name, ACPI_NAME_HID_DEVICE) == 0) {
		TRACE("i2c_hid_support found a compatible hid i2c device\n");
		return 0.6f;
	}

	TRACE("i2c_hid_support found a non hid i2c device\n");

	return -1.0f;
}


static dk_device_ops sDeviceOps = {
	i2c_hid_open,
	i2c_hid_close,
	i2c_hid_free,
	i2c_hid_read,
	i2c_hid_write,
	NULL,	// io
	i2c_hid_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


static status_t
i2c_hid_attach(dk_node *node, void **driverCookie)
{
	CALLED();

	// Evaluate ACPI _DSM to get descriptor address
	acpi_handle handle;
	char acpiPath[256];
	if (sDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_PATH,
		acpiPath, sizeof(acpiPath), NULL, false) != B_OK
		|| gACPI->get_handle(NULL, acpiPath, &handle) != B_OK) {
		return B_DEVICE_NOT_FOUND;
	}

	static uint8_t acpiHidGuid[] = { 0xF7, 0xF6, 0xDF, 0x3C, 0x67, 0x42, 0x55,
		0x45, 0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE };
	acpi_object_type* object = acpi_evaluate_dsm(handle, acpiHidGuid, 1, 1);
	if (object == NULL)
		return B_DEVICE_NOT_FOUND;
	if (object->object_type != ACPI_TYPE_INTEGER) {
		free(object);
		return B_DEVICE_NOT_FOUND;
	}

	uint32 descriptorAddress = object->integer.integer;
	free(object);

	hid_driver_cookie *device
		= (hid_driver_cookie *)calloc(1, sizeof(hid_driver_cookie));
	if (device == NULL)
		return B_NO_MEMORY;

	*driverCookie = device;

	device->node = node;
	device->descriptorAddress = descriptorAddress;

	status_t i2cStatus = sDeviceKeeper->get_interface(node,
		I2C_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void **)&device->i2c, (void **)&device->i2c_cookie);
	if (i2cStatus != B_OK) {
		ERROR("get_interface(i2c device) failed: %s\n", strerror(i2cStatus));
		free(device);
		*driverCookie = NULL;
		return i2cStatus;
	}

	mutex_lock(&sDriverLock);
	HIDDevice *hidDevice
		= new(std::nothrow) HIDDevice(descriptorAddress, device->i2c,
			device->i2c_cookie);

	if (hidDevice != NULL && hidDevice->InitCheck() == B_OK) {
		device->hidDevice = hidDevice;
	} else
		delete hidDevice;

	mutex_unlock(&sDriverLock);

	if (device->hidDevice == NULL)
		return B_IO_ERROR;

	// Publish child devices in devfs
	i2c_hid_publish_devices(device);

	return B_OK;
}


static void
i2c_hid_detach(void *driverCookie)
{
	CALLED();
	hid_driver_cookie *device = (hid_driver_cookie*)driverCookie;
	if (device == NULL)
		return;

	HIDDevice *hidDevice = device->hidDevice;

	mutex_lock(&sDriverLock);

	if (hidDevice != NULL) {
		// Unpublish all devfs entries and remove handlers from gDeviceList
		// before destroying the HIDDevice. ProtocolHandlerAt walks
		// HIDDevice's internal list, which is stable across
		// gDeviceList->RemoveDevice calls.
		for (uint32 i = 0;; i++) {
			ProtocolHandler *handler = hidDevice->ProtocolHandlerAt(i);
			if (handler == NULL)
				break;

			const char *publishPath = handler->PublishPath();
			if (publishPath != NULL)
				sDeviceKeeper->unpublish_device(device->node, publishPath);
			gDeviceList->RemoveDevice(NULL, handler);
		}

		if (hidDevice->IsOpen()) {
			// Still open from userland — mark removed to unblock pending I/O;
			// the device will be deleted in i2c_hid_free when the last
			// handle is closed.
			hidDevice->Removed();
		} else {
			delete hidDevice;
		}
	}

	mutex_unlock(&sDriverLock);

	free(device);
}


static status_t
i2c_hid_publish_devices(hid_driver_cookie *device)
{
	HIDDevice* hidDevice = device->hidDevice;
	if (hidDevice == NULL)
		return B_OK;
	for (uint32 i = 0;; i++) {
		ProtocolHandler *handler = hidDevice->ProtocolHandlerAt(i);
		if (handler == NULL)
			break;

		// As devices can be un- and replugged at will, we cannot
		// simply rely on a device count. If there is just one
		// keyboard, this does not mean that it uses the 0 name.
		// There might have been two keyboards and the one using 0
		// might have been unplugged. So we just generate names
		// until we find one that is not currently in use.
		int32 index = 0;
		char pathBuffer[B_DEV_NAME_LENGTH];
		const char *basePath = handler->BasePath();
		while (true) {
			sprintf(pathBuffer, "%s%" B_PRId32, basePath, index++);
			if (gDeviceList->FindDevice(pathBuffer) == NULL) {
				// this name is still free, use it
				handler->SetPublishPath(strdup(pathBuffer));
				break;
			}
		}

		gDeviceList->AddDevice(handler->PublishPath(), handler);

		sDeviceKeeper->publish_device(device->node, pathBuffer,
			&sDeviceOps);
	}

	return B_OK;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			gDeviceList = new(std::nothrow) DeviceList();
			if (gDeviceList == NULL) {
				return B_NO_MEMORY;
			}
			mutex_init(&sDriverLock, "i2c hid driver lock");

			return B_OK;
		case B_MODULE_UNINIT:
			delete gDeviceList;
			gDeviceList = NULL;
			mutex_destroy(&sDriverLock);
			return B_OK;

		default:
			break;
	}

	return B_ERROR;
}


//	#pragma mark -


static const dk_match_rule sI2cHidMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "i2c"),
	DK_MATCH_END
};

static const dk_match_dict sI2cHidMatchDict = {
	sI2cHidMatchRules,
	0
};


static dk_driver_info i2c_hid_driver_module = {
	.info = {
		I2C_HID_DRIVER_NAME,
		0,
		&std_ops
	},
	.match	= &sI2cHidMatchDict,
	.probe	= i2c_hid_support,
	.attach	= i2c_hid_attach,
	.detach	= i2c_hid_detach,
	.ops	= &sDeviceOps,
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&sDeviceKeeper },
	{ B_ACPI_MODULE_NAME, (module_info**)&gACPI },
	{}
};


module_info *modules[] = {
	(module_info *)&i2c_hid_driver_module,
	NULL
};
