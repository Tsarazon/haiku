/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurface.hpp>

#include <Autolock.h>
#include <Debug.h>
#include <Message.h>

#include <new>

#include "KosmSurfacePrivate.hpp"
#include "SurfaceRegistry.hpp"
#include "PlanarLayout.hpp"


#define SURFACE_INVALID() (fData == nullptr || fData->buffer == nullptr)

// All accesses to lockCount and seed MUST go through atomic builtins.
// Reads outside BLocker use acquire; reads under BLocker use relaxed
// (the lock already provides ordering); writes under BLocker use
// release so that unlocked readers see a consistent value.
// On x86 (TSO) all variants compile to plain loads/stores.
// On ARM64 acquire emits ldar, release emits stlr.

#define LOCK_COUNT_LOAD(buf) \
	__atomic_load_n(&(buf)->lockCount, __ATOMIC_ACQUIRE)

#define LOCK_COUNT_LOAD_LOCKED(buf) \
	__atomic_load_n(&(buf)->lockCount, __ATOMIC_RELAXED)

#define LOCK_COUNT_STORE(buf, val) \
	__atomic_store_n(&(buf)->lockCount, (val), __ATOMIC_RELEASE)

#define SEED_LOAD(buf) \
	__atomic_load_n(&(buf)->seed, __ATOMIC_ACQUIRE)

#define SEED_LOAD_LOCKED(buf) \
	__atomic_load_n(&(buf)->seed, __ATOMIC_RELAXED)

#define SEED_STORE(buf, val) \
	__atomic_store_n(&(buf)->seed, (val), __ATOMIC_RELEASE)


KosmSurface::KosmSurface()
	:
	fData(new(std::nothrow) Data)
{
	if (fData != nullptr)
		fData->buffer = nullptr;
}


KosmSurface::~KosmSurface()
{
	delete fData;
}


kosm_surface_id
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


kosm_pixel_format
KosmSurface::Format() const
{
	if (SURFACE_INVALID())
		return KOSM_PIXEL_FORMAT_ARGB8888;
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
		return nullptr;
	if (planeIndex >= fData->buffer->planeCount)
		return nullptr;
	if (LOCK_COUNT_LOAD(fData->buffer) == 0)
		return nullptr;

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
	return kosm_planar_get_component_count(fData->buffer->desc.format,
		planeIndex);
}


uint32
KosmSurface::BitDepthOfComponentOfPlane(uint32 planeIndex,
	uint32 componentIndex) const
{
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return kosm_planar_get_bit_depth(fData->buffer->desc.format,
		planeIndex, componentIndex);
}


uint32
KosmSurface::BitOffsetOfComponentOfPlane(uint32 planeIndex,
	uint32 componentIndex) const
{
	if (SURFACE_INVALID())
		return 0;
	if (planeIndex >= fData->buffer->planeCount)
		return 0;
	return kosm_planar_get_bit_offset(fData->buffer->desc.format,
		planeIndex, componentIndex);
}


status_t
KosmSurface::Lock(uint32 options, uint32* outSeed)
{
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	thread_id currentThread = find_thread(nullptr);
	bool readOnly = (options & KOSM_SURFACE_LOCK_READ_ONLY) != 0;
	int32 lc = LOCK_COUNT_LOAD_LOCKED(fData->buffer);

	if (lc > 0) {
		if (fData->buffer->lockOwner != currentThread)
			return B_BUSY;

		if (!fData->buffer->lockedReadOnly && readOnly) {
			// Downgrade not allowed
		} else if (fData->buffer->lockedReadOnly && !readOnly) {
			return B_NOT_ALLOWED;
		}

		LOCK_COUNT_STORE(fData->buffer, lc + 1);
	} else {
		LOCK_COUNT_STORE(fData->buffer, 1);
		fData->buffer->lockOwner = currentThread;
		fData->buffer->lockedReadOnly = readOnly;
	}

	if (outSeed != nullptr)
		*outSeed = SEED_LOAD_LOCKED(fData->buffer);

	return B_OK;
}


status_t
KosmSurface::LockWithTimeout(bigtime_t timeout, uint32 options,
	uint32* outSeed)
{
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	// Lazy semaphore creation. CAS avoids double-create when
	// multiple threads enter LockWithTimeout concurrently.
	if (__atomic_load_n(&fData->buffer->waitSem, __ATOMIC_ACQUIRE) < 0) {
		sem_id sem = create_sem(0, "kosm_surface_wait");
		if (sem >= 0) {
			sem_id expected = -1;
			if (!__atomic_compare_exchange_n(&fData->buffer->waitSem,
					&expected, sem, false,
					__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
				delete_sem(sem);
			}
		}
	}

	bigtime_t deadline = system_time() + timeout;

	for (;;) {
		status_t status = Lock(options, outSeed);
		if (status != B_BUSY)
			return status;

		bigtime_t remaining = deadline - system_time();
		if (remaining <= 0)
			return B_TIMED_OUT;

		sem_id sem = __atomic_load_n(&fData->buffer->waitSem,
			__ATOMIC_ACQUIRE);
		if (sem < 0)
			return B_NO_MEMORY;

		status = acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, remaining);

		if (status == B_TIMED_OUT)
			return B_TIMED_OUT;
	}
}


