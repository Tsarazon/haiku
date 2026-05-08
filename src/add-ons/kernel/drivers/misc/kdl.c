/*
 * Copyright (c) 2009 François Revol, <revol@free.fr>.
 * Permission is hereby granted under the terms of the MIT license.
 */

#include <device_keeper.h>
#include <KernelExport.h>
#include <Drivers.h>
#include <Errors.h>
#include <string.h>

#define KDL_DRIVER_MODULE_NAME "drivers/misc/kdl/dk_driver_v1"

static dk_keeper_info* sDeviceKeeper;
static bool sPublished = false;
static int32 sOpenMask;


static status_t
driver_open(void* _info, const char* name, int flags, void** _cookie)
{
	dprintf("kdl: open\n");

	if (atomic_or(&sOpenMask, 1)) {
		dprintf("kdl: open, BUSY!\n");
		return B_BUSY;
	}

	*_cookie = NULL;
	dprintf("kdl: open, success\n");
	return B_OK;
}


static status_t
driver_close(void* cookie)
{
	dprintf("kdl: close\n");
	return B_OK;
}


static status_t
driver_free(void* cookie)
{
	dprintf("kdl: free\n");
	atomic_and(&sOpenMask, ~1);
	return B_OK;
}


static status_t
driver_read(void* cookie, off_t position, void* buf, size_t* num_bytes)
{
	dprintf("kdl: read\n");
	panic("requested from kdl driver.");
	*num_bytes = 0;
	return B_ERROR;
}


static status_t
driver_write(void* cookie, off_t position, const void* buffer,
	size_t* num_bytes)
{
	dprintf("kdl: write\n");
	*num_bytes = 1;
	return B_OK;
}


static status_t
driver_control(void* cookie, uint32 op, void* arg, size_t len)
{
	dprintf("kdl: control\n");
	return B_ERROR;
}


/*	#pragma mark - dk_device_ops / dk_driver_info */


static dk_device_ops sDeviceOps = {
	driver_open,
	driver_close,
	driver_free,
	driver_read,
	driver_write,
	NULL,	/* io */
	driver_control,
	NULL,	/* select */
	NULL,	/* deselect */
	NULL,	/* device_removed */
};


static const dk_match_rule sKdlMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "generic" } },
	{}
};

static const dk_match_dict sKdlMatchDict = {
	sKdlMatchRules,
	0
};


static float
kdl_probe(dk_node* node)
{
	if (sPublished)
		return 0.0;
	return 0.01;
}


static status_t
kdl_attach(dk_node* node, void** cookie)
{
	sPublished = true;
	sDeviceKeeper->publish_device(node, "misc/kdl", &sDeviceOps);
	*cookie = node;
	return B_OK;
}


static void
kdl_detach(void* _cookie)
{
	sPublished = false;
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&sDeviceKeeper },
	{ NULL }
};

struct dk_driver_info sKdlDriver = {
	.info	= { KDL_DRIVER_MODULE_NAME, 0, NULL },
	.match	= &sKdlMatchDict,
	.probe	= kdl_probe,
	.attach	= kdl_attach,
	.detach	= kdl_detach,
	.ops	= &sDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sKdlDriver,
	NULL
};
