/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include <KernelExport.h>
#include <Drivers.h>
#include <Errors.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>

#include <ACPI.h>
#include <device_keeper.h>

#include <util/convertutf.h>

#include "acpi_thermal.h"

#define ACPI_THERMAL_MODULE_NAME "drivers/power/acpi_thermal/dk_driver_v1"
#define ACPI_THERMAL_BASENAME "power/acpi_thermal/%d"
#define ACPI_THERMAL_PATHID_GENERATOR "acpi_thermal/path_id"

static dk_keeper_info* sDeviceKeeper;

typedef struct acpi_thermal_device_info {
	acpi_device_module_info*	acpi;
	acpi_device					acpi_cookie;
	uint						kelvin_offset;
} acpi_thermal_device_info;


status_t acpi_thermal_control(void* _cookie, uint32 op, void* arg, size_t len);

static void guess_kelvin_offset(acpi_thermal_device_info* device);


static void
guess_kelvin_offset(acpi_thermal_device_info* device)
{
	acpi_thermal_type therm_info;
	acpi_thermal_control(device, drvOpGetThermalType, &therm_info, 0);
	device->kelvin_offset = 2732;
	if (therm_info.critical_temp > 2732
		&& (therm_info.critical_temp % 5) == 1)
		device->kelvin_offset = 2731;
}


static status_t
acpi_thermal_open(void* _cookie, const char* path, int flags, void** cookie)
{
	acpi_thermal_device_info* device = (acpi_thermal_device_info*)_cookie;
	*cookie = device;
	if (device->kelvin_offset == 0)
		guess_kelvin_offset(device);
	return B_OK;
}


static status_t
acpi_thermal_read(void* _cookie, off_t position, void* buf, size_t* num_bytes)
{
	acpi_thermal_device_info* device = (acpi_thermal_device_info*)_cookie;
	acpi_thermal_type therm_info;

	if (*num_bytes < 1)
		return B_IO_ERROR;

	if (position == 0) {
		char string[128];
		char* str = string;
		size_t max_len = sizeof(string);

		uint kelvinOffset = device->kelvin_offset;
		acpi_thermal_control(device, drvOpGetThermalType, &therm_info, 0);

		snprintf(str, max_len,
			"  Critical Temperature: %" B_PRIu32 ".%" B_PRIu32 " C\n",
			((therm_info.critical_temp - kelvinOffset) / 10),
			((therm_info.critical_temp - kelvinOffset) % 10));
		max_len -= strlen(str);
		str += strlen(str);

		snprintf(str, max_len,
			"  Current Temperature: %" B_PRIu32 ".%" B_PRIu32 " C\n",
			((therm_info.current_temp - kelvinOffset) / 10),
			((therm_info.current_temp - kelvinOffset) % 10));
		max_len -= strlen(str);
		str += strlen(str);

		if (therm_info.hot_temp > 0) {
			snprintf(str, max_len,
				"  Hot Temperature: %" B_PRIu32 ".%" B_PRIu32 " C\n",
				((therm_info.hot_temp - kelvinOffset) / 10),
				((therm_info.hot_temp - kelvinOffset) % 10));
			max_len -= strlen(str);
			str += strlen(str);
		}

		if (therm_info.passive_package)
			free(therm_info.passive_package);

		max_len = user_strlcpy((char*)buf, string, *num_bytes);
		if (max_len < B_OK)
			return B_BAD_ADDRESS;
		*num_bytes = max_len;
	} else {
		*num_bytes = 0;
	}

	return B_OK;
}


static status_t
acpi_thermal_write(void* cookie, off_t position, const void* buffer,
	size_t* num_bytes)
{
	return B_ERROR;
}


