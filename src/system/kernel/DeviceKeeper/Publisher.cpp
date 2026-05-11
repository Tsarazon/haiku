/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */

#include "Publisher.h"
#include "DeviceKeeper.h"
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
	fOps(ops),
	fMaxIOSize(0)
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


/*!	Read DMA transfer limits from the node's property chain.

	Block device drivers (AHCI, virtio-blk, NVMe) declare their DMA
	constraints as node properties. The bus manager typically sets
	KOSM_DMA_MAX_TRANSFER_BLOCKS on the bus node, and recursive
	property lookup finds it from the device node.

	We read max_transfer_size here so that _DoIO() and IO() can
	pre-split requests to a size the driver can handle in a single
	DMA transfer. Without this, the driver receives a multi-MB
	request and may only transfer a fraction, causing short reads
	that crash packagefs and other consumers.
*/
void
DkPublishedDevice::_ReadDMALimits()
{
	fMaxIOSize = 0;

	if (fNode == NULL || fOps->io == NULL)
		return;

	DkKeeper* keeper = DkKeeper::Instance();
	if (keeper == NULL)
		return;

	// DMA properties are typically on the bus node (PCI, USB),
	// not the device node itself. Recursive lookup walks the
	// parent chain to find them.
	ReadLocker treeLocker(keeper->TreeLock());

	// Block size for interpreting *_BLOCKS properties.
	// Read from KOSM_DMA_BLOCK_SIZE if the driver set it;
	// fall back to 512 (conservative — safe for 4K-native too,
	// just chunks smaller than necessary).
	generic_size_t blockSize = 512;
	const DkPropertyEntry* bsEntry = fNode->LookupRecursive(
		KOSM_DMA_BLOCK_SIZE);
	if (bsEntry != NULL && bsEntry->type == B_UINT32_TYPE
		&& bsEntry->value.ui32 > 0) {
		blockSize = bsEntry->value.ui32;
	}

	const DkPropertyEntry* entry = fNode->LookupRecursive(
		KOSM_DMA_MAX_TRANSFER_BLOCKS);
	if (entry != NULL && entry->type == B_UINT32_TYPE
		&& entry->value.ui32 > 0) {
		fMaxIOSize = (generic_size_t)entry->value.ui32 * blockSize;
	}

	// If no DMA limit found, also check max_segment properties
	// as a fallback indicator of device constraints.
	if (fMaxIOSize == 0) {
		entry = fNode->LookupRecursive(KOSM_DMA_MAX_SEGMENT_BLOCKS);
		if (entry != NULL && entry->type == B_UINT32_TYPE
			&& entry->value.ui32 > 0) {
			uint32 maxSegments = 16;
			const DkPropertyEntry* segEntry = fNode->LookupRecursive(
				KOSM_DMA_MAX_SEGMENT_COUNT);
			if (segEntry != NULL && segEntry->type == B_UINT32_TYPE
				&& segEntry->value.ui32 > 0)
				maxSegments = segEntry->value.ui32;

			fMaxIOSize = (generic_size_t)entry->value.ui32
				* blockSize * maxSegments;
		}
	}

	if (fMaxIOSize > 0) {
		DK_TRACE("%s DMA max I/O size: %" B_PRIuGENADDR
			" bytes (block size %" B_PRIuGENADDR ")\n",
			fNode->ModuleName(), fMaxIOSize, blockSize);
	}
}