status_t
KosmSurface::Unlock(uint32 options, uint32* outSeed)
{
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	int32 lc = LOCK_COUNT_LOAD_LOCKED(fData->buffer);

	if (lc == 0)
		return KOSM_SURFACE_NOT_LOCKED;

	if (fData->buffer->lockOwner != find_thread(nullptr))
		return B_NOT_ALLOWED;

	lc--;
	LOCK_COUNT_STORE(fData->buffer, lc);

	if (lc == 0) {
		if (!fData->buffer->lockedReadOnly) {
			uint32 s = SEED_LOAD_LOCKED(fData->buffer);
			SEED_STORE(fData->buffer, s + 1);

			// A write cycle means valid content was produced.
			// Clear the purged flag so subsequent NON_VOLATILE
			// transitions don't falsely report KOSM_SURFACE_PURGED.
			fData->buffer->contentsPurged = false;
		}

		fData->buffer->lockOwner = -1;
		fData->buffer->lockedReadOnly = false;

		// Wake one waiter, not all. Avoids thundering herd when
		// many threads contend on the same surface.
		sem_id sem = __atomic_load_n(&fData->buffer->waitSem,
			__ATOMIC_ACQUIRE);
		if (sem >= 0)
			release_sem(sem);
	}

	if (outSeed != nullptr)
		*outSeed = SEED_LOAD_LOCKED(fData->buffer);

	return B_OK;
}


void*
KosmSurface::BaseAddress() const
{
	if (SURFACE_INVALID())
		return nullptr;
	if (LOCK_COUNT_LOAD(fData->buffer) == 0)
		return nullptr;
	return fData->buffer->baseAddress;
}


uint32
KosmSurface::Seed() const
{
	if (SURFACE_INVALID())
		return 0;
	return SEED_LOAD(fData->buffer);
}


void
KosmSurface::IncrementUseCount()
{
	if (SURFACE_INVALID())
		return;

	BAutolock locker(fData->buffer->lock);

	if (fData->buffer->localUseCount == 0) {
		KosmSurfaceRegistry::Default()->IncrementGlobalUseCount(
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
		KosmSurfaceRegistry::Default()->DecrementGlobalUseCount(
			fData->buffer->id);
	}
}


int32
KosmSurface::LocalUseCount() const
{
	if (SURFACE_INVALID())
		return 0;
	return atomic_get(&fData->buffer->localUseCount);
}


bool
KosmSurface::IsInUse() const
{
	if (SURFACE_INVALID())
		return false;
	return KosmSurfaceRegistry::Default()->IsInUse(fData->buffer->id);
}


status_t
KosmSurface::SetAttachment(const char* key, const BMessage& value)
{
	if (key == nullptr)
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
	if (key == nullptr || outValue == nullptr)
		return B_BAD_VALUE;

	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);
	return fData->buffer->attachments.FindMessage(key, outValue);
}


status_t
KosmSurface::RemoveAttachment(const char* key)
{
	if (key == nullptr)
		return B_BAD_VALUE;

	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);
	return fData->buffer->attachments.RemoveName(key);
}


status_t
KosmSurface::CopyAllAttachments(BMessage* outValues) const
{
	if (outValues == nullptr)
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
KosmSurface::SetPurgeable(kosm_purgeable_state newState,
	kosm_purgeable_state* outOldState)
{
	if (SURFACE_INVALID())
		return B_BAD_VALUE;

	BAutolock locker(fData->buffer->lock);

	kosm_purgeable_state oldState = fData->buffer->purgeableState;

	if (outOldState != nullptr)
		*outOldState = oldState;

	if (newState == KOSM_PURGEABLE_KEEP_CURRENT)
		return B_OK;

	fData->buffer->purgeableState = newState;

	if (newState == KOSM_PURGEABLE_EMPTY)
		fData->buffer->contentsPurged = true;

	if (fData->buffer->contentsPurged
		&& newState == KOSM_PURGEABLE_NON_VOLATILE) {
		return KOSM_SURFACE_PURGED;
	}

	return B_OK;
}


bool
KosmSurface::IsVolatile() const
{
	if (SURFACE_INVALID())
		return false;

	// Lockless read: use atomic load so ARM64 doesn't see stale cache.
	kosm_purgeable_state state;
	__atomic_load(&fData->buffer->purgeableState, &state, __ATOMIC_ACQUIRE);
	return state == KOSM_PURGEABLE_VOLATILE;
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
	return LOCK_COUNT_LOAD(fData->buffer) > 0;
}


bool
KosmSurface::IsValid() const
{
	return fData != nullptr && fData->buffer != nullptr
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
KosmSurface::CreateAccessToken(KosmSurfaceToken* outToken)
{
	if (SURFACE_INVALID())
		return B_NO_INIT;
	if (outToken == nullptr)
		return B_BAD_VALUE;

	return KosmSurfaceRegistry::Default()->CreateAccessToken(
		fData->buffer->id, outToken);
}


status_t
KosmSurface::RevokeAllAccess()
{
	if (SURFACE_INVALID())
		return B_NO_INIT;

	return KosmSurfaceRegistry::Default()->RevokeAllAccess(
		fData->buffer->id);
}
