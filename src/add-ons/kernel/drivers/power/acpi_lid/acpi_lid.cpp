/*
 * Copyright 2008-2013, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <ACPI.h>
#include <device_keeper.h>

#include <fs/select_sync_pool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define ACPI_LID_MODULE_NAME "drivers/power/acpi_lid/dk_driver_v1"
#define ACPI_LID_BASENAME "power/acpi_lid/%d"
#define ACPI_LID_PATHID_GENERATOR "acpi_lid/path_id"

#define ACPI_NOTIFY_STATUS_CHANGED	0x80

//#define TRACE_LID
#ifdef TRACE_LID
#	define TRACE(x...) dprintf("acpi_lid: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("acpi_lid: " x)

static dk_keeper_info* sDeviceKeeper;


typedef struct acpi_lid_device_info {
	acpi_device_module_info*	acpi;
	acpi_device					acpi_cookie;
	uint8						last_status;
	bool						updated;
	select_sync_pool*			select_pool;
} acpi_lid_device_info;


static void
acpi_lid_read_status(acpi_lid_device_info* device)
{
	acpi_data buf;
	buf.pointer = NULL;
	buf.length = ACPI_ALLOCATE_BUFFER;
	if (device->acpi->evaluate_method(device->acpi_cookie, "_LID", NULL,
		&buf) != B_OK || buf.pointer == NULL
		|| ((acpi_object_type*)buf.pointer)->object_type
			!= ACPI_TYPE_INTEGER) {
		ERROR("couldn't get status\n");
	} else {
		acpi_object_type* object = (acpi_object_type*)buf.pointer;
		device->last_status = object->integer.integer;
		device->updated = true;
		TRACE("status %d\n", device->last_status);
	}
	free(buf.pointer);
}


static void
acpi_lid_notify_handler(acpi_handle _device, uint32 value, void* context)
{
	acpi_lid_device_info* device = (acpi_lid_device_info*)context;
	if (value == ACPI_NOTIFY_STATUS_CHANGED) {
		TRACE("status changed\n");
		acpi_lid_read_status(device);
		if (device->select_pool != NULL)
			notify_select_event_pool(device->select_pool, B_SELECT_READ);
	} else {
		ERROR("unknown notification\n");
	}
}


//	#pragma mark - device ops

static status_t acpi_lid_open(void* _c, const char* p, int f, void** c)
	{ *c = _c; return B_OK; }

static status_t
acpi_lid_read(void* _cookie, off_t position, void* buf, size_t* num_bytes)
{
	acpi_lid_device_info* device = (acpi_lid_device_info*)_cookie;
	if (*num_bytes < 1) return B_IO_ERROR;
	if (position > 0) { *num_bytes = 0; return B_OK; }
	if (user_memcpy(buf, &device->last_status, sizeof(uint8)) < B_OK)
		return B_BAD_ADDRESS;
	*num_bytes = 1;
	device->updated = false;
	return B_OK;
}

static status_t acpi_lid_write(void* c, off_t p, const void* b, size_t* n)
	{ return B_ERROR; }
static status_t acpi_lid_control(void* c, uint32 op, void* a, size_t l)
	{ return B_ERROR; }

static status_t
acpi_lid_select(void* _cookie, uint8 event, selectsync* sync)
{
	acpi_lid_device_info* device = (acpi_lid_device_info*)_cookie;
	if (event != B_SELECT_READ) return B_BAD_VALUE;
	status_t error = add_select_sync_pool_entry(&device->select_pool,
		sync, event);
	if (error != B_OK) return error;
	if (device->updated) notify_select_event(sync, event);
	return B_OK;
}

static status_t
acpi_lid_deselect(void* _cookie, uint8 event, selectsync* sync)
{
	acpi_lid_device_info* device = (acpi_lid_device_info*)_cookie;
	if (event != B_SELECT_READ) return B_BAD_VALUE;
	return remove_select_sync_pool_entry(&device->select_pool, sync, event);
}

static status_t acpi_lid_close(void* c) { return B_OK; }
static status_t acpi_lid_free(void* c) { return B_OK; }

static dk_device_ops sAcpiLidDeviceOps = {
	acpi_lid_open, acpi_lid_close, acpi_lid_free,
	acpi_lid_read, acpi_lid_write, NULL,
	acpi_lid_control, acpi_lid_select, acpi_lid_deselect, NULL
};


//	#pragma mark - match & driver


static const dk_match_rule sAcpiLidMatchRules[] = {
	{ KOSM_DEVICE_BUS,       B_STRING_TYPE, { .string = "acpi" } },
	{ "acpi/type",           B_UINT32_TYPE, { .ui32 = ACPI_TYPE_DEVICE } },
	{ KOSM_ACPI_DEVICE_HID,  B_STRING_TYPE, { .string = "PNP0C0D" } },
	{}
};

static const dk_match_dict sAcpiLidMatchDict = {
	sAcpiLidMatchRules, 0
};


static float
acpi_lid_support(dk_node* parent)
{
	dprintf("acpi_lid_support lid device found\n");
	return 0.6f;
}


static status_t
acpi_lid_attach(dk_node* node, void** _driverCookie)
{
	acpi_lid_device_info* device;
	device = (acpi_lid_device_info*)calloc(1, sizeof(*device));
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
		ACPI_DEVICE_NOTIFY, acpi_lid_notify_handler, device);
	if (status != B_OK)
		ERROR("can't install notify handler\n");

	device->last_status = 0;
	device->updated = false;
	device->select_pool = NULL;

	int path_id = sDeviceKeeper->create_id(ACPI_LID_PATHID_GENERATOR);
	if (path_id < 0) {
		ERROR("attach: couldn't create a path_id\n");
		free(device);
		return B_ERROR;
	}

	char name[B_DEV_NAME_LENGTH];
	snprintf(name, sizeof(name), ACPI_LID_BASENAME, path_id);
	sDeviceKeeper->publish_device(node, name, &sAcpiLidDeviceOps);

	*_driverCookie = device;
	return B_OK;
}


static void
acpi_lid_detach(void* driverCookie)
{
	acpi_lid_device_info* device = (acpi_lid_device_info*)driverCookie;
	device->acpi->remove_notify_handler(device->acpi_cookie,
		ACPI_DEVICE_NOTIFY, acpi_lid_notify_handler);
	free(device);
}


static status_t
acpi_lid_std_ops(int32 op, ...)
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


static dk_driver_info acpi_lid_driver = {
	.info   = { ACPI_LID_MODULE_NAME, 0, acpi_lid_std_ops },
	.match  = &sAcpiLidMatchDict,
	.probe  = acpi_lid_support,
	.attach = acpi_lid_attach,
	.detach = acpi_lid_detach,
	.ops    = &sAcpiLidDeviceOps,
};

module_info* modules[] = {
	(module_info*)&acpi_lid_driver,
	NULL
};
