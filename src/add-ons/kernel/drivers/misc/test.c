/*
 * Copyright (c) 2007 Marcus Overhagen <marcus@overhagen.de>
 * Permission is hereby granted under the terms of the MIT license.
 */

#include <device_keeper.h>
#include <KernelExport.h>
#include <Drivers.h>
#include <Errors.h>
#include <string.h>

#define TEST_DRIVER_MODULE_NAME "drivers/misc/test/dk_driver_v1"

static dk_keeper_info* sDeviceKeeper;
static bool sPublished = false;
static int32 sOpenMask;


static status_t
driver_open(void* driverCookie, const char* name, int flags, void** _cookie)
{
	dprintf("test: open\n");

	if (atomic_or(&sOpenMask, 1)) {
		dprintf("test: open, BUSY!\n");
		return B_BUSY;
	}

	*_cookie = NULL;
	dprintf("test: open, success\n");
	return B_OK;
}


static status_t
driver_close(void* cookie)
{
	dprintf("test: close enter\n");
	snooze(200000);
	dprintf("test: close leave\n");
	return B_OK;
}


static status_t
driver_free(void* cookie)
{
	dprintf("test: free\n");
	atomic_and(&sOpenMask, ~1);
	return B_OK;
}


static status_t
driver_read(void* cookie, off_t position, void* buf, size_t* num_bytes)
{
	dprintf("test: read\n");
	*num_bytes = 0;
	return B_ERROR;
}


static status_t
driver_write(void* cookie, off_t position, const void* buffer,
	size_t* num_bytes)
{
	dprintf("test: write\n");
	snooze(1000000);
	*num_bytes = 1;
	return B_OK;
}


static status_t
driver_control(void* cookie, uint32 op, void* arg, size_t len)
{
	dprintf("test: control\n");
	return B_ERROR;
}


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


/*	#pragma mark - dk_driver_info */


static const dk_match_rule sTestMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "generic" } },
	{}
};

static const dk_match_dict sTestMatchDict = {
	sTestMatchRules,
	0
};


static float
test_probe(dk_node* node)
{
	if (sPublished)
		return 0.0;
	return 0.01;
}


static status_t
test_attach(dk_node* node, void** cookie)
{
	sPublished = true;
	sDeviceKeeper->publish_device(node, "misc/test/1", &sDeviceOps);
	*cookie = node;
	return B_OK;
}


static void
test_detach(void* _cookie)
{
	sPublished = false;
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&sDeviceKeeper },
	{ NULL }
};

struct dk_driver_info sTestDriver = {
	.info	= { TEST_DRIVER_MODULE_NAME, 0, NULL },
	.match	= &sTestMatchDict,
	.probe	= test_probe,
	.attach	= test_attach,
	.detach	= test_detach,
	.ops	= &sDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sTestDriver,
	NULL
};
