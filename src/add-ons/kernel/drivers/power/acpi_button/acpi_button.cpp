/*
 * Copyright 2013, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2005, Nathan Whitehorn.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <ACPI.h>
#include <device_keeper.h>

#include <fs/select_sync_pool.h>

#include <stdlib.h>
#include <string.h>


#define ACPI_BUTTON_MODULE_NAME "drivers/power/acpi_button/dk_driver_v1"

#define ACPI_NOTIFY_BUTTON_SLEEP	0x80
#define ACPI_NOTIFY_BUTTON_WAKEUP	0x2

//#define TRACE_BUTTON
#ifdef TRACE_BUTTON
#	define TRACE(x...) dprintf("acpi_button: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("acpi_button: " x)

static dk_keeper_info* sDeviceKeeper;
static struct acpi_module_info* sAcpi;


typedef struct acpi_button_device_info {
	acpi_device_module_info*	acpi;
	acpi_device					acpi_cookie;
	uint32						type;
	bool						fixed;
	uint8						last_status;
	select_sync_pool*			select_pool;
} acpi_button_device_info;


static void
acpi_button_notify_handler(acpi_handle _device, uint32 value, void* context)
{
	acpi_button_device_info* device = (acpi_button_device_info*)context;
	if (value == ACPI_NOTIFY_BUTTON_SLEEP) {
		TRACE("sleep\n");
		device->last_status = 1;
		if (device->select_pool != NULL)
			notify_select_event_pool(device->select_pool, B_SELECT_READ);
	} else if (value == ACPI_NOTIFY_BUTTON_WAKEUP) {
		TRACE("wakeup\n");
	} else {
		ERROR("unknown notification\n");
	}
}


static uint32
acpi_button_fixed_handler(void* context)
{
	acpi_button_device_info* device = (acpi_button_device_info*)context;
	TRACE("sleep\n");
	device->last_status = 1;
	if (device->select_pool != NULL)
		notify_select_event_pool(device->select_pool, B_SELECT_READ);
	return B_OK;
}


//	#pragma mark - device ops


static status_t
acpi_button_open(void* _cookie, const char* path, int flags, void** cookie)
{
	acpi_button_device_info* device = (acpi_button_device_info*)_cookie;
	if (device->fixed)
		sAcpi->enable_fixed_event(device->type);
	*cookie = device;
	return B_OK;
}


static status_t
acpi_button_read(void* _cookie, off_t position, void* buffer,
	size_t* num_bytes)
{
	acpi_button_device_info* device = (acpi_button_device_info*)_cookie;
	if (*num_bytes < sizeof(uint8))
		return B_IO_ERROR;
	if (user_memcpy(buffer, &device->last_status, sizeof(uint8)) < B_OK)
		return B_BAD_ADDRESS;
	device->last_status = 0;
	*num_bytes = 1;
	return B_OK;
}


static status_t
acpi_button_write(void* cookie, off_t position, const void* buffer,
	size_t* num_bytes)
{
	return B_ERROR;
}


static status_t
acpi_button_control(void* _cookie, uint32 op, void* arg, size_t len)
{
	return B_ERROR;
}


static status_t
acpi_button_select(void* _cookie, uint8 event, selectsync* sync)
{
	acpi_button_device_info* device = (acpi_button_device_info*)_cookie;
	if (event != B_SELECT_READ)
		return B_BAD_VALUE;
	status_t error = add_select_sync_pool_entry(&device->select_pool,
		sync, event);
	if (error != B_OK)
		return error;
	if (device->last_status != 0)
		notify_select_event(sync, event);
	return B_OK;
}


static status_t
acpi_button_deselect(void* _cookie, uint8 event, selectsync* sync)
{
	acpi_button_device_info* device = (acpi_button_device_info*)_cookie;
	if (event != B_SELECT_READ)
		return B_BAD_VALUE;
	return remove_select_sync_pool_entry(&device->select_pool, sync,
		event);
}


static status_t
acpi_button_close(void* cookie) { return B_OK; }

static status_t
acpi_button_free(void* cookie) { return B_OK; }


static dk_device_ops sAcpiButtonDeviceOps = {
	acpi_button_open, acpi_button_close, acpi_button_free,
	acpi_button_read, acpi_button_write, NULL,
	acpi_button_control, acpi_button_select, acpi_button_deselect,
	NULL
};


//	#pragma mark - match & driver


// Match dict narrows to ACPI device nodes. Probe checks specific HIDs
// since we match 4 different ones (PNP0C0C, ACPI_FPB, PNP0C0E, ACPI_FSB).
static const dk_match_rule sAcpiButtonMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "acpi" } },
	{ "acpi/type",     B_UINT32_TYPE, { .ui32 = ACPI_TYPE_DEVICE } },
	{}
};

static const dk_match_dict sAcpiButtonMatchDict = {
	sAcpiButtonMatchRules, 0
};


static float
acpi_button_support(dk_node* parent)
{
	char hid[64];
	if (sDeviceKeeper->get_property_string(parent, KOSM_ACPI_DEVICE_HID,
		hid, sizeof(hid), NULL, false) != B_OK) {
		return 0.0f;
	}

	if (strcmp(hid, "PNP0C0C") == 0 || strcmp(hid, "ACPI_FPB") == 0
		|| strcmp(hid, "PNP0C0E") == 0 || strcmp(hid, "ACPI_FSB") == 0) {
		TRACE("button device found: %s\n", hid);
		return 0.6f;
	}

	return 0.0f;
}


static status_t
acpi_button_attach(dk_node* node, void** _driverCookie)
{
	acpi_button_device_info* device;
	device = (acpi_button_device_info*)calloc(1, sizeof(*device));
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = sDeviceKeeper->get_interface(node,
		ACPI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&device->acpi, (void**)&device->acpi_cookie);
	if (status != B_OK) {
		free(device);
		return status;
	}

	// Walk up to the parent ACPI device node — the child created by
	// ProbeAndAttachAll has no properties of its own, so we need
	// recursive=true to find the HID stored on the ACPI device.
	char hid[64];
	if (sDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_HID,
		hid, sizeof(hid), NULL, true) != B_OK) {
		free(device);
		return B_ERROR;
	}

	device->fixed = strcmp(hid, "PNP0C0C") != 0
		&& strcmp(hid, "PNP0C0E") != 0;
	TRACE("Device found, hid: %s, fixed: %d\n", hid, device->fixed);

	if (strcmp(hid, "PNP0C0C") == 0 || strcmp(hid, "ACPI_FPB") == 0)
		device->type = ACPI_EVENT_POWER_BUTTON;
	else if (strcmp(hid, "PNP0C0E") == 0 || strcmp(hid, "ACPI_FSB") == 0)
		device->type = ACPI_EVENT_SLEEP_BUTTON;
	else {
		free(device);
		return B_ERROR;
	}

	device->last_status = 0;
	device->select_pool = NULL;

	if (device->fixed) {
		sAcpi->reset_fixed_event(device->type);
		TRACE("Installing fixed handler for type %" B_PRIu32 "\n",
			device->type);
		if (sAcpi->install_fixed_event_handler(device->type,
			acpi_button_fixed_handler, device) != B_OK) {
			ERROR("can't install fixed handler\n");
		}
	} else {
		TRACE("Installing notify handler for type %" B_PRIu32 "\n",
			device->type);
		if (device->acpi->install_notify_handler(device->acpi_cookie,
			ACPI_DEVICE_NOTIFY, acpi_button_notify_handler,
			device) != B_OK) {
			ERROR("can't install notify handler\n");
		}
	}

	// Publish device based on button type
	if (strcmp(hid, "PNP0C0C") == 0)
		sDeviceKeeper->publish_device(node, "power/button/power",
			&sAcpiButtonDeviceOps);
	else if (strcmp(hid, "ACPI_FPB") == 0)
		sDeviceKeeper->publish_device(node, "power/button/power_fixed",
			&sAcpiButtonDeviceOps);
	else if (strcmp(hid, "PNP0C0E") == 0)
		sDeviceKeeper->publish_device(node, "power/button/sleep",
			&sAcpiButtonDeviceOps);
	else if (strcmp(hid, "ACPI_FSB") == 0)
		sDeviceKeeper->publish_device(node, "power/button/sleep_fixed",
			&sAcpiButtonDeviceOps);

	*_driverCookie = device;
	return B_OK;
}


static void
acpi_button_detach(void* driverCookie)
{
	acpi_button_device_info* device =
		(acpi_button_device_info*)driverCookie;
	if (device->fixed) {
		sAcpi->remove_fixed_event_handler(device->type,
			acpi_button_fixed_handler);
	} else {
		device->acpi->remove_notify_handler(device->acpi_cookie,
			ACPI_DEVICE_NOTIFY, acpi_button_notify_handler);
	}
	free(device);
}


static status_t
acpi_button_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		{
			status_t s = get_module(KOSM_DEVICE_KEEPER_MODULE_NAME,
				(module_info**)&sDeviceKeeper);
			if (s != B_OK)
				return s;
			s = get_module(B_ACPI_MODULE_NAME, (module_info**)&sAcpi);
			if (s != B_OK) {
				put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
				return s;
			}
			return B_OK;
		}
		case B_MODULE_UNINIT:
			put_module(B_ACPI_MODULE_NAME);
			put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
			return B_OK;
		default:
			return B_ERROR;
	}
}


static dk_driver_info acpi_button_driver = {
	.info   = { ACPI_BUTTON_MODULE_NAME, 0, acpi_button_std_ops },
	.match  = &sAcpiButtonMatchDict,
	.probe  = acpi_button_support,
	.attach = acpi_button_attach,
	.detach = acpi_button_detach,
	.ops    = &sAcpiButtonDeviceOps,
};

module_info* modules[] = {
	(module_info*)&acpi_button_driver,
	NULL
};
