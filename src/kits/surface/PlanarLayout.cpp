/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PlanarLayout.hpp"

#include <limits.h>


struct FormatInfo {
	uint32	planeCount;
	uint32	bytesPerPixel;
};


static const FormatInfo kFormatInfo[KOSM_PIXEL_FORMAT_COUNT] = {
	[KOSM_PIXEL_FORMAT_ARGB8888]	= { 1, 4 },
	[KOSM_PIXEL_FORMAT_BGRA8888]	= { 1, 4 },
	[KOSM_PIXEL_FORMAT_RGBA8888]	= { 1, 4 },
	[KOSM_PIXEL_FORMAT_RGBX8888]	= { 1, 4 },
	[KOSM_PIXEL_FORMAT_XRGB8888]	= { 1, 4 },
	[KOSM_PIXEL_FORMAT_RGB565]		= { 1, 2 },
	[KOSM_PIXEL_FORMAT_NV12]		= { 2, 1 },
	[KOSM_PIXEL_FORMAT_NV21]		= { 2, 1 },
	[KOSM_PIXEL_FORMAT_YV12]		= { 3, 1 },
	[KOSM_PIXEL_FORMAT_A8]			= { 1, 1 },
	[KOSM_PIXEL_FORMAT_L8]			= { 1, 1 },
};


uint32
kosm_planar_get_plane_count(kosm_pixel_format format)
{
	if (format >= KOSM_PIXEL_FORMAT_COUNT)
		return 1;
	return kFormatInfo[format].planeCount;
}


uint32
kosm_planar_get_bytes_per_pixel(kosm_pixel_format format)
{
	if (format >= KOSM_PIXEL_FORMAT_COUNT)
		return 4;
	return kFormatInfo[format].bytesPerPixel;
}


bool
kosm_planar_is_planar(kosm_pixel_format format)
{
	return kosm_planar_get_plane_count(format) > 1;
}


static inline size_t
align_stride(size_t stride, size_t alignment)
{
	return (stride + alignment - 1) & ~(alignment - 1);
}


// Returns true on overflow, false on success.
static inline bool
safe_mul(size_t a, size_t b, size_t* result)
{
	if (a != 0 && b > SIZE_MAX / a)
		return true;
	*result = a * b;
	return false;
}


void
kosm_planar_calculate_plane(kosm_pixel_format format, uint32 planeIndex,
	uint32 width, uint32 height, size_t strideAlignment,
	KosmPlaneInfo* outInfo)
{
	if (outInfo == NULL)
		return;

	outInfo->width = 0;
	outInfo->height = 0;
	outInfo->bytesPerElement = 0;
	outInfo->bytesPerRow = 0;
	outInfo->offset = 0;

	uint32 planeCount = kosm_planar_get_plane_count(format);
	if (planeIndex >= planeCount)
		return;

	if (planeIndex == 0) {
		outInfo->width = width;
		outInfo->height = height;
		outInfo->bytesPerElement = kosm_planar_get_bytes_per_pixel(format);

		size_t rawStride;
		if (safe_mul(width, outInfo->bytesPerElement, &rawStride))
			return;

		outInfo->bytesPerRow = align_stride(rawStride, strideAlignment);
		outInfo->offset = 0;
		return;
	}

	KosmPlaneInfo plane0;
	kosm_planar_calculate_plane(format, 0, width, height, strideAlignment,
		&plane0);

	size_t plane0Size;
	if (safe_mul(plane0.bytesPerRow, plane0.height, &plane0Size))
		return;

	if (format == KOSM_PIXEL_FORMAT_NV12 || format == KOSM_PIXEL_FORMAT_NV21) {
		outInfo->width = (width + 1) / 2;
		outInfo->height = (height + 1) / 2;
		outInfo->bytesPerElement = 2;

		size_t rawStride;
		if (safe_mul(outInfo->width, 2, &rawStride))
			return;

		outInfo->bytesPerRow = align_stride(rawStride, strideAlignment);
		outInfo->offset = plane0Size;
	} else if (format == KOSM_PIXEL_FORMAT_YV12) {
		outInfo->width = (width + 1) / 2;
		outInfo->height = (height + 1) / 2;
		outInfo->bytesPerElement = 1;
		outInfo->bytesPerRow = align_stride(outInfo->width, strideAlignment);

		size_t uvPlaneSize;
		if (safe_mul(outInfo->bytesPerRow, outInfo->height, &uvPlaneSize))
			return;

		if (planeIndex == 1)
			outInfo->offset = plane0Size;
		else
			outInfo->offset = plane0Size + uvPlaneSize;
	}
}


