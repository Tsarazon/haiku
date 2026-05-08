/*
 * Copyright 2013, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <ACPI.h>
#include <device_keeper.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define ACPI_AC_MODULE_NAME "drivers/power/acpi_ac/dk_driver_v1"
#define ACPI_AC_BASENAME "power/acpi_ac/%d"
#define ACPI_AC_PATHID_GENERATOR "acpi_ac/path_id"

#define TRACE_AC
#ifdef TRACE_AC
#	define TRACE(x...) dprintf("acpi_ac: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("acpi_ac: " x)

static dk_keeper_info* sDeviceKeeper;


typedef struct acpi_ac_device_info {
	acpi_device_module_info*	acpi;
	acpi_device					acpi_cookie;
	uint8						last_status;
} acpi_ac_device_info;


static void
acpi_ac_update_status(acpi_ac_device_info* device)
{
	acpi_data buf;
	buf.pointer = NULL;
	buf.length = ACPI_ALLOCATE_BUFFER;

	if (device->acpi->evaluate_method(device->acpi_cookie, "_PSR", NULL,
		&buf) != B_OK || buf.pointer == NULL
		|| ((acpi_object_type*)buf.pointer)->object_type
			!= ACPI_TYPE_INTEGER) {
		ERROR("couldn't get status\n");
	} else {
		acpi_object_type* object = (acpi_object_type*)buf.pointer;
		device->last_status = object->integer.integer;
		TRACE("status %d\n", device->last_status);
	}
	free(buf.pointer);
}


static void
acpi_ac_notify_handler(acpi_handle device, uint32 value, void* context)
{
	if (value != 0x80) {
		dprintf("acpi_ac: unknown notification (%d)\n", value);
		return;
	}
	acpi_ac_update_status((acpi_ac_device_info*)context);
}


//	#pragma mark - device ops


static status_t
acpi_ac_open(void* _cookie, const char* path, int flags, void** cookie)
{
	*cookie = _cookie;
	return B_OK;
}


static status_t
acpi_ac_read(void* _cookie, off_t position, void* buf, size_t* num_bytes)
{
	acpi_ac_device_info* device = (acpi_ac_device_info*)_cookie;
	if (*num_bytes < 1)
		return B_IO_ERROR;
	if (position > 0) {
		*num_bytes = 0;
		return B_OK;
	}
	if (user_memcpy(buf, &device->last_status, sizeof(uint8)) < B_OK)
		return B_BAD_ADDRESS;
	*num_bytes = 1;
	return B_OK;
}


static status_t
acpi_ac_write(void* cookie, off_t position, const void* buffer,
	size_t* num_bytes)
{
	return B_ERROR;
}


static status_t
acpi_ac_control(void* _cookie, uint32 op, void* arg, size_t len)
{
	return B_ERROR;
}


static status_t
acpi_ac_close(void* cookie) { return B_OK; }

static status_t
acpi_ac_free(void* cookie) { return B_OK; }


static dk_device_ops sAcpiAcDeviceOps = {
	acpi_ac_open, acpi_ac_close, acpi_ac_free,
	acpi_ac_read, acpi_ac_write, NULL,
	acpi_ac_control, NULL, NULL, NULL
};


//	#pragma mark - match & driver


static const dk_match_rule sAcpiAcMatchRules[] = {
	{ KOSM_DEVICE_BUS,       B_STRING_TYPE, { .string = "acpi" } },
	{ "acpi/type",           B_UINT32_TYPE, { .ui32 = ACPI_TYPE_DEVICE } },
	{ KOSM_ACPI_DEVICE_HID,  B_STRING_TYPE, { .string = "ACPI0003" } },
	{}
};

static const dk_match_dict sAcpiAcMatchDict = {
	sAcpiAcMatchRules, 0
};


static float
acpi_ac_support(dk_node* parent)
{
	dprintf("acpi_ac_support ac device found\n");
	return 0.6f;
}


static status_t
acpi_ac_attach(dk_node* node, void** _driverCookie)
{
	acpi_ac_device_info* device;
	device = (acpi_ac_device_info*)calloc(1, sizeof(*device));
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = sDeviceKeeper->get_interface(node,
		ACPI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&device->acpi, (void**)&device->acpi_cookie);
	if (status != B_OK) {
		free(device);
		return status;
	}

	status = device->acpi->install_notify_handler(device->acpi_cookie,
		ACPI_DEVICE_NOTIFY, acpi_ac_notify_handler, device);
	if (status != B_OK)
		ERROR("can't install notify handler\n");

	device->last_status = 0;
	acpi_ac_update_status(device);

	int path_id = sDeviceKeeper->create_id(ACPI_AC_PATHID_GENERATOR);
	if (path_id < 0) {
		ERROR("attach: couldn't create a path_id\n");
		free(device);
		return B_ERROR;
	}

	char name[B_DEV_NAME_LENGTH];
	snprintf(name, sizeof(name), ACPI_AC_BASENAME, path_id);
	sDeviceKeeper->publish_device(node, name, &sAcpiAcDeviceOps);

	*_driverCookie = device;
	return B_OK;
}


static void
acpi_ac_detach(void* driverCookie)
{
	acpi_ac_device_info* device = (acpi_ac_device_info*)driverCookie;
	device->acpi->remove_notify_handler(device->acpi_cookie,
		ACPI_DEVICE_NOTIFY, acpi_ac_notify_handler);
	free(device);
}


static status_t
acpi_ac_std_ops(int32 op, ...)
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


static dk_driver_info acpi_ac_driver = {
	.info   = { ACPI_AC_MODULE_NAME, 0, acpi_ac_std_ops },
	.match  = &sAcpiAcMatchDict,
	.probe  = acpi_ac_support,
	.attach = acpi_ac_attach,
	.detach = acpi_ac_detach,
	.ops    = &sAcpiAcDeviceOps,
};

module_info* modules[] = {
	(module_info*)&acpi_ac_driver,
	NULL
};
