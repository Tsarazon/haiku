/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "Publisher.h"
#include "Node.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>
#include <util/AutoLock.h>

#include "devfs_private.h"
#include "IORequest.h"


// DkPublishedDevice

DkPublishedDevice::DkPublishedDevice(DkNode* node, dk_device_ops* ops)
	:
	fNode(node),
	fOps(ops)
{
}


DkPublishedDevice::~DkPublishedDevice()
{
}


status_t
DkPublishedDevice::InitCheck() const
{
	return (fNode != NULL && fOps != NULL) ? B_OK : B_NO_INIT;
}


bool DkPublishedDevice::HasSelect() const { return fOps->select != NULL; }
bool DkPublishedDevice::HasDeselect() const { return fOps->deselect != NULL; }
bool DkPublishedDevice::HasRead() const { return fOps->read != NULL || fOps->io != NULL; }
bool DkPublishedDevice::HasWrite() const { return fOps->write != NULL || fOps->io != NULL; }
bool DkPublishedDevice::HasIO() const { return fOps->io != NULL; }


/*!	Emulate synchronous read/write via the asynchronous io() hook.
	Creates a temporary IORequest, submits it through io(), and waits
	for completion. This is the same fallback that Haiku's
	AbstractModuleDevice::_DoIO() provides for drivers that only
	implement the io() hook (e.g. SCSI disk, SCSI CD).
*/
status_t
DkPublishedDevice::_DoIO(void* cookie, off_t pos, void* buffer,
	size_t* _length, bool isWrite)
{
	IORequest request;
	status_t status = request.Init(pos, (addr_t)buffer, *_length, isWrite, 0);
	if (status != B_OK)
		return status;

	status = fOps->io(cookie, &request);
	if (status != B_OK)
		return status;

	status = request.Wait(0, 0);
	*_length = request.TransferredBytes();
	return status;
}


status_t
DkPublishedDevice::Open(const char* path, int openMode, void** _cookie)
{
	if (fOps->open == NULL)
		return B_UNSUPPORTED;
	return fOps->open(fNode->DriverCookie(), path, openMode, _cookie);
}


status_t
DkPublishedDevice::Read(void* cookie, off_t pos, void* buffer,
	size_t* _length)
{
	if (fOps->read != NULL)
		return fOps->read(cookie, pos, buffer, _length);

	// fallback: route through io() if available
	if (fOps->io != NULL)
		return _DoIO(cookie, pos, buffer, _length, false);

	return B_UNSUPPORTED;
}


status_t
DkPublishedDevice::Write(void* cookie, off_t pos, const void* buffer,
	size_t* _length)
{
	if (fOps->write != NULL)
		return fOps->write(cookie, pos, buffer, _length);

	// fallback: route through io() if available
	if (fOps->io != NULL)
		return _DoIO(cookie, pos, const_cast<void*>(buffer), _length, true);

	return B_UNSUPPORTED;
}


status_t
DkPublishedDevice::IO(void* cookie, io_request* request)
{
	if (fOps->io == NULL)
		return B_UNSUPPORTED;
	return fOps->io(cookie, request);
}


status_t
DkPublishedDevice::Control(void* cookie, int32 op, void* buffer,
	size_t length)
{
	if (fOps->control == NULL)
		return B_UNSUPPORTED;
	return fOps->control(cookie, op, buffer, length);
}


status_t
DkPublishedDevice::Select(void* cookie, uint8 event, selectsync* sync)
{
	if (fOps->select == NULL)
		return B_UNSUPPORTED;
	return fOps->select(cookie, event, sync);
}


status_t
DkPublishedDevice::Deselect(void* cookie, uint8 event, selectsync* sync)
{
	if (fOps->deselect == NULL)
		return B_UNSUPPORTED;
	return fOps->deselect(cookie, event, sync);
}


status_t
DkPublishedDevice::Close(void* cookie)
{
	if (fOps->close == NULL)
		return B_OK;
	return fOps->close(cookie);
}


status_t
DkPublishedDevice::Free(void* cookie)
{
	if (fOps->free == NULL)
		return B_OK;
	return fOps->free(cookie);
}


status_t
DkPublishedDevice::InitDevice()
{
	if (fNode != NULL)
		fNode->Acquire();
	return B_OK;
}


void
DkPublishedDevice::UninitDevice()
{
	if (fNode != NULL)
		fNode->Release();
}