/*!	Emulate synchronous read/write via the asynchronous io() hook.

	DMA-aware chunking: if the node declares DMA transfer limits
	(KOSM_DMA_MAX_TRANSFER_BLOCKS), we pre-split the request into
	chunks the driver's DMA engine can handle. Each chunk is a
	separate IORequest submitted, waited, and accumulated.

	Safety-net loop: even within a single chunk, the driver may
	return a partial transfer (e.g. end-of-device, segment limits).
	We loop on partial completion to ensure the full request is
	satisfied. This is the same semantics as POSIX read/pread.

	devfs_read_pages() calls device->Read() per iovec. A single
	iovec can span many contiguous pages (64KB, 256KB, etc.), so
	this function is the critical path for large file cache reads
	such as packagefs loading .hpkg archives.
*/
status_t
DkPublishedDevice::_DoIO(void* cookie, off_t pos, void* buffer,
	size_t* _length, bool isWrite)
{
	const size_t totalLength = *_length;
	size_t totalTransferred = 0;

	// Pre-split by DMA limit when known, otherwise full size
	// (driver reports partial, loop handles it).
	const size_t maxChunk = fMaxIOSize > 0
		? (size_t)fMaxIOSize : totalLength;

	while (totalTransferred < totalLength) {
		const size_t remaining = totalLength - totalTransferred;
		const size_t chunkSize = remaining < maxChunk
			? remaining : maxChunk;

		IORequest request;
		status_t status = request.Init(
			pos + (off_t)totalTransferred,
			(addr_t)buffer + totalTransferred,
			chunkSize, isWrite, 0);
		if (status != B_OK) {
			dprintf("_DoIO: %s IORequest::Init failed for %zu byte "
				"chunk at offset %" B_PRIdOFF ": %s "
				"(transferred so far %zu/%zu)\n",
				isWrite ? "write" : "read", chunkSize,
				pos + (off_t)totalTransferred, strerror(status),
				totalTransferred, totalLength);
			*_length = totalTransferred;
			return totalTransferred > 0 ? B_OK : status;
		}

		status = fOps->io(cookie, &request);
		if (status != B_OK) {
			dprintf("_DoIO: %s fOps->io failed for %zu byte chunk "
				"at offset %" B_PRIdOFF ": %s "
				"(transferred so far %zu/%zu)\n",
				isWrite ? "write" : "read", chunkSize,
				pos + (off_t)totalTransferred, strerror(status),
				totalTransferred, totalLength);
			*_length = totalTransferred;
			return totalTransferred > 0 ? B_OK : status;
		}

		status = request.Wait(0, 0);
		generic_size_t chunk = request.TransferredBytes();

		// Drivers that bypass IOScheduler (SCSI disk -> AHCI DMA path)
		// transfer data directly via DMA into the buffer but never
		// call IORequest::Advance(). TransferredBytes() stays 0 even
		// though the data is already in the buffer. If Wait() returned
		// B_OK, the driver completed successfully — trust it and
		// account for the full chunk.
		if (status == B_OK && chunk == 0) {
			DK_TRACE("_DoIO: driver returned B_OK with "
				"0 TransferredBytes for %zu byte chunk at offset "
				"%" B_PRIdOFF " — assuming full transfer\n",
				chunkSize, pos + (off_t)totalTransferred);
			chunk = chunkSize;
		}

		if (chunk == 0) {
			// No progress and error — end of device or hard error.
			dprintf("_DoIO: %s Wait failed with no progress for %zu "
				"byte chunk at offset %" B_PRIdOFF ": %s "
				"(transferred so far %zu/%zu)\n",
				isWrite ? "write" : "read", chunkSize,
				pos + (off_t)totalTransferred, strerror(status),
				totalTransferred, totalLength);
			*_length = totalTransferred;
			return totalTransferred > 0 ? B_OK : status;
		}

		totalTransferred += chunk;

		if (status != B_OK) {
			// Error after partial progress — return what we have.
			dprintf("_DoIO: %s Wait failed after partial transfer of "
				"%" B_PRIuGENADDR " bytes (chunk %zu, offset "
				"%" B_PRIdOFF "): %s "
				"(total transferred %zu/%zu)\n",
				isWrite ? "write" : "read", chunk, chunkSize,
				pos + (off_t)(totalTransferred - chunk),
				strerror(status), totalTransferred, totalLength);
			*_length = totalTransferred;
			return B_OK;
		}
	}

	*_length = totalTransferred;
	return B_OK;
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
	// io()-only drivers: use _DoIO which has its own DMA-aware loop.
	if (fOps->read == NULL) {
		if (fOps->io != NULL)
			return _DoIO(cookie, pos, buffer, _length, false);
		return B_UNSUPPORTED;
	}

	// Direct read hook: the driver (scsi_disk, etc.) may return a
	// partial transfer if the underlying transport limits the request
	// size (e.g. SCSI maxBlocks = 255 sectors via AHCI). Loop until
	// the full request is satisfied. Without this loop, devfs_read_pages
	// sees a short read, breaks its iovec iteration, and the file cache
	// returns pages filled with freed-memory patterns (0xDEAD) —
	// causing "Data read partially" in packagefs.
	const size_t totalLength = *_length;
	size_t totalTransferred = 0;

	while (totalTransferred < totalLength) {
		size_t remaining = totalLength - totalTransferred;
		status_t status = fOps->read(cookie, pos + (off_t)totalTransferred,
			(uint8*)buffer + totalTransferred, &remaining);

		if (remaining == 0) {
			*_length = totalTransferred;
			return totalTransferred > 0 ? B_OK : status;
		}

		totalTransferred += remaining;

		if (status != B_OK) {
			*_length = totalTransferred;
			return totalTransferred > 0 ? B_OK : status;
		}
	}

	*_length = totalTransferred;
	return B_OK;
}


