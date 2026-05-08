/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT license.
 */


#include <ACPI.h>
#include <condition_variable.h>
#include <device_keeper.h>
#include <Drivers.h>
#include <Errors.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kernel.h>


extern "C" {
#	include "acpi.h"
}


struct als_driver_cookie {
	acpi_device_module_info*	acpi;
	acpi_device					acpi_cookie;
};


struct als_device_cookie {
	als_driver_cookie*		driver_cookie;
	int32					stop_watching;
};


#define ACPI_ALS_DRIVER_NAME "drivers/sensor/acpi_als/dk_driver_v1"
#define ACPI_ALS_BASENAME "sensor/acpi_als/%d"
#define ACPI_ALS_PATHID_GENERATOR "acpi_als/path_id"
#define ACPI_NAME_ALS "ACPI0008"

#define TRACE_ALS
#ifdef TRACE_ALS
#	define TRACE(x...) dprintf("acpi_als: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("acpi_als: " x)


static dk_keeper_info* sDeviceKeeper;
static ConditionVariable sALSCondition;


static status_t
acpi_GetInteger(als_driver_cookie* device, const char* path, uint64* number)
{
	acpi_data buf;
	acpi_object_type object;
	buf.pointer = &object;
	buf.length = sizeof(acpi_object_type);

	status_t status = device->acpi->evaluate_method(device->acpi_cookie,
		path, NULL, &buf);
	if (status == B_OK) {
		if (object.object_type == ACPI_TYPE_INTEGER)
			*number = object.integer.integer;
		else
			status = B_BAD_VALUE;
	}
	return status;
}


void
als_notify_handler(acpi_handle device, uint32 value, void* context)
{
	TRACE("als_notify_handler event 0x%" B_PRIx32 "\n", value);
	sALSCondition.NotifyAll();
}


//	#pragma mark - device ops


static status_t
acpi_als_open(void* initCookie, const char* path, int flags, void** cookie)
{
	als_device_cookie* device;
	device = (als_device_cookie*)calloc(1, sizeof(als_device_cookie));
	if (device == NULL)
		return B_NO_MEMORY;

	device->driver_cookie = (als_driver_cookie*)initCookie;
	device->stop_watching = 0;

	*cookie = device;
	return B_OK;
}


static status_t
acpi_als_close(void* cookie)
{
	return B_OK;
}


static status_t
acpi_als_read(void* _cookie, off_t position, void* buffer, size_t* numBytes)
{
	if (*numBytes < 1)
		return B_IO_ERROR;

	als_device_cookie* device = (als_device_cookie*)_cookie;

	if (position == 0) {
		char string[10];
		uint64 luminance = 0;
		status_t status = acpi_GetInteger(device->driver_cookie,
			"_ALI", &luminance);
		if (status != B_OK)
			return B_ERROR;
		snprintf(string, sizeof(string), "%" B_PRIu64 "\n", luminance);
		size_t max_len = user_strlcpy((char*)buffer, string, *numBytes);
		if (max_len < B_OK)
			return B_BAD_ADDRESS;
		*numBytes = max_len;
	} else {
		*numBytes = 0;
	}

	return B_OK;
}


static status_t
acpi_als_write(void* cookie, off_t position, const void* buffer,
	size_t* numBytes)
{
	return B_ERROR;
}


static status_t
acpi_als_control(void* _cookie, uint32 op, void* arg, size_t len)
{
	return B_DEV_INVALID_IOCTL;
}


static status_t
acpi_als_free(void* cookie)
{
	als_device_cookie* device = (als_device_cookie*)cookie;
	free(device);
	return B_OK;
}


static dk_device_ops sAcpiAlsDeviceOps = {
	acpi_als_open, acpi_als_close, acpi_als_free,
	acpi_als_read, acpi_als_write, NULL,
	acpi_als_control, NULL, NULL, NULL
};


//	#pragma mark - match & driver


static const dk_match_rule sAcpiAlsMatchRules[] = {
	{ KOSM_DEVICE_BUS,       B_STRING_TYPE, { .string = "acpi" } },
	{ "acpi/type",           B_UINT32_TYPE, { .ui32 = ACPI_TYPE_DEVICE } },
	{ KOSM_ACPI_DEVICE_HID,  B_STRING_TYPE, { .string = ACPI_NAME_ALS } },
	{}
};

static const dk_match_dict sAcpiAlsMatchDict = {
	sAcpiAlsMatchRules, 0
};


static float
acpi_als_support(dk_node* parent)
{
	return 0.6f;
}


static status_t
acpi_als_attach(dk_node* node, void** _driverCookie)
{
	als_driver_cookie* device;
	device = (als_driver_cookie*)calloc(1, sizeof(als_driver_cookie));
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = sDeviceKeeper->get_interface(node,
		ACPI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&device->acpi, (void**)&device->acpi_cookie);
	if (status != B_OK) {
		free(device);
		return status;
	}

#ifdef TRACE_ALS
	char device_path[256];
	if (sDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_PATH,
		device_path, sizeof(device_path), NULL, true) == B_OK) {
		TRACE("acpi_als_attach %s\n", device_path);
	}
#endif

	uint64 sta;
	status = acpi_GetInteger(device, "_STA", &sta);
	uint64 mask = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED
		| ACPI_STA_DEVICE_FUNCTIONING;
	if (status == B_OK && (sta & mask) != mask) {
		ERROR("acpi_als_attach device disabled\n");
		free(device);
		return B_ERROR;
	}

	uint64 luminance;
	status = acpi_GetInteger(device, "_ALI", &luminance);
	if (status != B_OK) {
		ERROR("acpi_als_attach error when calling _ALI\n");
		free(device);
		return B_ERROR;
	}

	device->acpi->install_notify_handler(device->acpi_cookie,
		ACPI_ALL_NOTIFY, als_notify_handler, device);

	int pathID = sDeviceKeeper->create_id(ACPI_ALS_PATHID_GENERATOR);
	if (pathID < 0) {
		ERROR("attach: couldn't create a path_id\n");
		free(device);
		return B_ERROR;
	}

	char devname[B_DEV_NAME_LENGTH];
	snprintf(devname, sizeof(devname), ACPI_ALS_BASENAME, pathID);
	sDeviceKeeper->publish_device(node, devname, &sAcpiAlsDeviceOps);

	*_driverCookie = device;
	return B_OK;
}


static void
acpi_als_detach(void* driverCookie)
{
	TRACE("acpi_als_detach\n");
	als_driver_cookie* device = (als_driver_cookie*)driverCookie;

	device->acpi->remove_notify_handler(device->acpi_cookie,
		ACPI_ALL_NOTIFY, als_notify_handler);

	free(device);
}


static status_t
acpi_als_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			sALSCondition.Init(NULL, "als condition");
			return get_module(KOSM_DEVICE_KEEPER_MODULE_NAME,
				(module_info**)&sDeviceKeeper);
		case B_MODULE_UNINIT:
			put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
			return B_OK;
		default:
			return B_ERROR;
	}
}


static dk_driver_info acpi_als_driver = {
	.info   = { ACPI_ALS_DRIVER_NAME, 0, acpi_als_std_ops },
	.match  = &sAcpiAlsMatchDict,
	.probe  = acpi_als_support,
	.attach = acpi_als_attach,
	.detach = acpi_als_detach,
	.ops    = &sAcpiAlsDeviceOps,
};

module_info* modules[] = {
	(module_info*)&acpi_als_driver,
	NULL
};
