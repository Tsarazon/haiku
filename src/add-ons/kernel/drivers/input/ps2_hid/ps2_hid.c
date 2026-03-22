/*
 * Copyright 2005-2009 Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * PS/2 hid device driver
 *
 * Authors (in chronological order):
 *		Marcus Overhagen (marcus@overhagen.de)
 */


#include <device_manager.h>
#include <KernelExport.h>
#include <Drivers.h>
#include <string.h>

#include "PS2.h"

#define TRACE(x) dprintf x

#define PS2_HID_DRIVER_MODULE_NAME "drivers/input/ps2_hid/driver_v1"
#define PS2_HID_DEVICE_MODULE_NAME "drivers/input/ps2_hid/device_v1"

static device_manager_info *sDeviceManager;
static bool sPublished = false;

ps2_module_info *gPs2 = NULL;


static float
ps2_hid_supports_device(device_node *parent)
{
	const char *bus;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;
	if ((strcmp(bus, "root") == 0 || strcmp(bus, "pci") == 0)
		&& !sPublished)
		return 0.01;
	return 0.0;
}


static status_t
ps2_hid_register_device(device_node *node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "PS/2 HID"} },
		{ NULL }
	};
	return sDeviceManager->register_node(node,
		PS2_HID_DRIVER_MODULE_NAME, attrs, NULL, NULL);
}


static status_t
ps2_hid_init_driver(device_node *node, void **cookie)
{
	TRACE(("ps2_hid: init_driver\n"));
	status_t status = get_module(B_PS2_MODULE_NAME, (module_info **)&gPs2);
	if (status != B_OK)
		return status;
	*cookie = node;
	return B_OK;
}


static void
ps2_hid_uninit_driver(void *_cookie)
{
	TRACE(("ps2_hid: uninit_driver\n"));
	put_module(B_PS2_MODULE_NAME);
	sPublished = false;
}


static status_t
ps2_hid_register_child_devices(void *_cookie)
{
	/* ps2_hid publishes no devices — PS/2 bus manager handles device creation */
	(void)_cookie;
	sPublished = true;
	return B_OK;
}


static status_t ps2_hid_init_device(void *i, void **c)
{ *c = i; return B_OK; }
static void ps2_hid_uninit_device(void *c) {}

static status_t ps2_hid_open(void *i, const char *n, int m, void **c)
{ return B_ERROR; }
static status_t ps2_hid_close(void *c) { return B_OK; }
static status_t ps2_hid_free(void *c) { return B_OK; }
static status_t ps2_hid_read(void *c, off_t p, void *b, size_t *l)
{ *l = 0; return B_ERROR; }
static status_t ps2_hid_write(void *c, off_t p, const void *b, size_t *l)
{ *l = 0; return B_ERROR; }
static status_t ps2_hid_control(void *c, uint32 o, void *b, size_t l)
{ return B_ERROR; }


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&sDeviceManager },
	{ NULL }
};

struct device_module_info sPs2HidDevice = {
	{ PS2_HID_DEVICE_MODULE_NAME, 0, NULL },
	ps2_hid_init_device, ps2_hid_uninit_device, NULL,
	ps2_hid_open, ps2_hid_close, ps2_hid_free,
	ps2_hid_read, ps2_hid_write, NULL, ps2_hid_control,
	NULL, NULL
};

struct driver_module_info sPs2HidDriver = {
	{ PS2_HID_DRIVER_MODULE_NAME, 0, NULL },
	ps2_hid_supports_device, ps2_hid_register_device,
	ps2_hid_init_driver, ps2_hid_uninit_driver,
	ps2_hid_register_child_devices, NULL, NULL
};

module_info *modules[] = {
	(module_info *)&sPs2HidDriver,
	(module_info *)&sPs2HidDevice,
	NULL
};
