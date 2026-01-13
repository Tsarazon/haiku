/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AllocationBackend.h"
#include "PlanarLayout.h"
#include "SurfaceBuffer.h"

#include <OS.h>
#include <new>
#include <stdio.h>
#include <string.h>


class AreaBackend : public AllocationBackend {
public:
								AreaBackend();
	virtual						~AreaBackend();

	virtual	status_t			Allocate(const surface_desc& desc,
									SurfaceBuffer** outBuffer);
	virtual	void				Free(SurfaceBuffer* buffer);

	virtual	status_t			Map(SurfaceBuffer* buffer);
	virtual	status_t			Unmap(SurfaceBuffer* buffer);

	virtual	size_t				GetStrideAlignment(pixel_format format);
	virtual	size_t				GetMaxWidth();
	virtual	size_t				GetMaxHeight();
	virtual	bool				SupportsFormat(pixel_format format);
	virtual	bool				SupportsUsage(uint32 usage);

private:
	static	const size_t		kStrideAlignment = 64;
	static	const size_t		kMaxDimension = 16384;
};


AllocationBackend::~AllocationBackend()
{
}


AreaBackend::AreaBackend()
{
}


AreaBackend::~AreaBackend()
{
}


status_t
AreaBackend::Allocate(const surface_desc& desc, SurfaceBuffer** outBuffer)
{
	if (outBuffer == NULL)
		return B_BAD_VALUE;

	if (desc.width > kMaxDimension || desc.height > kMaxDimension)
		return B_BAD_VALUE;

	SurfaceBuffer* buffer = new(std::nothrow) SurfaceBuffer;
	if (buffer == NULL)
		return B_NO_MEMORY;

	buffer->desc = desc;
	buffer->planeCount = planar_get_plane_count(desc.format);

	for (uint32 i = 0; i < buffer->planeCount; i++) {
		planar_calculate_plane(desc.format, i, desc.width, desc.height,
			kStrideAlignment, &buffer->planes[i]);
	}

	buffer->allocSize = planar_calculate_total_size(desc.format,
		desc.width, desc.height, kStrideAlignment);

	if (buffer->desc.bytesPerElement == 0)
		buffer->desc.bytesPerElement = buffer->planes[0].bytesPerElement;
	if (buffer->desc.bytesPerRow == 0)
		buffer->desc.bytesPerRow = buffer->planes[0].bytesPerRow;

	size_t areaSize = (buffer->allocSize + B_PAGE_SIZE - 1)
		& ~(B_PAGE_SIZE - 1);

	char name[B_OS_NAME_LENGTH];
	snprintf(name, sizeof(name), "surface_%ux%u",
		desc.width, desc.height);

	buffer->baseAddress = NULL;
	buffer->areaId = create_area(name, &buffer->baseAddress,
		B_ANY_ADDRESS, areaSize, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

	if (buffer->areaId < 0) {
		status_t error = buffer->areaId;
		delete buffer;
		return error;
	}

	memset(buffer->baseAddress, 0, buffer->allocSize);

	*outBuffer = buffer;
	return B_OK;
}


void
AreaBackend::Free(SurfaceBuffer* buffer)
{
	if (buffer == NULL)
		return;

	if (buffer->areaId >= 0)
		delete_area(buffer->areaId);

	delete buffer;
}


status_t
AreaBackend::Map(SurfaceBuffer* buffer)
{
	return B_OK;
}


status_t
AreaBackend::Unmap(SurfaceBuffer* buffer)
{
	return B_OK;
}


size_t
AreaBackend::GetStrideAlignment(pixel_format format)
{
	return kStrideAlignment;
}


size_t
AreaBackend::GetMaxWidth()
{
	return kMaxDimension;
}


size_t
AreaBackend::GetMaxHeight()
{
	return kMaxDimension;
}


bool
AreaBackend::SupportsFormat(pixel_format format)
{
	switch (format) {
		case PIXEL_FORMAT_RGBA8888:
		case PIXEL_FORMAT_BGRA8888:
		case PIXEL_FORMAT_RGB565:
		case PIXEL_FORMAT_RGBX8888:
		case PIXEL_FORMAT_NV12:
		case PIXEL_FORMAT_NV21:
		case PIXEL_FORMAT_YV12:
			return true;
		default:
			return false;
	}
}


bool
AreaBackend::SupportsUsage(uint32 usage)
{
	uint32 supported = SURFACE_USAGE_CPU_READ | SURFACE_USAGE_CPU_WRITE
		| SURFACE_USAGE_COMPOSITOR;
	return (usage & ~supported) == 0;
}


AllocationBackend*
create_area_backend()
{
	return new(std::nothrow) AreaBackend;
}
