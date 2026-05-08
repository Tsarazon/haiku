/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2008-2011 Michael Lotz <mmlr@mlotz.ch>
 * Copyright 2020, 2022 Vladimir Kondratyev <wulf@FreeBSD.org>
 * Copyright 2023 Vladimir Serbinenko <phcoder@gmail.com>
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT license.
 */


//!	Driver for I2C Elan Devices.


#include <ACPI.h>
#include <device_keeper.h>
#include <i2c.h>

#include "DeviceList.h"
#include "Driver.h"
#include "ELANDevice.h"

#include <lock.h>
#include <util/AutoLock.h>

#include <new>
#include <stdio.h>
#include <string.h>


struct elan_driver_cookie {
	ELANDevice*				elanDevice;
};

#define I2C_ELAN_DRIVER_NAME "drivers/input/i2c_elan/dk_driver_v1"
#define I2C_ELAN_BASENAME "input/i2c_elan/%d"

static const char* elan_iic_devs[] = {
	"ELAN0000",
	"ELAN0100",
	"ELAN0600",
	"ELAN0601",
	"ELAN0602",
	"ELAN0603",
	"ELAN0604",
	"ELAN0605",
	"ELAN0606",
	"ELAN0607",
	"ELAN0608",
	"ELAN0609",
	"ELAN060B",
	"ELAN060C",
	"ELAN060F",
	"ELAN0610",
	"ELAN0611",
	"ELAN0612",
	"ELAN0615",
	"ELAN0616",
	"ELAN0617",
	"ELAN0618",
	"ELAN0619",
	"ELAN061A",
	"ELAN061B",
	"ELAN061C",
	"ELAN061D",
	"ELAN061E",
	"ELAN061F",
	"ELAN0620",
	"ELAN0621",
	"ELAN0622",
	"ELAN0623",
	"ELAN0624",
	"ELAN0625",
	"ELAN0626",
	"ELAN0627",
	"ELAN0628",
	"ELAN0629",
	"ELAN062A",
	"ELAN062B",
	"ELAN062C",
	"ELAN062D",
	"ELAN062E",	/* Lenovo V340 Whiskey Lake U */
	"ELAN062F",	/* Lenovo V340 Comet Lake U */
	"ELAN0631",
	"ELAN0632",
	"ELAN0633",	/* Lenovo S145 */
	"ELAN0634",	/* Lenovo V340 Ice lake */
	"ELAN0635",	/* Lenovo V1415-IIL */
	"ELAN0636",	/* Lenovo V1415-Dali */
	"ELAN0637",	/* Lenovo V1415-IGLR */
	"ELAN1000"
};

static dk_keeper_info* sDeviceKeeper;

DeviceList* gDeviceList = NULL;
static mutex sDriverLock;


// #pragma mark - driver hooks


static status_t
i2c_elan_open(void* initCookie, const char* path, int flags, void** _cookie)
{
	TRACE("open(%s, %" B_PRIu32 ", %p)\n", path, flags, _cookie);

	elan_driver_cookie* cookie = new(std::nothrow) elan_driver_cookie();
	if (cookie == NULL)
		return B_NO_MEMORY;

	MutexLocker locker(sDriverLock);

	ELANDevice* elan = (ELANDevice*)gDeviceList->FindDevice(path);
	TRACE("  path %s: handler %p (elan)\n", path, elan);

	cookie->elanDevice = elan;

	status_t result = elan == NULL ? B_ENTRY_NOT_FOUND : B_OK;
	if (result == B_OK)
		result = elan->Open(flags);

	if (result != B_OK) {
		delete cookie;
		return result;
	}

	*_cookie = cookie;
	return B_OK;
}


static status_t
i2c_elan_read(void* _cookie, off_t position, void* buffer, size_t* numBytes)
{
	TRACE_ALWAYS("unhandled read on i2c_elan\n");
	*numBytes = 0;
	return B_ERROR;
}


static status_t
i2c_elan_write(void* _cookie, off_t position, const void* buffer,
	size_t* numBytes)
{
	TRACE_ALWAYS("unhandled write on i2c_elan\n");
	*numBytes = 0;
	return B_ERROR;
}


static status_t
i2c_elan_control(void* _cookie, uint32 op, void* buffer, size_t length)
{
	elan_driver_cookie* cookie = (elan_driver_cookie*)_cookie;
	TRACE("control(%p, %" B_PRIu32 ", %p, %" B_PRIuSIZE ")\n",
		cookie, op, buffer, length);
	return cookie->elanDevice->Control(op, buffer, length);
}


static status_t
i2c_elan_close(void* _cookie)
{
	elan_driver_cookie* cookie = (elan_driver_cookie*)_cookie;
	TRACE("close(%p)\n", cookie);
	return cookie->elanDevice->Close();
}


static status_t
i2c_elan_free(void* _cookie)
{
	elan_driver_cookie* cookie = (elan_driver_cookie*)_cookie;
	TRACE("free(%p)\n", cookie);

	mutex_lock(&sDriverLock);

	ELANDevice* device = cookie->elanDevice;
	if (device->IsOpen()) {
		// another handler of this device is still open
	} else if (device->IsRemoved()) {
		// parent device removed, no handlers open — free it
		delete device;
	}

	mutex_unlock(&sDriverLock);

	delete cookie;
	return B_OK;
}