void
DkPublishedDevice::Removed()
{
	delete this;
}


void
DkPublishedDevice::NotifyRemoved()
{
	if (fOps != NULL && fOps->device_removed != NULL && fNode != NULL)
		fOps->device_removed(fNode->DriverCookie());
}


// DkPublisher

DkPublisher::DkPublisher()
{
}


DkPublisher::~DkPublisher()
{
}


status_t
DkPublisher::Publish(DkNode* node, const char* path, dk_device_ops* ops)
{
	if (node == NULL || path == NULL || ops == NULL)
		return B_BAD_VALUE;

	DkPublishedDevice* device = new(std::nothrow) DkPublishedDevice(node, ops);
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = device->InitCheck();
	if (status == B_OK)
		status = devfs_publish_device(path, device);
	if (status != B_OK) {
		delete device;
		return status;
	}

	{
		MutexLocker nodeLocker(node->NodeLock());
		status = node->AddPublishedPath(path);
	}

	if (status != B_OK) {
		// can't track the path — unpublish to avoid an untracked
		// device that UnpublishAll/NotifyDeviceRemoved would miss
		BaseDevice* baseDevice;
		if (devfs_get_device(path, baseDevice) == B_OK) {
			devfs_unpublish_device(baseDevice, true);
			devfs_put_device(baseDevice);
		}
		return status;
	}

	node->NotifyWatchers(KOSM_EVENT_DEVICE_PUBLISHED,
		reinterpret_cast<dk_node*>(node), path);

	return B_OK;
}


status_t
DkPublisher::Unpublish(DkNode* node, const char* path)
{
	if (path == NULL)
		return B_BAD_VALUE;

	BaseDevice* device;
	status_t status = devfs_get_device(path, device);
	if (status != B_OK)
		return status;

	status = devfs_unpublish_device(device, true);
	devfs_put_device(device);

	if (node != NULL) {
		node->NotifyWatchers(KOSM_EVENT_DEVICE_UNPUBLISHED,
			reinterpret_cast<dk_node*>(node), path);

		MutexLocker nodeLocker(node->NodeLock());
		node->RemovePublishedPath(path);
	}

	return status;
}


void
DkPublisher::UnpublishAll(DkNode* node)
{
	// iterate tracked paths and unpublish each
	while (true) {
		MutexLocker nodeLocker(node->NodeLock());
		const char* path = node->FirstPublishedPath();
		if (path == NULL)
			break;

		// copy path before releasing lock (RemovePublishedPath frees it)
		char pathBuf[B_PATH_NAME_LENGTH];
		strlcpy(pathBuf, path, sizeof(pathBuf));
		node->RemovePublishedPath(path);
		nodeLocker.Unlock();

		BaseDevice* device;
		if (devfs_get_device(pathBuf, device) == B_OK) {
			devfs_unpublish_device(device, true);
			devfs_put_device(device);
		}
	}
}


void
DkPublisher::NotifyDeviceRemoved(DkNode* node)
{
	// snapshot paths under node lock, invoke callbacks without lock
	static const int32 kStackMax = 4;
	char* stackBuf[kStackMax];
	char** paths = stackBuf;
	int32 count = 0;

	{
		MutexLocker nodeLocker(node->NodeLock());

		count = node->CountPublishedPaths();
		if (count == 0)
			return;

		if (count > kStackMax) {
			paths = (char**)malloc(count * sizeof(char*));
			if (paths == NULL)
				return;
		}

		const DkPublishedPathList& pathList = node->PublishedPaths();
		DkPublishedPathList::ConstIterator it = pathList.GetIterator();
		int32 i = 0;
		while (it.HasNext() && i < count) {
			paths[i] = strdup(it.Next()->path);
			if (paths[i] == NULL) {
				for (int32 j = 0; j < i; j++)
					free(paths[j]);
				if (paths != stackBuf)
					free(paths);
				return;
			}
			i++;
		}
		count = i;
	}

	for (int32 i = 0; i < count; i++) {
		BaseDevice* baseDevice;
		if (devfs_get_device(paths[i], baseDevice) == B_OK) {
			baseDevice->NotifyRemoved();
			devfs_put_device(baseDevice);
		}
		free(paths[i]);
	}

	if (paths != stackBuf)
		free(paths);
}
