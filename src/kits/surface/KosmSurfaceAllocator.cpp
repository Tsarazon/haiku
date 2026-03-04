/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurfaceAllocator.hpp>
#include <KosmSurface.hpp>

#include <Autolock.h>
#include <HashMap.h>
#include <Locker.h>

#include <new>
#include <stdio.h>
#include <string.h>

#include "AllocationBackend.hpp"
#include "KosmSurfacePrivate.hpp"
#include "PlanarLayout.hpp"
#include "SurfaceBuffer.hpp"
#include "SurfaceGuards.hpp"
#include "SurfaceRegistry.hpp"


extern KosmAllocationBackend* kosm_create_area_backend();


struct KosmSurfaceAllocator::Impl {
	typedef HashMap<HashKey32<kosm_surface_id>, KosmSurface*> SurfaceMap;

	KosmAllocationBackend*	backend;
	SurfaceMap				surfaces;
	BLocker					lock;

	Impl()
		:
		backend(kosm_create_area_backend()),
		surfaces(),
		lock("kosm_surface_allocator")
	{
	}

	~Impl()
	{
		delete backend;
	}
};


static kosm_surface_id
generate_surface_id()
{
	// Mix counter with system_time to produce non-sequential IDs.
	// Not cryptographic, but prevents trivial enumeration.
	static int32 sCounter = 0;
	int32 counter = atomic_add(&sCounter, 1) + 1;
	uint32 mixed = (uint32)counter;

	// Stafford variant 13 (a reduced-round murmurhash finalizer)
	mixed ^= mixed >> 16;
	mixed *= 0x45d9f3bU;
	mixed ^= mixed >> 16;

	// Mix in low bits of system_time for cross-process unpredictability
	mixed ^= (uint32)(system_time() & 0xFFFF);

	// Ensure never 0 or tombstone
	if (mixed == 0 || mixed == KOSM_SURFACE_ID_TOMBSTONE)
		mixed = (uint32)counter;

	return (kosm_surface_id)mixed;
}


KosmSurfaceAllocator*
KosmSurfaceAllocator::Default()
{
	static KosmSurfaceAllocator sDefault;
	return &sDefault;
}


KosmSurfaceAllocator::KosmSurfaceAllocator()
	:
	fImpl(new(std::nothrow) Impl)
{
}


KosmSurfaceAllocator::~KosmSurfaceAllocator()
{
	delete fImpl;
}


status_t
KosmSurfaceAllocator::Allocate(const KosmSurfaceDesc& desc,
	KosmSurface** outSurface)
{
	if (outSurface == nullptr)
		return B_BAD_VALUE;

	if (fImpl == nullptr || fImpl->backend == nullptr)
		return B_NO_INIT;

	if (desc.width == 0 || desc.height == 0)
		return B_BAD_VALUE;

	if (!fImpl->backend->SupportsFormat(desc.format))
		return B_BAD_VALUE;

	KosmSurfaceBuffer* buffer = nullptr;
	status_t status = fImpl->backend->Allocate(desc, &buffer);
	if (status != B_OK)
		return status;

	if (buffer->waitSem < 0) {
		fImpl->backend->Free(buffer);
		return B_NO_MEMORY;
	}

	buffer->isOriginal = true;

	BAutolock locker(fImpl->lock);

	buffer->id = generate_surface_id();
	status = KosmSurfaceRegistry::Default()->Register(buffer->id,
		buffer->areaId, buffer->desc, buffer->allocSize,
		buffer->planeCount);

	if (status != B_OK) {
		fImpl->backend->Free(buffer);
		return status;
	}

	KosmSurface* surface = new(std::nothrow) KosmSurface;
	if (surface == nullptr || surface->fData == nullptr) {
		KosmSurfaceRegistry::Default()->Unregister(buffer->id);
		fImpl->backend->Free(buffer);
		delete surface;
		return B_NO_MEMORY;
	}

	surface->fData->buffer = buffer;

	status = fImpl->surfaces.Put(
		HashKey32<kosm_surface_id>(buffer->id), surface);
	if (status != B_OK) {
		KosmSurfaceRegistry::Default()->Unregister(buffer->id);
		fImpl->backend->Free(buffer);
		surface->fData->buffer = nullptr;
		delete surface;
		return status;
	}

	*outSurface = surface;
	return B_OK;
}


