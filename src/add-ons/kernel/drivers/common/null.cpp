/*
 * Copyright 2002-2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <device_manager.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <new>
#include <string.h>


#define NULL_DRIVER_MODULE_NAME "drivers/common/null/driver_v1"
#define NULL_DEVICE_MODULE_NAME "drivers/common/null/device_v1"

#define DEVICE_NAME_FULL "full"
#define DEVICE_NAME_NULL "null"
#define DEVICE_NAME_ZERO "zero"

enum null_device_type {
	NULL_TYPE_NULL,
	NULL_TYPE_ZERO,
	NULL_TYPE_FULL
};

struct null_cookie {
	null_device_type type;
};


static device_manager_info* sDeviceManager;
static bool sPublished = false;


static status_t
null_open(void* _info, const char* name, int openMode, void** _cookie)
{
	null_cookie* cookie = new(std::nothrow) null_cookie;
	if (cookie == NULL)
		return B_NO_MEMORY;

	if (strstr(name, DEVICE_NAME_FULL) != NULL)
		cookie->type = NULL_TYPE_FULL;
	else if (strstr(name, DEVICE_NAME_ZERO) != NULL)
		cookie->type = NULL_TYPE_ZERO;
	else
		cookie->type = NULL_TYPE_NULL;

	*_cookie = cookie;
	return B_OK;
}


static status_t
null_close(void* cookie)
{
	return B_OK;
}


static status_t
null_free(void* _cookie)
{
	delete (null_cookie*)_cookie;
	return B_OK;
}


static status_t
null_ioctl(void* cookie, uint32 op, void* buffer, size_t length)
{
	return EPERM;
}


static status_t
null_read(void* _cookie, off_t pos, void* buffer, size_t* _length)
{
	null_cookie* cookie = (null_cookie*)_cookie;

	if (cookie->type == NULL_TYPE_ZERO) {
		if (user_memset(buffer, 0, *_length) < B_OK)
			return B_BAD_ADDRESS;
		return B_OK;
	}

	// null and full: read returns 0 bytes
	*_length = 0;
	return B_OK;
}


static status_t
null_write(void* _cookie, off_t pos, const void* buffer, size_t* _length)
{
	null_cookie* cookie = (null_cookie*)_cookie;

	if (cookie->type == NULL_TYPE_FULL)
		return B_DEVICE_FULL;

	// null and zero: write succeeds silently
	return B_OK;
}


//	#pragma mark - driver module API


static float
null_supports_device(device_node* parent)
{
	const char* bus;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;
	if ((strcmp(bus, "root") == 0 || strcmp(bus, "pci") == 0)
		&& !sPublished)
		return 0.01;
	return 0.0;
}


static status_t
null_register_device(device_node* node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "Null devices"} },
		{ NULL }
	};
	return sDeviceManager->register_node(node, NULL_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
null_init_driver(device_node* node, void** cookie)
{
	*cookie = node;
	return B_OK;
}


static void
null_uninit_driver(void* _cookie)
{
	sPublished = false;
}


static status_t
null_register_child_devices(void* _cookie)
{
	device_node* node = (device_node*)_cookie;

	if (sPublished)
		return B_OK;

	sPublished = true;
	sDeviceManager->publish_device(node, DEVICE_NAME_NULL,
		NULL_DEVICE_MODULE_NAME);
	sDeviceManager->publish_device(node, DEVICE_NAME_ZERO,
		NULL_DEVICE_MODULE_NAME);
	sDeviceManager->publish_device(node, DEVICE_NAME_FULL,
		NULL_DEVICE_MODULE_NAME);
	return B_OK;
}


static status_t
null_init_device(void* _info, void** _cookie)
{
	*_cookie = _info;
	return B_OK;
}


static void
null_uninit_device(void* _cookie)
{
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager },
	{ NULL }
};

struct device_module_info sNullDevice = {
	{
		NULL_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	null_init_device,
	null_uninit_device,
	NULL,	// device_removed

	null_open,
	null_close,
	null_free,
	null_read,
	null_write,
	NULL,	// io
	null_ioctl,

	NULL,	// select
	NULL,	// deselect
};

struct driver_module_info sNullDriver = {
	{
		NULL_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	null_supports_device,
	null_register_device,
	null_init_driver,
	null_uninit_driver,
	null_register_child_devices,
	NULL,	// rescan
	NULL,	// removed
};

module_info* modules[] = {
	(module_info*)&sNullDriver,
	(module_info*)&sNullDevice,
	NULL
};