static dk_device_ops sDeviceOps = {
	i2c_elan_open,
	i2c_elan_close,
	i2c_elan_free,
	i2c_elan_read,
	i2c_elan_write,
	NULL,		// io
	i2c_elan_control,
	NULL,		// select
	NULL,		// deselect
	NULL		// device_removed
};


//	#pragma mark - match & driver


static bool
is_elan_name(const char* name)
{
	if (name == NULL)
		return false;
	for (unsigned i = 0; i < B_COUNT_OF(elan_iic_devs); i++) {
		if (strcmp(name, elan_iic_devs[i]) == 0)
			return true;
	}
	return false;
}


// Match dict narrows to I2C bus. Probe checks ACPI HID/CID for ELAN names.
static const dk_match_rule sI2cElanMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "i2c" } },
	{}
};

static const dk_match_dict sI2cElanMatchDict = {
	sI2cElanMatchRules, 0
};


static float
i2c_elan_support(dk_node* parent)
{
	CALLED();

	TRACE("i2c_elan_support found an i2c device %p\n", parent);

	// Check that device has ACPI backing (HID property exists)
	char name[64];
	if (sDeviceKeeper->get_property_string(parent, KOSM_ACPI_DEVICE_HID,
		name, sizeof(name), NULL, false) == B_OK && is_elan_name(name)) {
		TRACE("i2c_elan_support found an elan i2c device\n");
		return 0.6f;
	}

	// Also check compatible IDs (CID)
	if (sDeviceKeeper->get_property_string(parent, KOSM_ACPI_DEVICE_CID,
		name, sizeof(name), NULL, false) == B_OK && is_elan_name(name)) {
		TRACE("i2c_elan_support found a compatible elan i2c device\n");
		return 0.6f;
	}

	return 0.0f;
}


static status_t
i2c_elan_attach(dk_node* node, void** _driverCookie)
{
	CALLED();

	elan_driver_cookie* device
		= (elan_driver_cookie*)calloc(1, sizeof(elan_driver_cookie));
	if (device == NULL)
		return B_NO_MEMORY;

	// Auto walk-up finds the I2CDevice driver attached to the ancestor
	// i2c device node; its cookie is an I2CDevice* which the callbacks
	// in I2CDevice.cpp route through to the bus manager.
	i2c_device_interface* i2c;
	void* i2c_cookie;
	status_t status = sDeviceKeeper->get_interface(node,
		I2C_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&i2c, &i2c_cookie);
	if (status != B_OK) {
		ERROR("failed to get i2c device interface: %s\n", strerror(status));
		free(device);
		return status;
	}

	mutex_lock(&sDriverLock);
	ELANDevice* elanDevice = new(std::nothrow) ELANDevice(i2c, i2c_cookie);

	if (elanDevice != NULL && elanDevice->InitCheck() == B_OK) {
		device->elanDevice = elanDevice;
	} else {
		delete elanDevice;
	}

	mutex_unlock(&sDriverLock);

	if (device->elanDevice == NULL) {
		free(device);
		return B_IO_ERROR;
	}

	// Publish device in devfs
	{
		int32 index = 0;
		char pathBuffer[B_DEV_NAME_LENGTH];
		while (true) {
			sprintf(pathBuffer, "input/mouse/" DEVICE_PATH_SUFFIX
				"/%" B_PRId32, index++);
			if (gDeviceList->FindDevice(pathBuffer) == NULL) {
				elanDevice->SetPublishPath(strdup(pathBuffer));
				break;
			}
		}

		gDeviceList->AddDevice(elanDevice->PublishPath(), elanDevice);

		// Publish on own node, not parent — DeviceKeeper tracks
		// published paths per-node for lifecycle management.
		sDeviceKeeper->publish_device(node, pathBuffer, &sDeviceOps);
	}

	*_driverCookie = device;
	return B_OK;
}


static void
i2c_elan_detach(void* driverCookie)
{
	CALLED();
	elan_driver_cookie* device = (elan_driver_cookie*)driverCookie;

	mutex_lock(&sDriverLock);
	if (device->elanDevice != NULL) {
		const char* publishPath = device->elanDevice->PublishPath();
		if (publishPath != NULL)
			gDeviceList->RemoveDevice(publishPath);
		delete device->elanDevice;
		device->elanDevice = NULL;
	}
	mutex_unlock(&sDriverLock);

	free(device);
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		{
			status_t s = get_module(KOSM_DEVICE_KEEPER_MODULE_NAME,
				(module_info**)&sDeviceKeeper);
			if (s != B_OK)
				return s;

			gDeviceList = new(std::nothrow) DeviceList();
			if (gDeviceList == NULL) {
				put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
				return B_NO_MEMORY;
			}
			mutex_init(&sDriverLock, "i2c elan driver lock");
			return B_OK;
		}
		case B_MODULE_UNINIT:
			delete gDeviceList;
			gDeviceList = NULL;
			mutex_destroy(&sDriverLock);
			put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
			return B_OK;
		default:
			return B_ERROR;
	}
}


//	#pragma mark -


static dk_driver_info i2c_elan_driver_module = {
	.info	= { I2C_ELAN_DRIVER_NAME, 0, &std_ops },
	.match	= &sI2cElanMatchDict,
	.probe	= i2c_elan_support,
	.attach	= i2c_elan_attach,
	.detach	= i2c_elan_detach,
	.ops	= &sDeviceOps,
};


module_info* modules[] = {
	(module_info*)&i2c_elan_driver_module,
	NULL
};
