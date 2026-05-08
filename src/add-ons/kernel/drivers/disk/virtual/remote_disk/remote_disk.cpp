/*
 * Copyright 2007, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include <string.h>

#include <KernelExport.h>
#include <Drivers.h>

#include <lock.h>
#include <util/AutoLock.h>
#include <util/kernel_cpp.h>

#include "RemoteDisk.h"


//#define TRACE_REMOTE_DISK
#ifdef TRACE_REMOTE_DISK
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) do {} while (false)
#endif


const bigtime_t kInitRetryDelay	= 10 * 1000000LL;	// 10 s

enum {
	MAX_REMOTE_DISKS	= 1
};


struct RemoteDiskDevice : recursive_lock {
	RemoteDisk*		remoteDisk;
	bigtime_t		lastInitRetryTime;

	RemoteDiskDevice()
		:
		remoteDisk(NULL),
		lastInitRetryTime(-1)
	{
	}

	~RemoteDiskDevice()
	{
		delete remoteDisk;
		Uninit();
	}

	status_t Init()
	{
		recursive_lock_init(this, "remote disk device");
		return B_OK;
	}

	void Uninit()
	{
		recursive_lock_destroy(this);
	}

	status_t LazyInitDisk()
	{
		if (remoteDisk)
			return B_OK;

		// don't try to init, if the last attempt wasn't long enough ago
		if (lastInitRetryTime >= 0
			&& system_time() < lastInitRetryTime + kInitRetryDelay) {
			return B_ERROR;
		}

		// create the object
		remoteDisk = new(nothrow) RemoteDisk;
		if (!remoteDisk) {
			lastInitRetryTime = system_time();
			return B_NO_MEMORY;
		}

		// find a server
		TRACE(("remote_disk: FindAnyRemoteDisk()\n"));
		status_t error = remoteDisk->FindAnyRemoteDisk();
		if (error != B_OK) {
			delete remoteDisk;
			remoteDisk = NULL;
			lastInitRetryTime = system_time();
			return B_NO_MEMORY;
		}

		return B_OK;
	}

	void GetGeometry(device_geometry* geometry, bool bios)
	{
		// TODO: Respect "bios" argument!
		geometry->bytes_per_sector = REMOTE_DISK_BLOCK_SIZE;
		geometry->sectors_per_track = 1;
		geometry->cylinder_count = remoteDisk->Size() / REMOTE_DISK_BLOCK_SIZE;
		geometry->head_count = 1;
		geometry->device_type = B_DISK;
		geometry->removable = true;
		geometry->read_only = remoteDisk->IsReadOnly();
		geometry->write_once = false;
		geometry->bytes_per_physical_sector = REMOTE_DISK_BLOCK_SIZE;
	}
};

typedef RecursiveLocker DeviceLocker;


static const char* kPublishedNames[] = {
	"disk/virtual/remote_disk/0/raw",
//	"misc/remote_disk_control",
	NULL
};

static RemoteDiskDevice* sDevices;


// #pragma mark - internal functions


// device_for_name
static RemoteDiskDevice*
device_for_name(const char* name)
{
	for (int i = 0; i < MAX_REMOTE_DISKS; i++) {
		if (strcmp(name, kPublishedNames[i]) == 0)
			return sDevices + i;
	}
	return NULL;
}


// #pragma mark - data device hooks


static status_t
remote_disk_open(const char* name, uint32 flags, void** cookie)
{
	RemoteDiskDevice* device = device_for_name(name);
	TRACE(("remote_disk_open(\"%s\") -> %p\n", name, device));
	if (!device)
		return B_BAD_VALUE;

	DeviceLocker locker(device);
	status_t error = device->LazyInitDisk();
	if (error != B_OK)
		return error;

	*cookie = device;

	return B_OK;
}


static status_t
remote_disk_close(void* cookie)
{
	TRACE(("remote_disk_close(%p)\n", cookie));

	// nothing to do
	return B_OK;
}


static status_t
remote_disk_read(void* cookie, off_t position, void* buffer, size_t* numBytes)
{
	TRACE(("remote_disk_read(%p, %lld, %p, %lu)\n", cookie, position, buffer,
		*numBytes));

	RemoteDiskDevice* device = (RemoteDiskDevice*)cookie;
	DeviceLocker locker(device);

	ssize_t bytesRead = device->remoteDisk->ReadAt(position, buffer, *numBytes);
	if (bytesRead < 0) {
		*numBytes = 0;
		TRACE(("remote_disk_read() failed: %s\n", strerror(bytesRead)));
		return bytesRead;
	}

	*numBytes = bytesRead;
	TRACE(("remote_disk_read() done: %ld\n", bytesRead));
	return B_OK;
}


static status_t
remote_disk_write(void* cookie, off_t position, const void* buffer,
	size_t* numBytes)
{
	TRACE(("remote_disk_write(%p, %lld, %p, %lu)\n", cookie, position, buffer,
		*numBytes));

	RemoteDiskDevice* device = (RemoteDiskDevice*)cookie;
	DeviceLocker locker(device);

	ssize_t bytesWritten = device->remoteDisk->WriteAt(position, buffer,
		*numBytes);
	if (bytesWritten < 0) {
		*numBytes = 0;
		TRACE(("remote_disk_write() failed: %s\n", strerror(bytesRead)));
		return bytesWritten;
	}

	*numBytes = bytesWritten;
	TRACE(("remote_disk_written() done: %ld\n", bytesWritten));
	return B_OK;
}


static status_t
remote_disk_control(void* cookie, uint32 op, void* arg, size_t len)
{
	TRACE(("remote_disk_control(%p, %lu, %p, %lu)\n", cookie, op, arg, len));

	RemoteDiskDevice* device = (RemoteDiskDevice*)cookie;
	DeviceLocker locker(device);

	// used data device
	switch (op) {
		case B_GET_DEVICE_SIZE:
			TRACE(("remote_disk: B_GET_DEVICE_SIZE\n"));
			*(size_t*)arg = device->remoteDisk->Size();
			return B_OK;

		case B_SET_NONBLOCKING_IO:
			TRACE(("remote_disk: B_SET_NONBLOCKING_IO\n"));
			return B_OK;

		case B_SET_BLOCKING_IO:
			TRACE(("remote_disk: B_SET_BLOCKING_IO\n"));
			return B_OK;

		case B_GET_READ_STATUS:
			TRACE(("remote_disk: B_GET_READ_STATUS\n"));
			*(bool*)arg = true;
			return B_OK;

		case B_GET_WRITE_STATUS:
			TRACE(("remote_disk: B_GET_WRITE_STATUS\n"));
			*(bool*)arg = true;
			return B_OK;

		case B_GET_ICON:
		{
			TRACE(("remote_disk: B_GET_ICON\n"));
			return B_BAD_VALUE;
		}

		case B_GET_BIOS_GEOMETRY:
		case B_GET_GEOMETRY:
		{
			TRACE(("remote_disk: %s\n",
				op == B_GET_BIOS_GEOMETRY ? "B_GET_BIOS_GEOMETRY" : "B_GET_GEOMETRY"));
			if (arg == NULL || len > sizeof(device_geometry))
				return B_BAD_VALUE;

			device_geometry geometry;
			device->GetGeometry(&geometry, op == B_GET_BIOS_GEOMETRY);
			return user_memcpy(arg, &geometry, len);
		}

		case B_GET_MEDIA_STATUS:
			TRACE(("remote_disk: B_GET_MEDIA_STATUS\n"));
			*(status_t*)arg = B_NO_ERROR;
			return B_OK;

		case B_SET_UNINTERRUPTABLE_IO:
			TRACE(("remote_disk: B_SET_UNINTERRUPTABLE_IO\n"));
			return B_OK;

		case B_SET_INTERRUPTABLE_IO:
			TRACE(("remote_disk: B_SET_INTERRUPTABLE_IO\n"));
			return B_OK;

		case B_FLUSH_DRIVE_CACHE:
			TRACE(("remote_disk: B_FLUSH_DRIVE_CACHE\n"));
			return B_OK;

		case B_GET_BIOS_DRIVE_ID:
			TRACE(("remote_disk: B_GET_BIOS_DRIVE_ID\n"));
			*(uint8*)arg = 0xF8;
			return B_OK;

		case B_GET_DRIVER_FOR_DEVICE:
		case B_SET_DEVICE_SIZE:
		case B_SET_PARTITION:
		case B_FORMAT_DEVICE:
		case B_EJECT_DEVICE:
		case B_LOAD_MEDIA:
		case B_GET_NEXT_OPEN_DEVICE:
			TRACE(("remote_disk: another ioctl: %lx (%lu)\n", op, op));
			return B_BAD_VALUE;

		default:
			TRACE(("remote_disk: unknown ioctl: %lx (%lu)\n", op, op));
			return B_BAD_VALUE;
	}
}


static status_t
remote_disk_free(void* cookie)
{
	TRACE(("remote_disk_free(%p)\n", cookie));

	// nothing to do
	return B_OK;
}


static device_hooks sDataDeviceHooks = {
	remote_disk_open,
	remote_disk_close,
	remote_disk_free,
	remote_disk_control,
	remote_disk_read,
	remote_disk_write
};


// #pragma mark - DeviceKeeper API

#include <device_keeper.h>
#include <string.h>

#define RD_DRIVER_MODULE_NAME "drivers/disk/virtual/remote_disk/dk_driver_v1"

static dk_keeper_info *sDeviceKeeper;
static bool sPublished = false;


//	#pragma mark - dk_device_ops


static status_t
rd_open(void *driverCookie, const char *path, int openMode, void **_cookie)
{
	return sDataDeviceHooks.open(path, openMode, _cookie);
}

static status_t
rd_close(void *cookie)
{
	return sDataDeviceHooks.close(cookie);
}

static status_t
rd_free(void *cookie)
{
	return sDataDeviceHooks.free(cookie);
}

static status_t
rd_read(void *cookie, off_t position, void *buffer, size_t *numBytes)
{
	return sDataDeviceHooks.read(cookie, position, buffer, numBytes);
}

static status_t
rd_write(void *cookie, off_t position, const void *buffer, size_t *numBytes)
{
	return sDataDeviceHooks.write(cookie, position, buffer, numBytes);
}

static status_t
rd_control(void *cookie, uint32 op, void *buffer, size_t length)
{
	return sDataDeviceHooks.control(cookie, op, buffer, length);
}


static dk_device_ops sDeviceOps = {
	rd_open,
	rd_close,
	rd_free,
	rd_read,
	rd_write,
	NULL,	// io
	rd_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


//	#pragma mark - dk_driver_info


static const dk_match_rule sRdMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "generic" } },
	{}
};

static const dk_match_dict sRdMatchDict = {
	sRdMatchRules,
	0
};


static float
rd_probe(dk_node *node)
{
	if (sPublished)
		return 0.0;
	return 0.01;
}


static status_t
rd_attach(dk_node *node, void **cookie)
{
	sDevices = new(nothrow) RemoteDiskDevice[MAX_REMOTE_DISKS];
	if (!sDevices)
		return B_NO_MEMORY;

	status_t error = B_OK;
	for (int i = 0; error == B_OK && i < MAX_REMOTE_DISKS; i++)
		error = sDevices[i].Init();
	if (error != B_OK) {
		delete[] sDevices;
		sDevices = NULL;
		return error;
	}

	// Publish devices
	sPublished = true;
	for (int i = 0; kPublishedNames[i] != NULL; i++)
		sDeviceKeeper->publish_device(node, kPublishedNames[i], &sDeviceOps);

	*cookie = node;
	return B_OK;
}


static void
rd_detach(void *_cookie)
{
	delete[] sDevices;
	sPublished = false;
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&sDeviceKeeper },
	{ NULL }
};

struct dk_driver_info sRdDriver = {
	.info	= { RD_DRIVER_MODULE_NAME, 0, NULL },
	.match	= &sRdMatchDict,
	.probe	= rd_probe,
	.attach	= rd_attach,
	.detach	= rd_detach,
	.ops	= &sDeviceOps,
};

module_info *modules[] = {
	(module_info *)&sRdDriver,
	NULL
};

