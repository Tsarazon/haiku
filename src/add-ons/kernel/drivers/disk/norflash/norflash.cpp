/*
 * Copyright 2012, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ithamar R. Adema <ithamar@upgrade-android.com>
 */


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <device_keeper.h>
#include <drivers/KernelExport.h>
#include <drivers/Drivers.h>
#include <kernel/OS.h>


//#define TRACE_NORFLASH
#ifdef TRACE_NORFLASH
#define TRACE(x...)	dprintf("nor: " x)
#else
#define TRACE(x...)
#endif


#define NORFLASH_DRIVER_MODULE_NAME	"drivers/disk/norflash/dk_driver_v1"


#define NORFLASH_ADDR	0x00000000
#define SIZE_IN_BLOCKS	256

/* Hide the start of NOR since U-Boot lives there */
#define HIDDEN_BLOCKS	2

struct nor_driver_info {
	dk_node *node;
	size_t blocksize;
	size_t totalsize;

	area_id id;
	uint8 *mapped;
};


static dk_keeper_info *sDeviceKeeper;


static status_t
nor_open(void *deviceCookie, const char *path, int openMode,
	void **_cookie)
{
	TRACE("open(%s)\n", path);
	*_cookie = deviceCookie;
	return B_OK;
}


static status_t
nor_close(void *_cookie)
{
	TRACE("close()\n");
	return B_OK;
}


static status_t
nor_free(void *_cookie)
{
	TRACE("free()\n");
	return B_OK;
}


static status_t
nor_ioctl(void *cookie, uint32 op, void *buffer, size_t length)
{
	nor_driver_info *info = (nor_driver_info*)cookie;
	TRACE("ioctl(%ld,%lu)\n", op, length);

	switch (op) {
		case B_GET_GEOMETRY:
		{
			device_geometry *deviceGeometry = (device_geometry*)buffer;
			deviceGeometry->removable = false;
			deviceGeometry->bytes_per_sector = info->blocksize;
			deviceGeometry->sectors_per_track = info->totalsize / info->blocksize;
			deviceGeometry->cylinder_count = 1;
			deviceGeometry->head_count = 1;
			deviceGeometry->device_type = B_DISK;
			deviceGeometry->removable = false;
			deviceGeometry->read_only = true;
			deviceGeometry->write_once = false;
			return B_OK;
		}
		break;

		case B_GET_DEVICE_NAME:
			strlcpy((char*)buffer, "NORFlash", length);
			break;
	}

	return B_ERROR;
}


static status_t
nor_read(void *_cookie, off_t position, void *data, size_t *numbytes)
{
	nor_driver_info *info = (nor_driver_info*)_cookie;
	TRACE("read(%lld,%lu)\n", position, *numbytes);

	position += HIDDEN_BLOCKS * info->blocksize;

	if (position + *numbytes > info->totalsize)
		*numbytes = info->totalsize - (position + *numbytes);

	memcpy(data, info->mapped + position, *numbytes);

	return B_OK;
}


static status_t
nor_write(void *_cookie, off_t position, const void *data, size_t *numbytes)
{
	TRACE("write(%lld,%lu)\n", position, *numbytes);
	*numbytes = 0;
	return B_ERROR;
}


static dk_device_ops sNorFlashDeviceOps = {
	nor_open,
	nor_close,
	nor_free,
	nor_read,
	nor_write,
	NULL,	// io
	nor_ioctl,
	NULL,	// select
	NULL,	// deselect
};


static float
nor_supports_device(dk_node *parent)
{
	char bus[64];
	TRACE("supports_device\n");

	if (sDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return B_ERROR;

	if (strcmp(bus, "generic"))
		return 0.0;

	return 1.0;
}


static status_t
nor_attach(dk_node *node, void **cookie)
{
	TRACE("attach\n");

	nor_driver_info *info = (nor_driver_info*)malloc(sizeof(nor_driver_info));
	if (info == NULL)
		return B_NO_MEMORY;

	memset(info, 0, sizeof(*info));

	info->node = node;
	info->mapped = NULL;
	info->blocksize = 128 * 1024;
	info->totalsize = (SIZE_IN_BLOCKS - HIDDEN_BLOCKS) * info->blocksize;

	info->id = map_physical_memory("NORFlash", NORFLASH_ADDR, info->totalsize,
		B_ANY_KERNEL_ADDRESS, B_READ_AREA, (void **)&info->mapped);
	if (info->id < 0) {
		free(info);
		return info->id;
	}

	info->mapped += HIDDEN_BLOCKS * info->blocksize;

	sDeviceKeeper->publish_device(node, "disk/nor/0/raw", &sNorFlashDeviceOps);

	*cookie = info;
	return B_OK;
}


static void
nor_detach(void *_cookie)
{
	TRACE("detach\n");
	nor_driver_info *info = (nor_driver_info*)_cookie;
	if (info) {
		delete_area(info->id);
		free(info);
	}
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&sDeviceKeeper },
	{ }
};


static dk_driver_info sNorFlashDriver = {
	{
		NORFLASH_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	.probe = nor_supports_device,
	.attach = nor_attach,
	.detach = nor_detach,

	.ops = &sNorFlashDeviceOps,
};


module_info *modules[] = {
	(module_info*)&sNorFlashDriver,
	NULL
};
