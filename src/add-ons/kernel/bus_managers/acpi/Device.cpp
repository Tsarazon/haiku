/*
 * Copyright 2009, Clemens Zeidler. All rights reserved.
 * Copyright 2006, Jérôme Duval. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ACPIPrivate.h"
extern "C" {
#include "acpi.h"
}


static status_t
acpi_install_notify_handler(acpi_device device, uint32 handlerType,
	acpi_notify_handler handler, void *context)
{
	return install_notify_handler(device->handle, handlerType, handler,
		context);
}


static status_t
acpi_remove_notify_handler(acpi_device device, uint32 handlerType,
	acpi_notify_handler handler)
{
	return remove_notify_handler(device->handle, handlerType, handler);
}


static status_t
acpi_install_address_space_handler(acpi_device device, uint32 spaceId,
	acpi_adr_space_handler handler, acpi_adr_space_setup setup, void *data)
{
	return install_address_space_handler(device->handle, spaceId, handler,
		setup, data);
}


static status_t
acpi_remove_address_space_handler(acpi_device device, uint32 spaceId,
	acpi_adr_space_handler handler)
{
	return remove_address_space_handler(device->handle, spaceId, handler);
}


static uint32
acpi_get_object_type(acpi_device device)
{
	return device->type;
}


static status_t
acpi_get_object(acpi_device device, const char *path,
	acpi_object_type **return_value)
{
	if (device->path == NULL)
		return B_BAD_VALUE;
	if (path) {
		char objname[255];
		snprintf(objname, sizeof(objname), "%s.%s", device->path, path);
		return get_object(objname, return_value);
	}
	return get_object(device->path, return_value);
}


static status_t
acpi_evaluate_method(acpi_device device, const char *method,
	acpi_objects *args, acpi_data *returnValue)
{
	return evaluate_method(device->handle, method, args, returnValue);
}


static status_t
acpi_walk_resources(acpi_device device, char *method,
	acpi_walk_resources_callback callback, void* context)
{
	return walk_resources(device->handle, method, callback, context);
}


static status_t
acpi_walk_namespace(acpi_device device, uint32 objectType, uint32 maxDepth,
	acpi_walk_callback descendingCallback,
	acpi_walk_callback ascendingCallback, void* context, void** returnValue)
{
	return walk_namespace(device->handle, objectType, maxDepth,
		descendingCallback, ascendingCallback, context, returnValue);
}


acpi_device_module_info gACPIDeviceInterface = {
	acpi_install_notify_handler,
	acpi_remove_notify_handler,
	acpi_install_address_space_handler,
	acpi_remove_address_space_handler,
	acpi_get_object_type,
	acpi_get_object,
	acpi_walk_namespace,
	acpi_evaluate_method,
	acpi_walk_resources
};


//	#pragma mark - dk_driver_info


static status_t
acpi_device_attach(dk_node* node, void** cookie)
{
	ACPI_HANDLE handle = NULL;
	char pathBuf[255];
	uint32 type;

	if (gDeviceKeeper->get_property_uint32(node, KOSM_ACPI_DEVICE_TYPE,
			&type, false) != B_OK)
		return B_ERROR;

	status_t pathStatus = gDeviceKeeper->get_property_string(node,
		KOSM_ACPI_DEVICE_PATH, pathBuf, sizeof(pathBuf), NULL, false);
	const char* path = (pathStatus == B_OK) ? pathBuf : NULL;

	acpi_device_cookie* device
		= (acpi_device_cookie*)malloc(sizeof(*device));
	if (device == NULL)
		return B_NO_MEMORY;
	memset(device, 0, sizeof(*device));

	if (path != NULL
		&& AcpiGetHandle(NULL, (ACPI_STRING)path, &handle) != AE_OK) {
		free(device);
		return B_ENTRY_NOT_FOUND;
	}

	device->handle = handle;
	device->path = path != NULL ? strdup(path) : NULL;
	device->type = type;
	device->node = node;
	snprintf(device->name, sizeof(device->name), "acpi_device %s",
		path != NULL ? path : "(none)");

	// Publish the acpi_device_module_info interface on this node; child
	// drivers (like acpi_button, acpi_battery, etc) retrieve it via
	// get_interface(node, ACPI_DEVICE_INTERFACE_NAME, ...).
	status_t status = gDeviceKeeper->publish_interface(node,
		ACPI_DEVICE_INTERFACE_NAME, &gACPIDeviceInterface);
	if (status != B_OK) {
		free(device->path);
		free(device);
		return status;
	}

	*cookie = device;
	return B_OK;
}


static void
acpi_device_detach(void* cookie)
{
	acpi_device_cookie* device = (acpi_device_cookie*)cookie;
	free(device->path);
	free(device);
}


// Matches every acpi/* node. Attached via module-name auto-attach from
// register_node() calls in Module.cpp; no match rule needed.
struct dk_driver_info gACPIDeviceDriver = {
	.info	= { ACPI_DEVICE_MODULE_NAME, 0, NULL },
	.attach	= acpi_device_attach,
	.detach	= acpi_device_detach,
};