size_t
kosm_planar_calculate_total_size(kosm_pixel_format format,
	uint32 width, uint32 height, size_t strideAlignment)
{
	size_t total = 0;
	uint32 planeCount = kosm_planar_get_plane_count(format);

	for (uint32 i = 0; i < planeCount; i++) {
		KosmPlaneInfo plane;
		kosm_planar_calculate_plane(format, i, width, height, strideAlignment,
			&plane);

		size_t planeSize;
		if (safe_mul(plane.bytesPerRow, plane.height, &planeSize))
			return 0;

		size_t planeEnd = plane.offset + planeSize;
		if (planeEnd < plane.offset)
			return 0;  // addition overflow

		if (planeEnd > total)
			total = planeEnd;
	}

	return total;
}


uint32
kosm_planar_get_component_count(kosm_pixel_format format, uint32 planeIndex)
{
	switch (format) {
		case KOSM_PIXEL_FORMAT_ARGB8888:
		case KOSM_PIXEL_FORMAT_BGRA8888:
		case KOSM_PIXEL_FORMAT_RGBA8888:
		case KOSM_PIXEL_FORMAT_RGBX8888:
		case KOSM_PIXEL_FORMAT_XRGB8888:
			return 4;

		case KOSM_PIXEL_FORMAT_RGB565:
			return 3;

		case KOSM_PIXEL_FORMAT_NV12:
		case KOSM_PIXEL_FORMAT_NV21:
			return (planeIndex == 0) ? 1 : 2;

		case KOSM_PIXEL_FORMAT_YV12:
			return 1;

		case KOSM_PIXEL_FORMAT_A8:
		case KOSM_PIXEL_FORMAT_L8:
			return 1;

		default:
			return 0;
	}
}


uint32
kosm_planar_get_bit_depth(kosm_pixel_format format, uint32 planeIndex,
	uint32 componentIndex)
{
	switch (format) {
		case KOSM_PIXEL_FORMAT_ARGB8888:
		case KOSM_PIXEL_FORMAT_BGRA8888:
		case KOSM_PIXEL_FORMAT_RGBA8888:
		case KOSM_PIXEL_FORMAT_RGBX8888:
		case KOSM_PIXEL_FORMAT_XRGB8888:
			return 8;

		case KOSM_PIXEL_FORMAT_RGB565:
			if (componentIndex == 1)
				return 6;
			return 5;

		case KOSM_PIXEL_FORMAT_NV12:
		case KOSM_PIXEL_FORMAT_NV21:
		case KOSM_PIXEL_FORMAT_YV12:
			return 8;

		case KOSM_PIXEL_FORMAT_A8:
		case KOSM_PIXEL_FORMAT_L8:
			return 8;

		default:
			return 0;
	}
}


uint32
kosm_planar_get_bit_offset(kosm_pixel_format format, uint32 planeIndex,
	uint32 componentIndex)
{
	switch (format) {
		case KOSM_PIXEL_FORMAT_ARGB8888:
			switch (componentIndex) {
				case 0: return 16;
				case 1: return 8;
				case 2: return 0;
				case 3: return 24;
			}
			break;

		case KOSM_PIXEL_FORMAT_BGRA8888:
			switch (componentIndex) {
				case 0: return 16;
				case 1: return 8;
				case 2: return 0;
				case 3: return 24;
			}
			break;

		case KOSM_PIXEL_FORMAT_RGBA8888:
			switch (componentIndex) {
				case 0: return 0;
				case 1: return 8;
				case 2: return 16;
				case 3: return 24;
			}
			break;

		case KOSM_PIXEL_FORMAT_RGBX8888:
			switch (componentIndex) {
				case 0: return 0;
				case 1: return 8;
				case 2: return 16;
				case 3: return 24;
			}
			break;

		case KOSM_PIXEL_FORMAT_XRGB8888:
			switch (componentIndex) {
				case 0: return 16;
				case 1: return 8;
				case 2: return 0;
				case 3: return 24;
			}
			break;

		case KOSM_PIXEL_FORMAT_RGB565:
			switch (componentIndex) {
				case 0: return 11;
				case 1: return 5;
				case 2: return 0;
			}
			break;

		case KOSM_PIXEL_FORMAT_NV12:
			if (planeIndex == 0)
				return 0;
			return (componentIndex == 0) ? 0 : 8;

		case KOSM_PIXEL_FORMAT_NV21:
			if (planeIndex == 0)
				return 0;
			return (componentIndex == 0) ? 8 : 0;

		case KOSM_PIXEL_FORMAT_YV12:
		case KOSM_PIXEL_FORMAT_A8:
		case KOSM_PIXEL_FORMAT_L8:
			return 0;

		default:
			break;
	}

	return 0;
}
