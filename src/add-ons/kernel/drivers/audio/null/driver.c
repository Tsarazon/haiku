/*
 * Copyright 2007 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Bek, host.haiku@gmx.de
 */
#include "driver.h"

#include <device_manager.h>
#include <string.h>


#define NULL_AUDIO_DRIVER_MODULE_NAME "drivers/audio/null_audio/driver_v1"
#define NULL_AUDIO_DEVICE_MODULE_NAME "drivers/audio/null_audio/device_v1"

static device_manager_info *sDeviceManager;
static bool sPublished = false;

device_t device;


static status_t
null_audio_open(void* _info, const char* name, int flags, void** cookie)
{
	dprintf("null_audio: %s\n", __func__);
	*cookie = &device;
	return B_OK;
}


static status_t
null_audio_read(void* cookie, off_t a, void* b, size_t* num_bytes)
{
	*num_bytes = 0;
	return B_IO_ERROR;
}


static status_t
null_audio_write(void* cookie, off_t a, const void* b, size_t* num_bytes)
{
	*num_bytes = 0;
	return B_IO_ERROR;
}


static status_t
null_audio_control(void* cookie, uint32 op, void* arg, size_t len)
{
	if (cookie)
		return multi_audio_control(cookie, op, arg, len);
	return B_BAD_VALUE;
}


static status_t
null_audio_close(void* cookie)
{
	device_t* dev = (device_t*)cookie;
	if (dev && dev->running)
		null_stop_hardware(dev);
	return B_OK;
}


static status_t
null_audio_free(void* cookie)
{
	return B_OK;
}


/*	#pragma mark - driver module API */


static float
null_audio_supports_device(device_node *parent)
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
null_audio_register_device(device_node *node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "Null Audio"} },
		{ NULL }
	};
	return sDeviceManager->register_node(node,
		NULL_AUDIO_DRIVER_MODULE_NAME, attrs, NULL, NULL);
}


static status_t
null_audio_init_driver(device_node *node, void **cookie)
{
	device.running = false;
	*cookie = node;
	return B_OK;
}

static void null_audio_uninit_driver(void *c) { sPublished = false; }

static status_t
null_audio_register_child_devices(void *_cookie)
{
	device_node *node = (device_node *)_cookie;
	if (sPublished)
		return B_OK;
	sPublished = true;
	return sDeviceManager->publish_device(node,
		MULTI_AUDIO_DEV_PATH "/null/0",
		NULL_AUDIO_DEVICE_MODULE_NAME);
}

static status_t null_audio_init_device(void *i, void **c)
{ *c = i; return B_OK; }
static void null_audio_uninit_device(void *c) {}


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&sDeviceManager },
	{ NULL }
};

struct device_module_info sNullAudioDevice = {
	{ NULL_AUDIO_DEVICE_MODULE_NAME, 0, NULL },
	null_audio_init_device, null_audio_uninit_device, NULL,
	null_audio_open, null_audio_close, null_audio_free,
	null_audio_read, null_audio_write, NULL, null_audio_control,
	NULL, NULL
};

struct driver_module_info sNullAudioDriver = {
	{ NULL_AUDIO_DRIVER_MODULE_NAME, 0, NULL },
	null_audio_supports_device, null_audio_register_device,
	null_audio_init_driver, null_audio_uninit_driver,
	null_audio_register_child_devices, NULL, NULL
};

module_info *modules[] = {
	(module_info *)&sNullAudioDriver,
	(module_info *)&sNullAudioDevice,
	NULL
};
