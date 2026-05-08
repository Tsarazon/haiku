/*
 * Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2016, Jessica Hamilton, jessica.l.hamilton@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include <device_keeper.h>
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

#define FB_DRIVER_MODULE_NAME "drivers/graphics/framebuffer/dk_driver_v1"


char* gDeviceNames[MAX_CARDS + 1];
framebuffer_info* gDeviceInfo[MAX_CARDS];
mutex gLock;

static dk_keeper_info* sDeviceKeeper;
static bool sPublished = false;


//	#pragma mark - dk_device_ops


static status_t
fb_open(void *driverCookie, const char *path, int openMode, void **_cookie)
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


static dk_device_ops sDeviceOps = {
	fb_open,
	fb_close,
	fb_free,
	fb_read,
	fb_write,
	NULL,	// io
	fb_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


//	#pragma mark - dk_driver_info


static const dk_match_rule sFbMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "pci"),
	DK_MATCH_END
};

static const dk_match_dict sFbMatchDict = {
	sFbMatchRules,
	0
};


static float
fb_probe(dk_node *node)
{
	TRACE((DEVICE_NAME ": probe()\n"));

	// only match if we're a framebuffer (no VESA modes, but have frame buffer)
	if (get_boot_item(VESA_MODES_BOOT_INFO, NULL) != NULL
		|| get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL) == NULL)
		return 0.0;

	if (sPublished)
		return 0.0;

	// match PCI display devices with low priority
	uint8 classBase = 0;
	sDeviceKeeper->get_property_uint8(node, KOSM_DEVICE_TYPE,
		&classBase, false);
	if (classBase == PCI_display) {
		TRACE((DEVICE_NAME ": framebuffer device found!\n"));
		return 0.1;
	}

	return 0.0;
}


static status_t
fb_attach(dk_node *node, void **cookie)
{
	TRACE((DEVICE_NAME ": attach()\n"));

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

	// Publish device
	status_t status = sDeviceKeeper->publish_device(node, gDeviceNames[0],
		&sDeviceOps);
	if (status != B_OK) {
		TRACE((DEVICE_NAME ": publish_device failed: %s\n",
			strerror(status)));
		mutex_destroy(&gLock);
		free(gDeviceNames[0]);
		gDeviceNames[0] = NULL;
		free(gDeviceInfo[0]);
		gDeviceInfo[0] = NULL;
		return status;
	}

	sPublished = true;
	*cookie = node;
	return B_OK;
}


static void
fb_detach(void *_cookie)
{
	TRACE((DEVICE_NAME ": detach()\n"));

	dk_node* node = (dk_node*)_cookie;

	char* name;
	for (int32 index = 0; (name = gDeviceNames[index]) != NULL; index++) {
		sDeviceKeeper->unpublish_device(node, name);
		free(gDeviceInfo[index]);
		free(name);
		gDeviceNames[index] = NULL;
		gDeviceInfo[index] = NULL;
	}

	mutex_destroy(&gLock);

	sPublished = false;
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&sDeviceKeeper },
	{ NULL }
};

struct dk_driver_info sFbDriver = {
	.info   = { FB_DRIVER_MODULE_NAME, 0, NULL },
	.match  = &sFbMatchDict,
	.probe  = fb_probe,
	.attach = fb_attach,
	.detach = fb_detach,
	.ops    = &sDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sFbDriver,
	NULL
};
