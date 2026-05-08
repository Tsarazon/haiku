/*
 * Copyright 2002-2006, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <device_keeper.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <new>
#include <string.h>


#define NULL_DRIVER_MODULE_NAME "drivers/common/null/dk_driver_v1"

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


static dk_keeper_info* sDeviceKeeper;


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
null_supports_device(dk_node* parent)
{
	char bus[64];
	if (sDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1.0f;
	if (strcmp(bus, "generic") == 0)
		return 0.01f;
	return -1.0f;
}


static dk_device_ops sNullDeviceOps = {
	null_open,
	null_close,
	null_free,
	null_read,
	null_write,
	NULL,	// io
	null_ioctl,
	NULL,	// select
	NULL,	// deselect
	NULL	// device_removed
};


static status_t
null_init_driver(dk_node* node, void** cookie)
{
	sDeviceKeeper->publish_device(node, DEVICE_NAME_NULL, &sNullDeviceOps);
	sDeviceKeeper->publish_device(node, DEVICE_NAME_ZERO, &sNullDeviceOps);
	sDeviceKeeper->publish_device(node, DEVICE_NAME_FULL, &sNullDeviceOps);
	*cookie = node;
	return B_OK;
}


static void
null_uninit_driver(void* _cookie)
{
}


//	#pragma mark -


static const dk_match_rule sNullMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "generic" } },
	{}
};

static const dk_match_dict sNullMatchDict = {
	sNullMatchRules,
	0
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&sDeviceKeeper },
	{ NULL }
};

static dk_driver_info sNullDriver = {
	.info	= { NULL_DRIVER_MODULE_NAME, 0, NULL },
	.match	= &sNullMatchDict,
	.probe	= null_supports_device,
	.attach	= null_init_driver,
	.detach	= null_uninit_driver,
	.ops	= &sNullDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sNullDriver,
	NULL
};