void
KosmSurfaceAllocator::Free(KosmSurface* surface)
{
	if (surface == nullptr || fImpl == nullptr)
		return;

	BAutolock locker(fImpl->lock);

	kosm_surface_id id = surface->ID();
	if (id == 0)
		return;

	KosmSurface* existing = fImpl->surfaces.Get(
		HashKey32<kosm_surface_id>(id));
	if (existing != surface)
		return;

	fImpl->surfaces.Remove(HashKey32<kosm_surface_id>(id));

	KosmSurfaceBuffer* buffer = surface->fData->buffer;
	surface->fData->buffer = nullptr;

	if (buffer != nullptr) {
		if (buffer->isOriginal) {
			status_t status = KosmSurfaceRegistry::Default()->Unregister(id);
			if (status == KOSM_SURFACE_IN_USE) {
				fprintf(stderr, "KosmSurfaceAllocator: freeing surface %"
					B_PRIu32 " that is still in use\n", id);
				KosmSurfaceRegistry::Default()->ForceUnregister(id);
			}
		} else {
			KosmSurfaceRegistry::Default()->DecrementGlobalUseCount(id);
		}

		if (buffer->ownsArea)
			fImpl->backend->Free(buffer);
		else
			delete buffer;
	}

	delete surface;
}


status_t
KosmSurfaceAllocator::Lookup(kosm_surface_id id, KosmSurface** outSurface)
{
	if (outSurface == nullptr || id == 0)
		return B_BAD_VALUE;

	if (fImpl == nullptr)
		return B_NO_INIT;

	BAutolock locker(fImpl->lock);

	KosmSurface* surface = fImpl->surfaces.Get(
		HashKey32<kosm_surface_id>(id));
	if (surface == nullptr)
		return B_NAME_NOT_FOUND;

	*outSurface = surface;
	return B_OK;
}


status_t
KosmSurfaceAllocator::LookupOrClone(kosm_surface_id id,
	KosmSurface** outSurface)
{
	if (outSurface == nullptr || id == 0)
		return B_BAD_VALUE;

	if (fImpl == nullptr)
		return B_NO_INIT;

	BAutolock locker(fImpl->lock);

	KosmSurface* surface = fImpl->surfaces.Get(
		HashKey32<kosm_surface_id>(id));
	if (surface != nullptr) {
		*outSurface = surface;
		return B_OK;
	}

	locker.Unlock();
	return _CreateFromClone(id, outSurface);
}


status_t
KosmSurfaceAllocator::LookupWithToken(const KosmSurfaceToken& token,
	KosmSurface** outSurface)
{
	if (outSurface == nullptr || token.id == 0)
		return B_BAD_VALUE;

	if (fImpl == nullptr)
		return B_NO_INIT;

	// Always validate the token against the registry first,
	// even for locally-cached surfaces. This ensures revoked
	// tokens are rejected.
	status_t status = KosmSurfaceRegistry::Default()->ValidateToken(token);
	if (status != B_OK) {
		*outSurface = nullptr;
		return B_NAME_NOT_FOUND;
	}

	BAutolock locker(fImpl->lock);

	KosmSurface* surface = fImpl->surfaces.Get(
		HashKey32<kosm_surface_id>(token.id));
	if (surface != nullptr) {
		*outSurface = surface;
		return B_OK;
	}

	locker.Unlock();
	return _CreateFromCloneWithToken(token, outSurface);
}


status_t
KosmSurfaceAllocator::_CreateFromClone(kosm_surface_id id,
	KosmSurface** outSurface)
{
	KosmSurfaceDesc desc;
	area_id sourceArea;
	size_t allocSize;
	uint32 planeCount;

	status_t status = KosmSurfaceRegistry::Default()->LookupInfo(id,
		&desc, &sourceArea, &allocSize, &planeCount);
	if (status != B_OK)
		return status;

	return _CloneFromRegistry(id, desc, sourceArea, allocSize, planeCount,
		outSurface);
}


