/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurface.hpp>

#include <Autolock.h>
#include <Debug.h>
#include <Message.h>

#include "KosmSurfacePrivate.hpp"
#include "SurfaceBuffer.hpp"
#include "SurfaceRegistry.hpp"


#define SURFACE_INVALID() (fData == NULL || fData->buffer == NULL)


KosmSurface::KosmSurface()
	:
	fData(new(std::nothrow) Data)
{
	if (fData != NULL)
		fData->buffer = NULL;
}


KosmSurface::~KosmSurface()
{
	delete fData;
}


surface_id
KosmSurface::ID() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->id;
}


uint32
KosmSurface::Width() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->desc.width;
}


uint32
KosmSurface::Height() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->desc.height;
}


pixel_format
KosmSurface::Format() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return PIXEL_FORMAT_BGRA8888;
	return fData->buffer->desc.format;
}


uint32
KosmSurface::BytesPerElement() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->desc.bytesPerElement;
}


uint32
KosmSurface::BytesPerRow() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->desc.bytesPerRow;
}


size_t
KosmSurface::AllocSize() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->allocSize;
}


uint32
KosmSurface::PlaneCount() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->planeCount;
}


uint32
KosmSurface::WidthOfPlane(uint32 planeIndex) const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].width;
}


uint32
KosmSurface::HeightOfPlane(uint32 planeIndex) const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].height;
}


uint32
KosmSurface::BytesPerElementOfPlane(uint32 planeIndex) const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].bytesPerElement;
}


uint32
KosmSurface::BytesPerRowOfPlane(uint32 planeIndex) const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].bytesPerRow;
}


void*
KosmSurface::BaseAddressOfPlane(uint32 planeIndex) const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return NULL;
	if (planeIndex >= fData->buffer->planeCount)
		return NULL;
	if (fData->buffer->lockState == 0)
		return NULL;

	uint8* base = (uint8*)fData->buffer->baseAddress;
	return base + fData->buffer->planes[planeIndex].offset;
}


status_t
KosmSurface::Lock(uint32 options, uint32* outSeed)
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	if (fData->buffer->lockState != 0) {
		if (fData->buffer->lockOwner == find_thread(NULL))
			return B_SURFACE_ALREADY_LOCKED;
		return B_BUSY;
	}

	fData->buffer->lockState = (options & SURFACE_LOCK_READ_ONLY) ? 1 : 2;
	fData->buffer->lockOwner = find_thread(NULL);

	if (outSeed != NULL)
		*outSeed = fData->buffer->seed;

	return B_OK;
}


status_t
KosmSurface::Unlock(uint32 options, uint32* outSeed)
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	if (fData->buffer->lockState == 0)
		return B_SURFACE_NOT_LOCKED;

	if (fData->buffer->lockOwner != find_thread(NULL))
		return B_NOT_ALLOWED;

	if (fData->buffer->lockState == 2)
		fData->buffer->seed++;

	fData->buffer->lockState = 0;
	fData->buffer->lockOwner = -1;

	if (outSeed != NULL)
		*outSeed = fData->buffer->seed;

	return B_OK;
}


void*
KosmSurface::BaseAddress() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return NULL;
	if (fData->buffer->lockState == 0)
		return NULL;
	return fData->buffer->baseAddress;
}


uint32
KosmSurface::Seed() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->seed;
}


void
KosmSurface::IncrementUseCount()
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return;

	BAutolock locker(fData->buffer->lock);
	int32 oldCount = atomic_add(&fData->buffer->localUseCount, 1);
	if (oldCount == 0) {
		SurfaceRegistry::Default()->IncrementGlobalUseCount(
			fData->buffer->id);
	}
}


void
KosmSurface::DecrementUseCount()
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return;

	BAutolock locker(fData->buffer->lock);
	int32 oldCount = atomic_add(&fData->buffer->localUseCount, -1);
	if (oldCount == 1) {
		SurfaceRegistry::Default()->DecrementGlobalUseCount(
			fData->buffer->id);
	}
}


int32
KosmSurface::LocalUseCount() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	return atomic_get(&fData->buffer->localUseCount);
}


bool
KosmSurface::IsInUse() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return false;
	return SurfaceRegistry::Default()->IsInUse(fData->buffer->id);
}


status_t
KosmSurface::SetAttachment(const char* key, const BMessage& value)
{
	if (key == NULL)
		return B_BAD_VALUE;

	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	fData->buffer->attachments.RemoveName(key);

	return fData->buffer->attachments.AddMessage(key, &value);
}


status_t
KosmSurface::GetAttachment(const char* key, BMessage* outValue) const
{
	if (key == NULL || outValue == NULL)
		return B_BAD_VALUE;

	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	return fData->buffer->attachments.FindMessage(key, outValue);
}


status_t
KosmSurface::RemoveAttachment(const char* key)
{
	if (key == NULL)
		return B_BAD_VALUE;

	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	return fData->buffer->attachments.RemoveName(key);
}


status_t
KosmSurface::CopyAllAttachments(BMessage* outValues) const
{
	if (outValues == NULL)
		return B_BAD_VALUE;

	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	*outValues = fData->buffer->attachments;
	return B_OK;
}


status_t
KosmSurface::RemoveAllAttachments()
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	fData->buffer->attachments.MakeEmpty();
	return B_OK;
}


area_id
KosmSurface::Area() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return -1;
	return fData->buffer->areaId;
}


status_t
KosmSurface::SetPurgeable(surface_purgeable_state newState,
	surface_purgeable_state* outOldState)
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	if (outOldState != NULL)
		*outOldState = fData->buffer->purgeableState;

	if (newState == SURFACE_PURGEABLE_KEEP_CURRENT)
		return B_OK;

	surface_purgeable_state oldState = fData->buffer->purgeableState;
	fData->buffer->purgeableState = newState;

	if (newState == SURFACE_PURGEABLE_EMPTY) {
		fData->buffer->contentsPurged = true;
	}

	if (oldState == SURFACE_PURGEABLE_EMPTY || fData->buffer->contentsPurged) {
		if (newState == SURFACE_PURGEABLE_NON_VOLATILE) {
			return B_SURFACE_PURGEABLE;
		}
	}

	return B_OK;
}


bool
KosmSurface::IsVolatile() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return false;
	return fData->buffer->purgeableState == SURFACE_PURGEABLE_VOLATILE;
}


uint32
KosmSurface::Usage() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->desc.usage;
}


bool
KosmSurface::IsLocked() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return false;
	return fData->buffer->lockState != 0;
}


bool
KosmSurface::IsValid() const
{
	return fData != NULL && fData->buffer != NULL
		&& fData->buffer->areaId >= 0;
}


thread_id
KosmSurface::LockOwner() const
{
	ASSERT(!SURFACE_INVALID());
	if (SURFACE_INVALID())
		return -1;
	return fData->buffer->lockOwner;
}
