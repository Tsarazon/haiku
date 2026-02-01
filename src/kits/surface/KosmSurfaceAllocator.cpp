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
#include <string.h>

#include "AllocationBackend.hpp"
#include "KosmSurfacePrivate.hpp"
#include "PlanarLayout.hpp"
#include "SurfaceBuffer.hpp"
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


static const int32 kMaxIdRetries = 8;


static kosm_surface_id
generate_surface_id()
{
	static int32 sCounter = 0;
	int32 counter = atomic_add(&sCounter, 1);
	uint32 hash = (uint32)system_time() ^ (counter * 2654435761u);

	if (hash == 0 || hash == KOSM_SURFACE_ID_TOMBSTONE)
		hash = 1;

	return hash;
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
	if (outSurface == NULL)
		return B_BAD_VALUE;

	if (fImpl == NULL || fImpl->backend == NULL)
		return B_NO_INIT;

	if (desc.width == 0 || desc.height == 0)
		return B_BAD_VALUE;

	if (!fImpl->backend->SupportsFormat(desc.format))
		return B_BAD_VALUE;

	KosmSurfaceBuffer* buffer = NULL;
	status_t status = fImpl->backend->Allocate(desc, &buffer);
	if (status != B_OK)
		return status;

	BAutolock locker(fImpl->lock);

	status = KOSM_SURFACE_ID_EXISTS;
	int32 retries = 0;
	while (status == KOSM_SURFACE_ID_EXISTS && retries < kMaxIdRetries) {
		buffer->id = generate_surface_id();
		status = KosmSurfaceRegistry::Default()->Register(buffer->id,
			buffer->areaId, buffer->desc, buffer->allocSize,
			buffer->planeCount);
		retries++;
	}

	if (status != B_OK) {
		fImpl->backend->Free(buffer);
		return status;
	}

	KosmSurface* surface = new(std::nothrow) KosmSurface;
	if (surface == NULL || surface->fData == NULL) {
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
		surface->fData->buffer = NULL;
		delete surface;
		return status;
	}

	*outSurface = surface;
	return B_OK;
}


