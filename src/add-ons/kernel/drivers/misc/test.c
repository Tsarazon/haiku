/*
 * Copyright (c) 2007 Marcus Overhagen <marcus@overhagen.de>
 * Permission is hereby granted under the terms of the MIT license.
 */

#include <device_manager.h>
#include <KernelExport.h>
#include <Drivers.h>
#include <Errors.h>
#include <string.h>

#define TEST_DRIVER_MODULE_NAME "drivers/misc/test/driver_v1"
#define TEST_DEVICE_MODULE_NAME "drivers/misc/test/device_v1"

static device_manager_info* sDeviceManager;
static bool sPublished = false;
static int32 sOpenMask;


static status_t
driver_open(void* _info, const char* name, int flags, void** _cookie)
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


/*	#pragma mark - driver module API */


static float
test_supports_device(device_node* parent)
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
test_register_device(device_node* node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Test Driver"} },
		{ NULL }
	};
	return sDeviceManager->register_node(node, TEST_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
test_init_driver(device_node* node, void** cookie)
{
	*cookie = node;
	return B_OK;
}

static void test_uninit_driver(void* c) { sPublished = false; }

static status_t
test_register_child_devices(void* _cookie)
{
	device_node* node = (device_node*)_cookie;
	if (sPublished) return B_OK;
	sPublished = true;
	return sDeviceManager->publish_device(node, "misc/test/1",
		TEST_DEVICE_MODULE_NAME);
}

static status_t test_init_device(void* i, void** c)
{ *c = i; return B_OK; }
static void test_uninit_device(void* c) {}


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager },
	{ NULL }
};

struct device_module_info sTestDevice = {
	{ TEST_DEVICE_MODULE_NAME, 0, NULL },
	test_init_device, test_uninit_device, NULL,
	driver_open, driver_close, driver_free,
	driver_read, driver_write, NULL, driver_control,
	NULL, NULL
};

struct driver_module_info sTestDriver = {
	{ TEST_DRIVER_MODULE_NAME, 0, NULL },
	test_supports_device, test_register_device,
	test_init_driver, test_uninit_driver,
	test_register_child_devices, NULL, NULL
};

module_info* modules[] = {
	(module_info*)&sTestDriver,
	(module_info*)&sTestDevice,
	NULL
};
