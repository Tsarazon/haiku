/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmSurfaceAllocator.hpp>
#include <KosmSurface.hpp>

#include <Autolock.h>
#include <Locker.h>

#include <new>
#include <string.h>

#include "AllocationBackend.hpp"
#include "KosmSurfacePrivate.hpp"
#include "PlanarLayout.hpp"
#include "SurfaceBuffer.hpp"
#include "SurfaceRegistry.hpp"


extern AllocationBackend* create_area_backend();


static const int32 kMaxSurfaces = 1024;


struct KosmSurfaceAllocator::Impl {
	AllocationBackend*	backend;
	KosmSurface*		surfaces[kMaxSurfaces];
	surface_id			nextId;
	BLocker				lock;

	Impl()
		:
		backend(create_area_backend()),
		nextId(1),
		lock("surface_allocator")
	{
		memset(surfaces, 0, sizeof(surfaces));
	}

	~Impl()
	{
		delete backend;
	}

	int32 IndexFor(surface_id id) const
	{
		return (id - 1) % kMaxSurfaces;
	}
};


KosmSurfaceAllocator*
KosmSurfaceAllocator::Default()
{
	static KosmSurfaceAllocator sDefault;
	return &sDefault;
}


KosmSurfaceAllocator::KosmSurfaceAllocator()
	:
	fImpl(new Impl)
{
}


KosmSurfaceAllocator::~KosmSurfaceAllocator()
{
	delete fImpl;
}


status_t
KosmSurfaceAllocator::Allocate(const surface_desc& desc,
	KosmSurface** outSurface)
{
	if (outSurface == NULL)
		return B_BAD_VALUE;

	if (desc.width == 0 || desc.height == 0)
		return B_BAD_VALUE;

	if (!fImpl->backend->SupportsFormat(desc.format))
		return B_BAD_VALUE;

	SurfaceBuffer* buffer = NULL;
	status_t status = fImpl->backend->Allocate(desc, &buffer);
	if (status != B_OK)
		return status;

	BAutolock locker(fImpl->lock);

	buffer->id = fImpl->nextId++;

	KosmSurface* surface = new(std::nothrow) KosmSurface;
	if (surface == NULL || surface->fData == NULL) {
		fImpl->backend->Free(buffer);
		delete surface;
		return B_NO_MEMORY;
	}

	surface->fData->buffer = buffer;

	status = SurfaceRegistry::Default()->Register(buffer->id, buffer->areaId,
		buffer->desc, buffer->allocSize);
	if (status != B_OK) {
		fImpl->backend->Free(buffer);
		delete surface;
		return status;
	}

	int32 index = fImpl->IndexFor(buffer->id);
	fImpl->surfaces[index] = surface;

	*outSurface = surface;
	return B_OK;
}


void
KosmSurfaceAllocator::Free(KosmSurface* surface)
{
	if (surface == NULL)
		return;

	BAutolock locker(fImpl->lock);

	surface_id id = surface->ID();
	int32 index = fImpl->IndexFor(id);

	if (fImpl->surfaces[index] == surface)
		fImpl->surfaces[index] = NULL;

	status_t status = SurfaceRegistry::Default()->Unregister(id);
	if (status == B_SURFACE_IN_USE)
		debugger("Freeing surface that is still in use");

	fImpl->backend->Free(surface->fData->buffer);
	delete surface;
}


status_t
KosmSurfaceAllocator::Lookup(surface_id id, KosmSurface** outSurface)
{
	if (outSurface == NULL)
		return B_BAD_VALUE;

	BAutolock locker(fImpl->lock);

	int32 index = fImpl->IndexFor(id);
	KosmSurface* surface = fImpl->surfaces[index];

	if (surface == NULL || surface->ID() != id)
		return B_NAME_NOT_FOUND;

	*outSurface = surface;
	return B_OK;
}


size_t
KosmSurfaceAllocator::GetPropertyMaximum(const char* property) const
{
	if (strcmp(property, "width") == 0)
		return fImpl->backend->GetMaxWidth();
	if (strcmp(property, "height") == 0)
		return fImpl->backend->GetMaxHeight();
	return 0;
}


size_t
KosmSurfaceAllocator::GetPropertyAlignment(const char* property) const
{
	if (strcmp(property, "bytesPerRow") == 0)
		return fImpl->backend->GetStrideAlignment(PIXEL_FORMAT_BGRA8888);
	return 1;
}


bool
KosmSurfaceAllocator::IsFormatSupported(pixel_format format) const
{
	return fImpl->backend->SupportsFormat(format);
}


status_t
KosmSurfaceAllocator::CreateFromClone(surface_id id, area_id clonedArea,
	KosmSurface** outSurface)
{
	if (outSurface == NULL || clonedArea < 0)
		return B_BAD_VALUE;

	// Get metadata from registry
	surface_desc desc;
	area_id sourceArea;
	status_t status = SurfaceRegistry::Default()->LookupInfo(id, &desc,
		&sourceArea);
	if (status != B_OK)
		return status;

	// Verify cloned area is valid
	area_info info;
	if (get_area_info(clonedArea, &info) != B_OK)
		return B_BAD_VALUE;

	BAutolock locker(fImpl->lock);

	// Create SurfaceBuffer
	SurfaceBuffer* buffer = new(std::nothrow) SurfaceBuffer;
	if (buffer == NULL)
		return B_NO_MEMORY;

	buffer->id = id;
	buffer->desc = desc;
	buffer->areaId = clonedArea;
	buffer->baseAddress = info.address;
	buffer->allocSize = info.size;
	buffer->planeCount = planar_get_plane_count(desc.format);

	// Calculate plane info
	for (uint32 i = 0; i < buffer->planeCount; i++) {
		planar_calculate_plane(desc.format, i, desc.width, desc.height,
			fImpl->backend->GetStrideAlignment(desc.format),
			&buffer->planes[i]);
	}

	KosmSurface* surface = new(std::nothrow) KosmSurface;
	if (surface == NULL || surface->fData == NULL) {
		delete buffer;
		delete surface;
		return B_NO_MEMORY;
	}

	surface->fData->buffer = buffer;

	int32 index = fImpl->IndexFor(id);
	fImpl->surfaces[index] = surface;

	*outSurface = surface;
	return B_OK;
}
