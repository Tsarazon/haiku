/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurface.hpp>

#include <Autolock.h>
#include <Debug.h>
#include <Message.h>

#include <new>

#include "SurfaceBuffer.hpp"
#include "SurfaceRegistry.hpp"
#include "PlanarLayout.hpp"


struct KosmSurface::Data {
	SurfaceBuffer*	buffer;
};


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
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->id;
}


uint32
KosmSurface::Width() const
{
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->desc.width;
}


uint32
KosmSurface::Height() const
{
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->desc.height;
}


pixel_format
KosmSurface::Format() const
{
	if (SURFACE_INVALID())
		return PIXEL_FORMAT_ARGB8888;
	return fData->buffer->desc.format;
}


uint32
KosmSurface::BytesPerElement() const
{
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->desc.bytesPerElement;
}


uint32
KosmSurface::BytesPerRow() const
{
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->desc.bytesPerRow;
}


size_t
KosmSurface::AllocSize() const
{
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->allocSize;
}


uint32
KosmSurface::PlaneCount() const
{
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->planeCount;
}


uint32
KosmSurface::WidthOfPlane(uint32 planeIndex) const
{
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].width;
}


uint32
KosmSurface::HeightOfPlane(uint32 planeIndex) const
{
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].height;
}


uint32
KosmSurface::BytesPerElementOfPlane(uint32 planeIndex) const
{
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].bytesPerElement;
}


uint32
KosmSurface::BytesPerRowOfPlane(uint32 planeIndex) const
{
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return fData->buffer->planes[planeIndex].bytesPerRow;
}


void*
KosmSurface::BaseAddressOfPlane(uint32 planeIndex) const
{
	if (SURFACE_INVALID())
		return NULL;
	if (planeIndex >= fData->buffer->planeCount)
		return NULL;
	if (fData->buffer->lockCount == 0)
		return NULL;

	uint8* base = (uint8*)fData->buffer->baseAddress;
	return base + fData->buffer->planes[planeIndex].offset;
}


uint32
KosmSurface::ComponentCountOfPlane(uint32 planeIndex) const
{
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return planar_get_component_count(fData->buffer->desc.format, planeIndex);
}


uint32
KosmSurface::BitDepthOfComponentOfPlane(uint32 planeIndex,
	uint32 componentIndex) const
{
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return planar_get_bit_depth(fData->buffer->desc.format, planeIndex,
		componentIndex);
}


uint32
KosmSurface::BitOffsetOfComponentOfPlane(uint32 planeIndex,
	uint32 componentIndex) const
{
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return planar_get_bit_offset(fData->buffer->desc.format, planeIndex,
		componentIndex);
}


status_t
KosmSurface::Lock(uint32 options, uint32* outSeed)
{
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	thread_id currentThread = find_thread(NULL);
	bool readOnly = (options & SURFACE_LOCK_READ_ONLY) != 0;

	if (fData->buffer->lockCount > 0) {
		if (fData->buffer->lockOwner != currentThread)
			return B_BUSY;

		if (!fData->buffer->lockedReadOnly && readOnly) {
			// Downgrade not allowed
		} else if (fData->buffer->lockedReadOnly && !readOnly) {
			return B_NOT_ALLOWED;
		}

		fData->buffer->lockCount++;
	} else {
		fData->buffer->lockCount = 1;
		fData->buffer->lockOwner = currentThread;
		fData->buffer->lockedReadOnly = readOnly;
	}

	if (outSeed != NULL)
		*outSeed = fData->buffer->seed;

	return B_OK;
}


status_t
KosmSurface::Unlock(uint32 options, uint32* outSeed)
{
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	if (fData->buffer->lockCount == 0)
		return B_SURFACE_NOT_LOCKED;

	if (fData->buffer->lockOwner != find_thread(NULL))
		return B_NOT_ALLOWED;

	fData->buffer->lockCount--;

	if (fData->buffer->lockCount == 0) {
		if (!fData->buffer->lockedReadOnly)
			fData->buffer->seed++;

		fData->buffer->lockOwner = -1;
		fData->buffer->lockedReadOnly = false;
	}

	if (outSeed != NULL)
		*outSeed = fData->buffer->seed;

	return B_OK;
}


