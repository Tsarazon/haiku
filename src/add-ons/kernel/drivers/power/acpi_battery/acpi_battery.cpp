/*
 * Copyright 2009, Haiku, Inc. All Rights Reserved.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
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

#include "device/power_managment.h"


struct battery_driver_cookie {
	acpi_device_module_info*	acpi;
	acpi_device					acpi_cookie;
};


struct battery_device_cookie {
	battery_driver_cookie*		driver_cookie;
	int32						stop_watching;
};


#define ACPI_BATTERY_DRIVER_NAME "drivers/power/acpi_battery/dk_driver_v1"

#define ACPI_BATTERY_BASENAME "power/acpi_battery/%d"
#define ACPI_BATTERY_PATHID_GENERATOR "acpi_battery/path_id"

#define ACPI_NAME_BATTERY "PNP0C0A"

//#define TRACE_BATTERY
#ifdef TRACE_BATTERY
#	define TRACE(x...) dprintf("acpi_battery: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("acpi_battery: " x)


static dk_keeper_info* sDeviceKeeper;
static ConditionVariable sBatteryCondition;


inline uint32
GetUint32(acpi_object_type* pointer)
{
	return (pointer->object_type == ACPI_TYPE_INTEGER)
		? pointer->integer.integer : UINT32_MAX;
}


inline void
GetString(char* buffer, size_t length, acpi_object_type* pointer)
{
	strlcpy(buffer, (pointer->object_type == ACPI_TYPE_STRING)
		? pointer->string.string : "", length);
}


status_t
ReadBatteryStatus(battery_driver_cookie* cookie,
	acpi_battery_info* batteryStatus)
{
	TRACE("ReadBatteryStatus\n");

	acpi_data buffer;
	buffer.pointer = NULL;
	buffer.length = ACPI_ALLOCATE_BUFFER;

	status_t status = cookie->acpi->evaluate_method(cookie->acpi_cookie,
		"_BST", NULL, &buffer);
	if (status != B_OK)
		goto exit;

	{
		acpi_object_type* object = (acpi_object_type*)buffer.pointer;
		if (object->object_type != ACPI_TYPE_PACKAGE
			|| object->package.count < 4) {
			status = B_ERROR;
			goto exit;
		}

		acpi_object_type* pointer = object->package.objects;
		batteryStatus->state = GetUint32(pointer++);
		if (batteryStatus->state == UINT32_MAX)
			batteryStatus->state = BATTERY_CRITICAL_STATE;
		batteryStatus->current_rate = GetUint32(pointer++);
		if (batteryStatus->state == BATTERY_DISCHARGING
			&& batteryStatus->current_rate == 0) {
			batteryStatus->state = BATTERY_NOT_CHARGING;
		}
		batteryStatus->capacity = GetUint32(pointer++);
		batteryStatus->voltage = GetUint32(pointer++);

		if (batteryStatus->voltage == UINT32_MAX
			&& batteryStatus->current_rate == UINT32_MAX
			&& batteryStatus->capacity == UINT32_MAX) {
			batteryStatus->state = BATTERY_CRITICAL_STATE;
		}
	}

exit:
	free(buffer.pointer);
	return status;
}


status_t
ReadBatteryInfo(battery_driver_cookie* cookie,
	acpi_extended_battery_info* batteryInfo)
{
	TRACE("ReadBatteryInfo\n");

	acpi_data buffer;
	buffer.pointer = NULL;
	buffer.length = ACPI_ALLOCATE_BUFFER;

	bool isBIF = false;
	status_t status = cookie->acpi->evaluate_method(cookie->acpi_cookie,
		"_BIX", NULL, &buffer);
	if (status != B_OK) {
		isBIF = true;
		free(buffer.pointer);
		buffer.pointer = NULL;
		buffer.length = ACPI_ALLOCATE_BUFFER;
		status = cookie->acpi->evaluate_method(cookie->acpi_cookie,
			"_BIF", NULL, &buffer);
		if (status != B_OK)
			goto exit;
	}

	if (buffer.pointer == NULL) {
		status = B_ERROR;
		goto exit;
	}

	{
		acpi_object_type* object = (acpi_object_type*)buffer.pointer;

		TRACE("ReadBatteryInfo %d %u\n", object->object_type,
			object->package.count);
		if (object->object_type != ACPI_TYPE_PACKAGE
			|| (isBIF && object->package.count < 13)) {
			status = B_ERROR;
			goto exit;
		}

		acpi_object_type* pointer = object->package.objects;
		if (isBIF) {
			batteryInfo->revision = ACPI_BATTERY_REVISION_BIF;
		} else {
			batteryInfo->revision = GetUint32(pointer++);
			TRACE("ReadBatteryInfo revision %u\n", batteryInfo->revision);
			if (object->package.count < 20) {
				status = B_ERROR;
				goto exit;
			}
		}

		batteryInfo->power_unit = GetUint32(pointer++);
		batteryInfo->design_capacity = GetUint32(pointer++);
		batteryInfo->last_full_charge = GetUint32(pointer++);
		batteryInfo->technology = GetUint32(pointer++);
		batteryInfo->design_voltage = GetUint32(pointer++);
		batteryInfo->design_capacity_warning = GetUint32(pointer++);
		batteryInfo->design_capacity_low = GetUint32(pointer++);

		if (batteryInfo->revision != ACPI_BATTERY_REVISION_BIF) {
			batteryInfo->cycles = GetUint32(pointer++);
			batteryInfo->accuracy = GetUint32(pointer++);
			batteryInfo->max_sampling_time = GetUint32(pointer++);
			batteryInfo->min_sampling_time = GetUint32(pointer++);
			batteryInfo->max_average_interval = GetUint32(pointer++);
			batteryInfo->min_average_interval = GetUint32(pointer++);
		}

		batteryInfo->capacity_granularity_1 = GetUint32(pointer++);
		batteryInfo->capacity_granularity_2 = GetUint32(pointer++);
		GetString(batteryInfo->model_number,
			sizeof(batteryInfo->model_number), pointer++);
		GetString(batteryInfo->serial_number,
			sizeof(batteryInfo->serial_number), pointer++);
		GetString(batteryInfo->type, sizeof(batteryInfo->type), pointer++);
		GetString(batteryInfo->oem_info, sizeof(batteryInfo->oem_info),
			pointer++);

		if (batteryInfo->revision != ACPI_BATTERY_REVISION_BIF
			&& batteryInfo->revision >= ACPI_BATTERY_REVISION_1
			&& object->package.count > 20) {
			batteryInfo->swapping_capability = GetUint32(pointer++);
		}
	}

exit:
	free(buffer.pointer);
	return status;
}


int
EstimatedRuntime(battery_driver_cookie* cookie, acpi_battery_info* info)
{
	acpi_object_type argument;
	argument.object_type = ACPI_TYPE_INTEGER;
	argument.integer.integer = info->current_rate;

	acpi_objects arguments;
	arguments.count = 1;
	arguments.pointer = &argument;

	acpi_object_type object;

	acpi_data buffer;
	buffer.pointer = &object;
	buffer.length = sizeof(object);

	status_t status = cookie->acpi->evaluate_method(cookie->acpi_cookie,
		"_BTM", &arguments, &buffer);
	if (status != B_OK)
		return -1;

	acpi_object_type* returnObject = (acpi_object_type*)buffer.pointer;
	if (returnObject->object_type != ACPI_TYPE_INTEGER)
		return -1;

	return returnObject->integer.integer;
}


void
battery_notify_handler(acpi_handle device, uint32 value, void* context)
{
	TRACE("battery_notify_handler event 0x%x\n", int(value));
	sBatteryCondition.NotifyAll();
}


//	#pragma mark - device ops


static status_t
acpi_battery_open(void* initCookie, const char* path, int flags, void** cookie)
{
	battery_device_cookie* device;
	device = (battery_device_cookie*)calloc(1, sizeof(battery_device_cookie));
	if (device == NULL)
		return B_NO_MEMORY;

	device->driver_cookie = (battery_driver_cookie*)initCookie;
	device->stop_watching = 0;

	*cookie = device;
	return B_OK;
}


static status_t
acpi_battery_close(void* cookie)
{
	return B_OK;
}


static status_t
acpi_battery_read(void* _cookie, off_t position, void* buffer,
	size_t* numBytes)
{
	if (*numBytes < 1)
		return B_IO_ERROR;

	battery_device_cookie* device = (battery_device_cookie*)_cookie;

	acpi_battery_info batteryStatus;
	ReadBatteryStatus(device->driver_cookie, &batteryStatus);

	acpi_extended_battery_info batteryInfo;
	ReadBatteryInfo(device->driver_cookie, &batteryInfo);

	if (position == 0) {
		char string[512];
		char* str = string;
		ssize_t max_len = sizeof(string);

		snprintf(str, max_len, "Battery Status:\n");
		max_len -= strlen(str);
		str += strlen(str);

		snprintf(str, max_len, " State %" B_PRIu32 ", Current Rate %"
			B_PRIu32 ", Capacity %" B_PRIu32 ", Voltage %" B_PRIu32 "\n",
			batteryStatus.state, batteryStatus.current_rate,
			batteryStatus.capacity, batteryStatus.voltage);
		max_len -= strlen(str);
		str += strlen(str);

		snprintf(str, max_len, "\nBattery Info:\n");
		max_len -= strlen(str);
		str += strlen(str);

		snprintf(str, max_len, " Power Unit %" B_PRIu32
			", Design Capacity %" B_PRIu32 ", Last Full Charge %"
			B_PRIu32 ", Technology %" B_PRIu32 "\n",
			batteryInfo.power_unit, batteryInfo.design_capacity,
			batteryInfo.last_full_charge, batteryInfo.technology);
		max_len -= strlen(str);
		str += strlen(str);

		snprintf(str, max_len, " Design Voltage %" B_PRIu32
			", Design Capacity Warning %" B_PRIu32
			", Design Capacity Low %" B_PRIu32
			", Capacity Granularity1 %" B_PRIu32
			", Capacity Granularity2 %" B_PRIu32 "\n",
			batteryInfo.design_voltage,
			batteryInfo.design_capacity_warning,
			batteryInfo.design_capacity_low,
			batteryInfo.capacity_granularity_1,
			batteryInfo.capacity_granularity_2);
		max_len -= strlen(str);
		str += strlen(str);

		snprintf(str, max_len, " Model Number %s, Serial Number %s, "
			"Type %s, OEM Info %s\n", batteryInfo.model_number,
			batteryInfo.serial_number, batteryInfo.type,
			batteryInfo.oem_info);

		max_len = user_strlcpy((char*)buffer, string, *numBytes);
		if (max_len < B_OK)
			return B_BAD_ADDRESS;
		*numBytes = max_len;
	} else {
		*numBytes = 0;
	}

	return B_OK;
}


static status_t
acpi_battery_write(void* cookie, off_t position, const void* buffer,
	size_t* numBytes)
{
	return B_ERROR;
}


static status_t
acpi_battery_control(void* _cookie, uint32 op, void* arg, size_t len)
{
	battery_device_cookie* device = (battery_device_cookie*)_cookie;
	status_t err;

	switch (op) {
		case IDENTIFY_DEVICE:
		{
			if (len < sizeof(uint32))
				return B_BAD_VALUE;
			uint32 magicId = kMagicACPIBatteryID;
			if (!IS_USER_ADDRESS(arg)
				|| user_memcpy(arg, &magicId, sizeof(magicId)) < B_OK) {
				return B_BAD_ADDRESS;
			}
			return B_OK;
		}

		case GET_BATTERY_INFO:
		{
			if (len < sizeof(acpi_battery_info))
				return B_BAD_VALUE;
			acpi_battery_info batteryInfo;
			err = ReadBatteryStatus(device->driver_cookie, &batteryInfo);
			if (err != B_OK)
				return err;
			if (!IS_USER_ADDRESS(arg)
				|| user_memcpy(arg, &batteryInfo, sizeof(batteryInfo))
					< B_OK) {
				return B_BAD_ADDRESS;
			}
			return B_OK;
		}

		case GET_EXTENDED_BATTERY_INFO:
		{
			if (len < sizeof(acpi_extended_battery_info))
				return B_BAD_VALUE;
			acpi_extended_battery_info extBatteryInfo;
			err = ReadBatteryInfo(device->driver_cookie, &extBatteryInfo);
			if (err != B_OK)
				return err;
			if (!IS_USER_ADDRESS(arg)
				|| user_memcpy(arg, &extBatteryInfo,
					sizeof(extBatteryInfo)) < B_OK) {
				return B_BAD_ADDRESS;
			}
			return B_OK;
		}

		case WATCH_BATTERY:
			sBatteryCondition.Wait();
			if (atomic_get(&(device->stop_watching))) {
				atomic_set(&(device->stop_watching), 0);
				return B_ERROR;
			}
			return B_OK;

		case STOP_WATCHING_BATTERY:
			atomic_set(&(device->stop_watching), 1);
			sBatteryCondition.NotifyAll();
			return B_OK;
	}

	return B_DEV_INVALID_IOCTL;
}


static status_t
acpi_battery_free(void* cookie)
{
	battery_device_cookie* device = (battery_device_cookie*)cookie;
	free(device);
	return B_OK;
}


static dk_device_ops sAcpiBatteryDeviceOps = {
	acpi_battery_open,
	acpi_battery_close,
	acpi_battery_free,
	acpi_battery_read,
	acpi_battery_write,
	NULL,		// io
	acpi_battery_control,
	NULL,		// select
	NULL,		// deselect
	NULL		// device_removed
};


//	#pragma mark - match & driver


static const dk_match_rule sAcpiBatteryMatchRules[] = {
	{ KOSM_DEVICE_BUS,       B_STRING_TYPE, { .string = "acpi" } },
	{ "acpi/type",           B_UINT32_TYPE, { .ui32 = ACPI_TYPE_DEVICE } },
	{ KOSM_ACPI_DEVICE_HID,  B_STRING_TYPE, { .string = ACPI_NAME_BATTERY } },
	{}
};

static const dk_match_dict sAcpiBatteryMatchDict = {
	sAcpiBatteryMatchRules, 0
};


static float
acpi_battery_support(dk_node* parent)
{
	return 0.6f;
}


static status_t
acpi_battery_attach(dk_node* node, void** _driverCookie)
{
	battery_driver_cookie* device;
	device = (battery_driver_cookie*)calloc(1, sizeof(battery_driver_cookie));
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = sDeviceKeeper->get_interface(node,
		ACPI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&device->acpi, (void**)&device->acpi_cookie);
	if (status != B_OK) {
		free(device);
		return status;
	}

#ifdef TRACE_BATTERY
	char device_path[256];
	if (sDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_PATH,
		device_path, sizeof(device_path), NULL, true) == B_OK) {
		TRACE("acpi_battery_attach %s\n", device_path);
	}
#endif

	device->acpi->install_notify_handler(device->acpi_cookie,
		ACPI_ALL_NOTIFY, battery_notify_handler, device);

	int pathID = sDeviceKeeper->create_id(ACPI_BATTERY_PATHID_GENERATOR);
	if (pathID < 0) {
		ERROR("attach: couldn't create a path_id\n");
		free(device);
		return B_ERROR;
	}

	char devname[B_DEV_NAME_LENGTH];
	snprintf(devname, sizeof(devname), ACPI_BATTERY_BASENAME, pathID);
	sDeviceKeeper->publish_device(node, devname, &sAcpiBatteryDeviceOps);

	*_driverCookie = device;
	return B_OK;
}


static void
acpi_battery_detach(void* driverCookie)
{
	TRACE("acpi_battery_detach\n");
	battery_driver_cookie* device = (battery_driver_cookie*)driverCookie;

	device->acpi->remove_notify_handler(device->acpi_cookie,
		ACPI_ALL_NOTIFY, battery_notify_handler);

	free(device);
}


static status_t
acpi_battery_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			sBatteryCondition.Init(NULL, "battery condition");
			return get_module(KOSM_DEVICE_KEEPER_MODULE_NAME,
				(module_info**)&sDeviceKeeper);
		case B_MODULE_UNINIT:
			put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
			return B_OK;
		default:
			return B_ERROR;
	}
}


static dk_driver_info acpi_battery_driver = {
	.info   = { ACPI_BATTERY_DRIVER_NAME, 0, acpi_battery_std_ops },
	.match  = &sAcpiBatteryMatchDict,
	.probe  = acpi_battery_support,
	.attach = acpi_battery_attach,
	.detach = acpi_battery_detach,
	.ops    = &sAcpiBatteryDeviceOps,
};

module_info* modules[] = {
	(module_info*)&acpi_battery_driver,
	NULL
};