status_t
KosmSurfaceAllocator::_CreateFromCloneWithToken(const KosmSurfaceToken& token,
	KosmSurface** outSurface)
{
	KosmSurfaceDesc desc;
	area_id sourceArea;
	size_t allocSize;
	uint32 planeCount;

	status_t status = KosmSurfaceRegistry::Default()->LookupInfoWithToken(
		token, &desc, &sourceArea, &allocSize, &planeCount);
	if (status != B_OK)
		return status;

	return _CloneFromRegistry(token.id, desc, sourceArea, allocSize,
		planeCount, outSurface);
}


status_t
KosmSurfaceAllocator::_CloneFromRegistry(kosm_surface_id id,
	const KosmSurfaceDesc& desc, area_id sourceArea,
	size_t allocSize, uint32 planeCount, KosmSurface** outSurface)
{
	void* address = nullptr;
	area_id clonedArea = clone_area("kosm_surface_clone", &address,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, sourceArea);
	if (clonedArea < 0)
		return clonedArea;

	KosmAreaGuard areaGuard(clonedArea);

	BAutolock locker(fImpl->lock);

	KosmSurface* existing = fImpl->surfaces.Get(
		HashKey32<kosm_surface_id>(id));
	if (existing != nullptr) {
		*outSurface = existing;
		return B_OK;
	}

	KosmSurfaceBuffer* buffer = new(std::nothrow) KosmSurfaceBuffer;
	if (buffer == nullptr)
		return B_NO_MEMORY;

	if (buffer->waitSem < 0) {
		delete buffer;
		return B_NO_MEMORY;
	}

	buffer->id = id;
	buffer->desc = desc;
	buffer->areaId = clonedArea;
	buffer->baseAddress = address;
	buffer->allocSize = allocSize;
	buffer->ownsArea = true;
	buffer->isOriginal = false;
	buffer->planeCount = planeCount;

	size_t strideAlignment = fImpl->backend->GetStrideAlignment(desc.format);
	for (uint32 i = 0; i < buffer->planeCount; i++) {
		kosm_planar_calculate_plane(desc.format, i, desc.width, desc.height,
			strideAlignment, &buffer->planes[i]);
	}

	KosmSurface* surface = new(std::nothrow) KosmSurface;
	if (surface == nullptr || surface->fData == nullptr) {
		delete buffer;
		delete surface;
		return B_NO_MEMORY;
	}

	surface->fData->buffer = buffer;

	status_t status = fImpl->surfaces.Put(
		HashKey32<kosm_surface_id>(id), surface);
	if (status != B_OK) {
		surface->fData->buffer = nullptr;
		delete buffer;
		delete surface;
		return status;
	}

	status = KosmSurfaceRegistry::Default()->IncrementGlobalUseCount(id);
	if (status != B_OK) {
		fImpl->surfaces.Remove(HashKey32<kosm_surface_id>(id));
		surface->fData->buffer = nullptr;
		delete buffer;
		delete surface;
		return status;
	}

	areaGuard.Release();

	*outSurface = surface;
	return B_OK;
}


size_t
KosmSurfaceAllocator::GetPropertyMaximum(const char* property) const
{
	if (fImpl == nullptr || fImpl->backend == nullptr)
		return 0;

	if (strcmp(property, "width") == 0)
		return fImpl->backend->GetMaxWidth();
	if (strcmp(property, "height") == 0)
		return fImpl->backend->GetMaxHeight();

	return 0;
}


size_t
KosmSurfaceAllocator::GetPropertyAlignment(const char* property) const
{
	if (fImpl == nullptr || fImpl->backend == nullptr)
		return 1;

	if (strcmp(property, "bytesPerRow") == 0)
		return fImpl->backend->GetStrideAlignment(KOSM_PIXEL_FORMAT_ARGB8888);

	return 1;
}


bool
KosmSurfaceAllocator::IsFormatSupported(kosm_pixel_format format) const
{
	if (fImpl == nullptr || fImpl->backend == nullptr)
		return false;

	return fImpl->backend->SupportsFormat(format);
}