void*
KosmSurface::BaseAddress() const
{
	if (SURFACE_INVALID())
		return NULL;
	if (fData->buffer->lockCount == 0)
		return NULL;
	return fData->buffer->baseAddress;
}


uint32
KosmSurface::Seed() const
{
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->seed;
}


void
KosmSurface::IncrementUseCount()
{
	if (SURFACE_INVALID())
		return;

	BAutolock locker(fData->buffer->lock);

	if (fData->buffer->localUseCount == 0) {
		SurfaceRegistry::Default()->IncrementGlobalUseCount(
			fData->buffer->id);
	}
	fData->buffer->localUseCount++;
}


void
KosmSurface::DecrementUseCount()
{
	if (SURFACE_INVALID())
		return;

	BAutolock locker(fData->buffer->lock);

	if (fData->buffer->localUseCount <= 0)
		return;

	fData->buffer->localUseCount--;
	if (fData->buffer->localUseCount == 0) {
		SurfaceRegistry::Default()->DecrementGlobalUseCount(
			fData->buffer->id);
	}
}


int32
KosmSurface::LocalUseCount() const
{
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->localUseCount;
}


bool
KosmSurface::IsInUse() const
{
	if (SURFACE_INVALID())
		return false;
	return SurfaceRegistry::Default()->IsInUse(fData->buffer->id);
}


status_t
KosmSurface::SetAttachment(const char* key, const BMessage& value)
{
	if (key == NULL)
		return B_BAD_VALUE;

	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	fData->buffer->attachments.RemoveName(key);
	return fData->buffer->attachments.AddMessage(key, &value);
}


status_t
KosmSurface::SetAttachments(const BMessage& values)
{
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	char* name;
	type_code type;
	int32 count;

	for (int32 i = 0; values.GetInfo(B_MESSAGE_TYPE, i, &name, &type, &count)
			== B_OK; i++) {
		BMessage value;
		if (values.FindMessage(name, &value) == B_OK) {
			fData->buffer->attachments.RemoveName(name);
			fData->buffer->attachments.AddMessage(name, &value);
		}
	}

	return B_OK;
}


status_t
KosmSurface::GetAttachment(const char* key, BMessage* outValue) const
{
	if (key == NULL || outValue == NULL)
		return B_BAD_VALUE;

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

	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);
	*outValues = fData->buffer->attachments;
	return B_OK;
}


status_t
KosmSurface::RemoveAllAttachments()
{
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);
	fData->buffer->attachments.MakeEmpty();
	return B_OK;
}


status_t
KosmSurface::SetPurgeable(surface_purgeable_state newState,
	surface_purgeable_state* outOldState)
{
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	surface_purgeable_state oldState = fData->buffer->purgeableState;

	if (outOldState != NULL)
		*outOldState = oldState;

	if (newState == SURFACE_PURGEABLE_KEEP_CURRENT)
		return B_OK;

	fData->buffer->purgeableState = newState;

	if (newState == SURFACE_PURGEABLE_EMPTY)
		fData->buffer->contentsPurged = true;

	if (fData->buffer->contentsPurged
		&& newState == SURFACE_PURGEABLE_NON_VOLATILE) {
		return B_SURFACE_PURGED;
	}

	return B_OK;
}


bool
KosmSurface::IsVolatile() const
{
	if (SURFACE_INVALID())
		return false;
	return fData->buffer->purgeableState == SURFACE_PURGEABLE_VOLATILE;
}


uint32
KosmSurface::Usage() const
{
	if (SURFACE_INVALID())
		return 0;
	return fData->buffer->desc.usage;
}


bool
KosmSurface::IsLocked() const
{
	if (SURFACE_INVALID())
		return false;
	return fData->buffer->lockCount > 0;
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
	if (SURFACE_INVALID())
		return -1;
	return fData->buffer->lockOwner;
}


status_t
KosmSurface::CreateAccessToken(surface_token* outToken)
{
	if (SURFACE_INVALID())
		return B_NO_INIT;
	if (outToken == NULL)
		return B_BAD_VALUE;

	return SurfaceRegistry::Default()->CreateAccessToken(
		fData->buffer->id, outToken);
}


status_t
KosmSurface::RevokeAllAccess()
{
	if (SURFACE_INVALID())
		return B_NO_INIT;

	return SurfaceRegistry::Default()->RevokeAllAccess(fData->buffer->id);
}
