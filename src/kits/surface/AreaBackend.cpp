/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AllocationBackend.hpp"
#include "PlanarLayout.hpp"
#include "SurfaceBuffer.hpp"

#include <OS.h>

#include <new>
#include <stdio.h>
#include <string.h>


class KosmAreaBackend : public KosmAllocationBackend {
public:
								KosmAreaBackend();
	virtual						~KosmAreaBackend();

	virtual	status_t			Allocate(const KosmSurfaceDesc& desc,
									KosmSurfaceBuffer** outBuffer);
	virtual	void				Free(KosmSurfaceBuffer* buffer);

	virtual	status_t			Map(KosmSurfaceBuffer* buffer);
	virtual	status_t			Unmap(KosmSurfaceBuffer* buffer);

	virtual	size_t				GetStrideAlignment(kosm_pixel_format format);
	virtual	size_t				GetMaxWidth();
	virtual	size_t				GetMaxHeight();
	virtual	bool				SupportsFormat(kosm_pixel_format format);
	virtual	bool				SupportsUsage(uint32 usage);

private:
	static	int32				sNextId;

	static	const size_t		kStrideAlignment = 64;
	static	const size_t		kMaxDimension = 16384;
};


int32 KosmAreaBackend::sNextId = 1;


KosmAllocationBackend::~KosmAllocationBackend()
{
}


KosmAreaBackend::KosmAreaBackend()
{
}


KosmAreaBackend::~KosmAreaBackend()
{
}


status_t
KosmAreaBackend::Allocate(const KosmSurfaceDesc& desc,
	KosmSurfaceBuffer** outBuffer)
{
	if (outBuffer == NULL)
		return B_BAD_VALUE;

	if (desc.width == 0 || desc.height == 0)
		return B_BAD_VALUE;

	if (desc.width > kMaxDimension || desc.height > kMaxDimension)
		return B_BAD_VALUE;

	KosmSurfaceBuffer* buffer = new(std::nothrow) KosmSurfaceBuffer;
	if (buffer == NULL)
		return B_NO_MEMORY;

	buffer->desc = desc;
	buffer->planeCount = kosm_planar_get_plane_count(desc.format);

	for (uint32 i = 0; i < buffer->planeCount; i++) {
		kosm_planar_calculate_plane(desc.format, i, desc.width, desc.height,
			kStrideAlignment, &buffer->planes[i]);
	}

	buffer->allocSize = kosm_planar_calculate_total_size(desc.format,
		desc.width, desc.height, kStrideAlignment);

	if (buffer->desc.bytesPerElement == 0)
		buffer->desc.bytesPerElement = buffer->planes[0].bytesPerElement;
	if (buffer->desc.bytesPerRow == 0)
		buffer->desc.bytesPerRow = buffer->planes[0].bytesPerRow;

	size_t areaSize = (buffer->allocSize + B_PAGE_SIZE - 1)
		& ~(B_PAGE_SIZE - 1);

	int32 uniqueId = atomic_add(&sNextId, 1);
	char name[B_OS_NAME_LENGTH];
	snprintf(name, sizeof(name), "kosm_surface_%d_%ux%u",
		uniqueId, desc.width, desc.height);

	buffer->baseAddress = NULL;
	buffer->areaId = create_area(name, &buffer->baseAddress,
		B_ANY_ADDRESS, areaSize, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

	if (buffer->areaId < 0) {
		status_t error = buffer->areaId;
		delete buffer;
		return error;
	}

	buffer->ownsArea = true;
	memset(buffer->baseAddress, 0, buffer->allocSize);

	*outBuffer = buffer;
	return B_OK;
}


void
KosmAreaBackend::Free(KosmSurfaceBuffer* buffer)
{
	if (buffer == NULL)
		return;

	if (buffer->ownsArea && buffer->areaId >= 0)
		delete_area(buffer->areaId);

	delete buffer;
}


status_t
KosmAreaBackend::Map(KosmSurfaceBuffer* buffer)
{
	return B_OK;
}


status_t
KosmAreaBackend::Unmap(KosmSurfaceBuffer* buffer)
{
	return B_OK;
}


size_t
KosmAreaBackend::GetStrideAlignment(kosm_pixel_format format)
{
	return kStrideAlignment;
}


size_t
KosmAreaBackend::GetMaxWidth()
{
	return kMaxDimension;
}


size_t
KosmAreaBackend::GetMaxHeight()
{
	return kMaxDimension;
}


bool
KosmAreaBackend::SupportsFormat(kosm_pixel_format format)
{
	return format < KOSM_PIXEL_FORMAT_COUNT;
}


bool
KosmAreaBackend::SupportsUsage(uint32 usage)
{
	uint32 supported = KOSM_SURFACE_USAGE_CPU_READ
		| KOSM_SURFACE_USAGE_CPU_WRITE
		| KOSM_SURFACE_USAGE_COMPOSITOR
		| KOSM_SURFACE_USAGE_PURGEABLE;
	return (usage & ~supported) == 0;
}


KosmAllocationBackend*
kosm_create_area_backend()
{
	return new(std::nothrow) KosmAreaBackend;
}