status_t
DkPublishedDevice::Write(void* cookie, off_t pos, const void* buffer,
	size_t* _length)
{
	if (fOps->write == NULL) {
		if (fOps->io != NULL)
			return _DoIO(cookie, pos, const_cast<void*>(buffer), _length, true);
		return B_UNSUPPORTED;
	}

	const size_t totalLength = *_length;
	size_t totalTransferred = 0;

	while (totalTransferred < totalLength) {
		size_t remaining = totalLength - totalTransferred;
		status_t status = fOps->write(cookie,
			pos + (off_t)totalTransferred,
			(const uint8*)buffer + totalTransferred, &remaining);

		if (remaining == 0) {
			dprintf("DkPublishedDevice::Write: fOps->write made no "
				"progress at offset %" B_PRIdOFF " (status=%s, "
				"transferred so far %zu/%zu)\n",
				pos + (off_t)totalTransferred, strerror(status),
				totalTransferred, totalLength);
			*_length = totalTransferred;
			return totalTransferred > 0 ? B_OK : status;
		}

		totalTransferred += remaining;

		if (status != B_OK) {
			dprintf("DkPublishedDevice::Write: fOps->write returned "
				"%s after partial transfer of %zu bytes at offset "
				"%" B_PRIdOFF " (total transferred %zu/%zu)\n",
				strerror(status), remaining,
				pos + (off_t)(totalTransferred - remaining),
				totalTransferred, totalLength);
			*_length = totalTransferred;
			return totalTransferred > 0 ? B_OK : status;
		}
	}

	*_length = totalTransferred;
	return B_OK;
}


status_t
DkPublishedDevice::IO(void* cookie, io_request* request)
{
	if (fOps->io == NULL)
		return B_UNSUPPORTED;

	// Pass through directly when no DMA limits or request fits.
	if (fMaxIOSize == 0 || request->Length() <= fMaxIOSize)
		return fOps->io(cookie, request);

	// Request exceeds DMA transfer limit — split into sub-requests.
	return _SplitIO(cookie, request);
}


/*!	Split a large io_request into DMA-sized sub-requests.

	Used by IO() when the request exceeds fMaxIOSize.

	Two-phase approach to prevent premature parent notification:

	Phase 1: Create ALL sub-requests up front. Each CreateSubRequest()
	increments the parent's fPendingChildren under its lock. After
	Phase 1, fPendingChildren equals the total sub-request count.

	Phase 2: Submit sub-requests to the driver. Even if the first sub
	completes synchronously (driver returns, IRQ fires before next
	io() call), SubRequestFinished only decrements fPendingChildren
	from N to N-1. The parent's NotifyFinished cannot fire until ALL
	subs complete.

	This avoids the race inherent in interleaved create-submit loops
	where a fast completion could drop fPendingChildren to 0 between
	the first io() return and the second CreateSubRequest().
*/
status_t
DkPublishedDevice::_SplitIO(void* cookie, io_request* request)
{
	const off_t baseOffset = request->Offset();
	const generic_size_t totalLength = request->Length();
	off_t currentOffset = baseOffset;
	generic_size_t remaining = totalLength;

	// Phase 1: create all sub-requests.
	// Use stack array for typical cases, heap for pathological ones.
	static const int32 kStackMax = 32;
	IORequest* stackSubs[kStackMax];
	IORequest** subs = stackSubs;
	int32 subCount = 0;

	int32 estimatedCount = (int32)((remaining + fMaxIOSize - 1)
		/ fMaxIOSize);
	if (estimatedCount > kStackMax) {
		subs = (IORequest**)malloc(estimatedCount * sizeof(IORequest*));
		if (subs == NULL) {
			request->SetStatusAndNotify(B_NO_MEMORY);
			return B_OK;
		}
	}

	int32 subCapacity = (subs == stackSubs) ? kStackMax : estimatedCount;

	while (remaining > 0 && subCount < subCapacity) {
		generic_size_t chunkSize = remaining < fMaxIOSize
			? remaining : fMaxIOSize;

		IORequest* sub;
		status_t status = request->CreateSubRequest(
			currentOffset,	// parentOffset (absolute device offset)
			currentOffset,	// sub-request device offset
			chunkSize,
			sub);
		if (status != B_OK)
			break;

		subs[subCount++] = sub;
		currentOffset += chunkSize;
		remaining -= chunkSize;
	}

	if (subCount == 0) {
		// Could not create any sub-requests.
		if (subs != stackSubs)
			free(subs);
		request->SetStatusAndNotify(B_NO_MEMORY);
		return B_OK;
	}

	// Phase 2: submit all sub-requests. fPendingChildren is already
	// at the final count, so no single completion can trigger
	// premature parent notification.
	for (int32 i = 0; i < subCount; i++) {
		status_t status = fOps->io(cookie, subs[i]);
		if (status != B_OK) {
			// Fail this sub and all remaining unsubmitted subs so
			// their SubRequestFinished() calls still decrement
			// fPendingChildren, allowing the parent to eventually
			// reach completion (with error status from the failing sub).
			for (int32 j = i; j < subCount; j++)
				subs[j]->SetStatusAndNotify(status);
			break;
		}
	}

	if (subs != stackSubs)
		free(subs);

	// Return B_OK: completion is asynchronous via sub-requests.
	// Any errors are reported through the request's notification
	// mechanism, not the return value.
	return B_OK;
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
	if (fNode != NULL) {
		fNode->Acquire();
		_ReadDMALimits();
	}
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