void
KosmSurfaceAllocator::Free(KosmSurface* surface)
{
	if (surface == NULL || fImpl == NULL)
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
	surface->fData->buffer = NULL;

	if (buffer != NULL) {
		status_t status = KosmSurfaceRegistry::Default()->Unregister(id);
		if (status == KOSM_SURFACE_IN_USE)
			debugger("Freeing surface that is still in use");

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
	if (outSurface == NULL || id == 0)
		return B_BAD_VALUE;

	if (fImpl == NULL)
		return B_NO_INIT;

	BAutolock locker(fImpl->lock);

	KosmSurface* surface = fImpl->surfaces.Get(
		HashKey32<kosm_surface_id>(id));
	if (surface == NULL)
		return B_NAME_NOT_FOUND;

	*outSurface = surface;
	return B_OK;
}


status_t
KosmSurfaceAllocator::LookupOrClone(kosm_surface_id id,
	KosmSurface** outSurface)
{
	if (outSurface == NULL || id == 0)
		return B_BAD_VALUE;

	if (fImpl == NULL)
		return B_NO_INIT;

	BAutolock locker(fImpl->lock);

	KosmSurface* surface = fImpl->surfaces.Get(
		HashKey32<kosm_surface_id>(id));
	if (surface != NULL) {
		*outSurface = surface;
		return B_OK;
	}

	locker.Unlock();
	return _CreateFromClone(id, outSurface);
}


status_t
KosmSurfaceAllocator::_CreateFromClone(kosm_surface_id id,
	KosmSurface** outSurface)
{
	if (outSurface == NULL || id == 0)
		return B_BAD_VALUE;

	KosmSurfaceDesc desc;
	area_id sourceArea;
	size_t allocSize;
	uint32 planeCount;

	status_t status = KosmSurfaceRegistry::Default()->LookupInfo(id,
		&desc, &sourceArea, &allocSize, &planeCount);
	if (status != B_OK)
		return status;

	void* address = NULL;
	area_id clonedArea = clone_area("kosm_surface_clone", &address,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, sourceArea);
	if (clonedArea < 0)
		return clonedArea;

	BAutolock locker(fImpl->lock);

	KosmSurface* existing = fImpl->surfaces.Get(
		HashKey32<kosm_surface_id>(id));
	if (existing != NULL) {
		delete_area(clonedArea);
		*outSurface = existing;
		return B_OK;
	}

	KosmSurfaceBuffer* buffer = new(std::nothrow) KosmSurfaceBuffer;
	if (buffer == NULL) {
		delete_area(clonedArea);
		return B_NO_MEMORY;
	}

	buffer->id = id;
	buffer->desc = desc;
	buffer->areaId = clonedArea;
	buffer->baseAddress = address;
	buffer->allocSize = allocSize;
	buffer->ownsArea = true;
	buffer->planeCount = planeCount;

	size_t strideAlignment = fImpl->backend->GetStrideAlignment(desc.format);
	for (uint32 i = 0; i < buffer->planeCount; i++) {
		kosm_planar_calculate_plane(desc.format, i, desc.width, desc.height,
			strideAlignment, &buffer->planes[i]);
	}

	KosmSurface* surface = new(std::nothrow) KosmSurface;
	if (surface == NULL || surface->fData == NULL) {
		delete_area(clonedArea);
		delete buffer;
		delete surface;
		return B_NO_MEMORY;
	}

	surface->fData->buffer = buffer;

	status = fImpl->surfaces.Put(
		HashKey32<kosm_surface_id>(id), surface);
	if (status != B_OK) {
		delete_area(clonedArea);
		delete buffer;
		surface->fData->buffer = NULL;
		delete surface;
		return status;
	}

	KosmSurfaceRegistry::Default()->IncrementGlobalUseCount(id);

	*outSurface = surface;
	return B_OK;
}


status_t
KosmSurfaceAllocator::LookupWithToken(const KosmSurfaceToken& token,
	KosmSurface** outSurface)
{
	if (outSurface == NULL || token.id == 0)
		return B_BAD_VALUE;

	if (fImpl == NULL)
		return B_NO_INIT;

	BAutolock locker(fImpl->lock);

	KosmSurface* surface = fImpl->surfaces.Get(
		HashKey32<kosm_surface_id>(token.id));
	if (surface != NULL) {
		*outSurface = surface;
		return B_OK;
	}

	locker.Unlock();
	return _CreateFromCloneWithToken(token, outSurface);
}


status_t
KosmSurfaceAllocator::_CreateFromCloneWithToken(const KosmSurfaceToken& token,
	KosmSurface** outSurface)
{
	if (outSurface == NULL || token.id == 0)
		return B_BAD_VALUE;

	KosmSurfaceDesc desc;
	area_id sourceArea;
	size_t allocSize;
	uint32 planeCount;

	status_t status = KosmSurfaceRegistry::Default()->LookupInfoWithToken(
		token, &desc, &sourceArea, &allocSize, &planeCount);
	if (status != B_OK)
		return status;

	void* address = NULL;
	area_id clonedArea = clone_area("kosm_surface_clone", &address,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, sourceArea);
	if (clonedArea < 0)
		return clonedArea;

	BAutolock locker(fImpl->lock);

	KosmSurface* existing = fImpl->surfaces.Get(
		HashKey32<kosm_surface_id>(token.id));
	if (existing != NULL) {
		delete_area(clonedArea);
		*outSurface = existing;
		return B_OK;
	}

	KosmSurfaceBuffer* buffer = new(std::nothrow) KosmSurfaceBuffer;
	if (buffer == NULL) {
		delete_area(clonedArea);
		return B_NO_MEMORY;
	}

	buffer->id = token.id;
	buffer->desc = desc;
	buffer->areaId = clonedArea;
	buffer->baseAddress = address;
	buffer->allocSize = allocSize;
	buffer->ownsArea = true;
	buffer->planeCount = planeCount;

	size_t strideAlignment = fImpl->backend->GetStrideAlignment(desc.format);
	for (uint32 i = 0; i < buffer->planeCount; i++) {
		kosm_planar_calculate_plane(desc.format, i, desc.width, desc.height,
			strideAlignment, &buffer->planes[i]);
	}

	KosmSurface* surface = new(std::nothrow) KosmSurface;
	if (surface == NULL || surface->fData == NULL) {
		delete_area(clonedArea);
		delete buffer;
		delete surface;
		return B_NO_MEMORY;
	}

	surface->fData->buffer = buffer;

	status = fImpl->surfaces.Put(
		HashKey32<kosm_surface_id>(token.id), surface);
	if (status != B_OK) {
		delete_area(clonedArea);
		delete buffer;
		surface->fData->buffer = NULL;
		delete surface;
		return status;
	}

	KosmSurfaceRegistry::Default()->IncrementGlobalUseCount(token.id);

	*outSurface = surface;
	return B_OK;
}


size_t
KosmSurfaceAllocator::GetPropertyMaximum(const char* property) const
{
	if (fImpl == NULL || fImpl->backend == NULL)
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
	if (fImpl == NULL || fImpl->backend == NULL)
		return 1;

	if (strcmp(property, "bytesPerRow") == 0)
		return fImpl->backend->GetStrideAlignment(KOSM_PIXEL_FORMAT_ARGB8888);

	return 1;
}


bool
KosmSurfaceAllocator::IsFormatSupported(kosm_pixel_format format) const
{
	if (fImpl == NULL || fImpl->backend == NULL)
		return false;

	return fImpl->backend->SupportsFormat(format);
}