status_t
acpi_thermal_control(void* _cookie, uint32 op, void* arg, size_t len)
{
	acpi_thermal_device_info* device = (acpi_thermal_device_info*)_cookie;
	status_t err = B_DEV_INVALID_IOCTL;

	switch (op) {
		case B_GET_DEVICE_NAME:
		{
			acpi_data buffer;
			buffer.pointer = NULL;
			buffer.length = ACPI_ALLOCATE_BUFFER;

			err = device->acpi->evaluate_method(device->acpi_cookie,
				"_STR", NULL, &buffer);
			if (err != B_OK) {
				dprintf("acpi_thermal: could not get zone name: %s\n",
					strerror(err));
				free(buffer.pointer);
				break;
			}

			acpi_object_type* object = (acpi_object_type*)buffer.pointer;
			if (object->object_type == ACPI_TYPE_STRING) {
				err = user_strlcpy((char*)arg, object->string.string,
					min(object->string.len, len));
				free(buffer.pointer);
			} else if (object->object_type == ACPI_TYPE_BUFFER) {
				char utf8[256];
				ssize_t bytes = utf16le_to_utf8(object->buffer.buffer,
					object->buffer.length / 2, utf8, sizeof(utf8));
				free(buffer.pointer);
				if (bytes < 0) {
					err = bytes;
					break;
				}
				err = user_strlcpy((char*)arg, utf8,
					min((size_t)bytes, len));
			}

			if (err > 0)
				err = B_OK;
			break;
		}
		case drvOpGetThermalType:
		{
			acpi_object_type object;
			acpi_thermal_type* att = (acpi_thermal_type*)arg;

			acpi_data buffer;
			buffer.pointer = &object;
			buffer.length = sizeof(object);

			err = device->acpi->evaluate_method(device->acpi_cookie,
				"_CRT", NULL, &buffer);
			att->critical_temp =
				(err == B_OK && object.object_type == ACPI_TYPE_INTEGER)
				? object.integer.integer : 0;

			err = device->acpi->evaluate_method(device->acpi_cookie,
				"_TMP", NULL, &buffer);
			att->current_temp =
				(err == B_OK && object.object_type == ACPI_TYPE_INTEGER)
				? object.integer.integer : 0;

			err = device->acpi->evaluate_method(device->acpi_cookie,
				"_HOT", NULL, &buffer);
			att->hot_temp =
				(err == B_OK && object.object_type == ACPI_TYPE_INTEGER)
				? object.integer.integer : 0;

			att->passive_package = NULL;
			att->active_count = 0;
			att->active_devices = NULL;
			err = B_OK;
			break;
		}
	}
	return err;
}


static status_t acpi_thermal_close(void* c) { return B_OK; }
static status_t acpi_thermal_free(void* c) { return B_OK; }


static dk_device_ops sAcpiThermalDeviceOps = {
	acpi_thermal_open, acpi_thermal_close, acpi_thermal_free,
	acpi_thermal_read, acpi_thermal_write, NULL,
	acpi_thermal_control, NULL, NULL, NULL
};


//	#pragma mark - match & driver


// ACPI thermal zones have type == ACPI_TYPE_THERMAL, not ACPI_TYPE_DEVICE.
static const dk_match_rule sAcpiThermalMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "acpi" } },
	{ "acpi/type",     B_UINT32_TYPE, { .ui32 = ACPI_TYPE_THERMAL } },
	{}
};

static const dk_match_dict sAcpiThermalMatchDict = {
	sAcpiThermalMatchRules, 0
};


static float
acpi_thermal_support(dk_node* parent)
{
	return 0.6f;
}


static status_t
acpi_thermal_attach(dk_node* node, void** _driverCookie)
{
	acpi_thermal_device_info* device;
	device = (acpi_thermal_device_info*)calloc(1, sizeof(*device));
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = sDeviceKeeper->get_interface(node,
		ACPI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&device->acpi, (void**)&device->acpi_cookie);
	if (status != B_OK) {
		free(device);
		return status;
	}

	int path_id = sDeviceKeeper->create_id(ACPI_THERMAL_PATHID_GENERATOR);
	if (path_id < 0) {
		free(device);
		return B_ERROR;
	}

	char name[B_DEV_NAME_LENGTH];
	snprintf(name, sizeof(name), ACPI_THERMAL_BASENAME, path_id);
	sDeviceKeeper->publish_device(node, name, &sAcpiThermalDeviceOps);

	*_driverCookie = device;
	return B_OK;
}


static void
acpi_thermal_detach(void* _cookie)
{
	free(_cookie);
}


static status_t
acpi_thermal_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return get_module(KOSM_DEVICE_KEEPER_MODULE_NAME,
				(module_info**)&sDeviceKeeper);
		case B_MODULE_UNINIT:
			put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
			return B_OK;
		default:
			return B_ERROR;
	}
}


static dk_driver_info acpi_thermal_driver = {
	.info   = { ACPI_THERMAL_MODULE_NAME, 0, acpi_thermal_std_ops },
	.match  = &sAcpiThermalMatchDict,
	.probe  = acpi_thermal_support,
	.attach = acpi_thermal_attach,
	.detach = acpi_thermal_detach,
	.ops    = &sAcpiThermalDeviceOps,
};

module_info* modules[] = {
	(module_info*)&acpi_thermal_driver,
	NULL
};
