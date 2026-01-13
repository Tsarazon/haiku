/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurface.h>

#include <Autolock.h>
#include <Message.h>

#include "KosmSurfacePrivate.h"
#include "SurfaceBuffer.h"
#include "SurfaceRegistry.h"


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
	return fData->buffer->id;
}


uint32
KosmSurface::Width() const
{
	return fData->buffer->desc.width;
}


uint32
KosmSurface::Height() const
{
	return fData->buffer->desc.height;
}


pixel_format
KosmSurface::Format() const
{
	return fData->buffer->desc.format;
}


uint32
KosmSurface::BytesPerElement() const
{
	return fData->buffer->desc.bytesPerElement;
}


uint32
KosmSurface::BytesPerRow() const
{
	return fData->buffer->desc.bytesPerRow;
}


size_t
KosmSurface::AllocSize() const
{
	return fData->buffer->allocSize;
}


uint32
KosmSurface::PlaneCount() const
{
	return fData->buffer->planeCount;
}


uint32
KosmSurface::WidthOfPlane(uint32 planeIndex) const
{
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].width;
}


uint32
KosmSurface::HeightOfPlane(uint32 planeIndex) const
{
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].height;
}


uint32
KosmSurface::BytesPerElementOfPlane(uint32 planeIndex) const
{
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].bytesPerElement;
}


uint32
KosmSurface::BytesPerRowOfPlane(uint32 planeIndex) const
{
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].bytesPerRow;
}


void*
KosmSurface::BaseAddressOfPlane(uint32 planeIndex) const
{
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
	if (fData->buffer->lockState == 0)
		return NULL;
	return fData->buffer->baseAddress;
}


uint32
KosmSurface::Seed() const
{
	return fData->buffer->seed;
}


void
KosmSurface::IncrementUseCount()
{
	int32 oldCount = atomic_add(&fData->buffer->localUseCount, 1);
	if (oldCount == 0) {
		SurfaceRegistry::Default()->IncrementGlobalUseCount(
			fData->buffer->id);
	}
}


void
KosmSurface::DecrementUseCount()
{
	int32 oldCount = atomic_add(&fData->buffer->localUseCount, -1);
	if (oldCount == 1) {
		SurfaceRegistry::Default()->DecrementGlobalUseCount(
			fData->buffer->id);
	}
}


int32
KosmSurface::LocalUseCount() const
{
	return atomic_get(&fData->buffer->localUseCount);
}


bool
KosmSurface::IsInUse() const
{
	return SurfaceRegistry::Default()->IsInUse(fData->buffer->id);
}


status_t
KosmSurface::SetAttachment(const char* key, const BMessage& value)
{
	if (key == NULL)
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

	BAutolock locker(fData->buffer->lock);

	return fData->buffer->attachments.FindMessage(key, outValue);
}


status_t
KosmSurface::RemoveAttachment(const char* key)
{
	if (key == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	return fData->buffer->attachments.RemoveName(key);
}


status_t
KosmSurface::CopyAllAttachments(BMessage* outValues) const
{
	if (outValues == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	*outValues = fData->buffer->attachments;
	return B_OK;
}


status_t
KosmSurface::RemoveAllAttachments()
{
	BAutolock locker(fData->buffer->lock);

	fData->buffer->attachments.MakeEmpty();
	return B_OK;
}


area_id
KosmSurface::Area() const
{
	return fData->buffer->areaId;
}
