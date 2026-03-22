/*
 * Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2016, Jessica Hamilton, jessica.l.hamilton@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include <device_manager.h>
#include <OS.h>
#include <KernelExport.h>
#include <SupportDefs.h>
#include <bus/PCI.h>
#include <frame_buffer_console.h>
#include <boot_item.h>
#include <vesa_info.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "driver.h"
#include "device.h"


#define TRACE_DRIVER
#ifdef TRACE_DRIVER
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define MAX_CARDS 1

#define FB_DRIVER_MODULE_NAME "drivers/graphics/framebuffer/driver_v1"
#define FB_DEVICE_MODULE_NAME "drivers/graphics/framebuffer/device_v1"


char* gDeviceNames[MAX_CARDS + 1];
framebuffer_info* gDeviceInfo[MAX_CARDS];
mutex gLock;

static device_manager_info* sDeviceManager;
static bool sPublished = false;


//	#pragma mark - driver module API


static float
fb_supports_device(device_node *parent)
{
	TRACE((DEVICE_NAME ": supports_device()\n"));
	const char *bus;

	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;

	if (strcmp(bus, "pci"))
		return 0.0;

	// only match if we're a framebuffer (no VESA modes, but have frame buffer)
	if (get_boot_item(VESA_MODES_BOOT_INFO, NULL) != NULL
		|| get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL) == NULL)
		return 0.0;

	if (sPublished)
		return 0.0;

	// match PCI display devices with low priority
	uint8 classBase = 0;
	sDeviceManager->get_attr_uint8(parent, B_DEVICE_TYPE, &classBase, false);
	if (classBase == PCI_display) {
		TRACE((DEVICE_NAME ": framebuffer device found!\n"));
		return 0.1;
	}

	return 0.0;
}


static status_t
fb_register_device(device_node *node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "Framebuffer"} },
		{ NULL }
	};

	return sDeviceManager->register_node(node, FB_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
fb_init_driver(device_node *node, void **cookie)
{
	TRACE((DEVICE_NAME ": init_driver()\n"));

	gDeviceInfo[0] = (framebuffer_info*)malloc(sizeof(framebuffer_info));
	if (gDeviceInfo[0] == NULL)
		return B_NO_MEMORY;

	memset(gDeviceInfo[0], 0, sizeof(framebuffer_info));

	gDeviceNames[0] = strdup("graphics/framebuffer");
	if (gDeviceNames[0] == NULL) {
		free(gDeviceInfo[0]);
		return B_NO_MEMORY;
	}

	gDeviceNames[1] = NULL;

	mutex_init(&gLock, "framebuffer lock");

	*cookie = node;
	return B_OK;
}


static void
fb_uninit_driver(void *_cookie)
{
	TRACE((DEVICE_NAME ": uninit_driver()\n"));

	mutex_destroy(&gLock);

	char* name;
	for (int32 index = 0; (name = gDeviceNames[index]) != NULL; index++) {
		free(gDeviceInfo[index]);
		free(name);
		gDeviceNames[index] = NULL;
		gDeviceInfo[index] = NULL;
	}

	sPublished = false;
}


static status_t
fb_register_child_devices(void *_cookie)
{
	device_node *node = (device_node *)_cookie;

	if (sPublished || gDeviceNames[0] == NULL)
		return B_OK;

	sPublished = true;
	return sDeviceManager->publish_device(node, gDeviceNames[0],
		FB_DEVICE_MODULE_NAME);
}


//	#pragma mark - device module API


static status_t
fb_open(void *_info, const char *path, int openMode, void **_cookie)
{
	return gDeviceHooks.open(path, openMode, _cookie);
}


static status_t
fb_close(void *cookie)
{
	return gDeviceHooks.close(cookie);
}


static status_t
fb_free(void *cookie)
{
	return gDeviceHooks.free(cookie);
}


static status_t
fb_read(void *cookie, off_t position, void *buffer, size_t *numBytes)
{
	return gDeviceHooks.read(cookie, position, buffer, numBytes);
}


static status_t
fb_write(void *cookie, off_t position, const void *buffer, size_t *numBytes)
{
	return gDeviceHooks.write(cookie, position, buffer, numBytes);
}


static status_t
fb_control(void *cookie, uint32 op, void *buffer, size_t length)
{
	return gDeviceHooks.control(cookie, op, buffer, length);
}


static status_t
fb_init_device(void *_info, void **_cookie)
{
	*_cookie = _info;
	return B_OK;
}


static void
fb_uninit_device(void *_cookie)
{
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&sDeviceManager },
	{ NULL }
};

struct device_module_info sFbDevice = {
	{
		FB_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	fb_init_device,
	fb_uninit_device,
	NULL,	// device_removed

	fb_open,
	fb_close,
	fb_free,
	fb_read,
	fb_write,
	NULL,	// io
	fb_control,

	NULL,	// select
	NULL,	// deselect
};

struct driver_module_info sFbDriver = {
	{
		FB_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	fb_supports_device,
	fb_register_device,
	fb_init_driver,
	fb_uninit_driver,
	fb_register_child_devices,
	NULL,	// rescan
	NULL,	// removed
};

module_info* modules[] = {
	(module_info*)&sFbDriver,
	(module_info*)&sFbDevice,
	NULL
};
