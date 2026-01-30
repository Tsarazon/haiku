/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PlanarLayout.hpp"


struct FormatInfo {
	uint32	planeCount;
	uint32	bytesPerPixel;
};


static const FormatInfo kFormatInfo[PIXEL_FORMAT_COUNT] = {
	[PIXEL_FORMAT_ARGB8888]	= { 1, 4 },
	[PIXEL_FORMAT_BGRA8888]	= { 1, 4 },
	[PIXEL_FORMAT_RGBA8888]	= { 1, 4 },
	[PIXEL_FORMAT_RGBX8888]	= { 1, 4 },
	[PIXEL_FORMAT_XRGB8888]	= { 1, 4 },
	[PIXEL_FORMAT_RGB565]	= { 1, 2 },
	[PIXEL_FORMAT_NV12]		= { 2, 1 },
	[PIXEL_FORMAT_NV21]		= { 2, 1 },
	[PIXEL_FORMAT_YV12]		= { 3, 1 },
	[PIXEL_FORMAT_A8]		= { 1, 1 },
	[PIXEL_FORMAT_L8]		= { 1, 1 },
};


uint32
planar_get_plane_count(pixel_format format)
{
	if (format >= PIXEL_FORMAT_COUNT)
		return 1;
	return kFormatInfo[format].planeCount;
}


uint32
planar_get_bytes_per_pixel(pixel_format format)
{
	if (format >= PIXEL_FORMAT_COUNT)
		return 4;
	return kFormatInfo[format].bytesPerPixel;
}


bool
planar_is_format_planar(pixel_format format)
{
	return planar_get_plane_count(format) > 1;
}


static inline size_t
align_stride(size_t stride, size_t alignment)
{
	return (stride + alignment - 1) & ~(alignment - 1);
}


void
planar_calculate_plane(pixel_format format, uint32 planeIndex,
	uint32 width, uint32 height, size_t strideAlignment,
	plane_info* outInfo)
{
	if (outInfo == NULL)
		return;

	outInfo->width = 0;
	outInfo->height = 0;
	outInfo->bytesPerElement = 0;
	outInfo->bytesPerRow = 0;
	outInfo->offset = 0;

	uint32 planeCount = planar_get_plane_count(format);
	if (planeIndex >= planeCount)
		return;

	if (planeIndex == 0) {
		outInfo->width = width;
		outInfo->height = height;
		outInfo->bytesPerElement = planar_get_bytes_per_pixel(format);
		outInfo->bytesPerRow = align_stride(
			width * outInfo->bytesPerElement, strideAlignment);
		outInfo->offset = 0;
		return;
	}

	plane_info plane0;
	planar_calculate_plane(format, 0, width, height, strideAlignment, &plane0);
	size_t plane0Size = plane0.bytesPerRow * plane0.height;

	if (format == PIXEL_FORMAT_NV12 || format == PIXEL_FORMAT_NV21) {
		outInfo->width = (width + 1) / 2;
		outInfo->height = (height + 1) / 2;
		outInfo->bytesPerElement = 2;
		outInfo->bytesPerRow = align_stride(
			outInfo->width * 2, strideAlignment);
		outInfo->offset = plane0Size;
	} else if (format == PIXEL_FORMAT_YV12) {
		outInfo->width = (width + 1) / 2;
		outInfo->height = (height + 1) / 2;
		outInfo->bytesPerElement = 1;
		outInfo->bytesPerRow = align_stride(outInfo->width, strideAlignment);

		size_t uvPlaneSize = outInfo->bytesPerRow * outInfo->height;
		if (planeIndex == 1)
			outInfo->offset = plane0Size;
		else
			outInfo->offset = plane0Size + uvPlaneSize;
	}
}


size_t
planar_calculate_total_size(pixel_format format, uint32 width, uint32 height,
	size_t strideAlignment)
{
	size_t total = 0;
	uint32 planeCount = planar_get_plane_count(format);

	for (uint32 i = 0; i < planeCount; i++) {
		plane_info plane;
		planar_calculate_plane(format, i, width, height, strideAlignment,
			&plane);
		size_t planeEnd = plane.offset + plane.bytesPerRow * plane.height;
		if (planeEnd > total)
			total = planeEnd;
	}

	return total;
}


uint32
planar_get_component_count(pixel_format format, uint32 planeIndex)
{
	switch (format) {
		case PIXEL_FORMAT_ARGB8888:
		case PIXEL_FORMAT_BGRA8888:
		case PIXEL_FORMAT_RGBA8888:
		case PIXEL_FORMAT_RGBX8888:
		case PIXEL_FORMAT_XRGB8888:
			return 4;

		case PIXEL_FORMAT_RGB565:
			return 3;

		case PIXEL_FORMAT_NV12:
		case PIXEL_FORMAT_NV21:
			return (planeIndex == 0) ? 1 : 2;

		case PIXEL_FORMAT_YV12:
			return 1;

		case PIXEL_FORMAT_A8:
		case PIXEL_FORMAT_L8:
			return 1;

		default:
			return 0;
	}
}


uint32
planar_get_bit_depth(pixel_format format, uint32 planeIndex,
	uint32 componentIndex)
{
	switch (format) {
		case PIXEL_FORMAT_ARGB8888:
		case PIXEL_FORMAT_BGRA8888:
		case PIXEL_FORMAT_RGBA8888:
		case PIXEL_FORMAT_RGBX8888:
		case PIXEL_FORMAT_XRGB8888:
			return 8;

		case PIXEL_FORMAT_RGB565:
			if (componentIndex == 1)
				return 6;
			return 5;

		case PIXEL_FORMAT_NV12:
		case PIXEL_FORMAT_NV21:
		case PIXEL_FORMAT_YV12:
			return 8;

		case PIXEL_FORMAT_A8:
		case PIXEL_FORMAT_L8:
			return 8;

		default:
			return 0;
	}
}


uint32
planar_get_bit_offset(pixel_format format, uint32 planeIndex,
	uint32 componentIndex)
{
	switch (format) {
		case PIXEL_FORMAT_ARGB8888:
			// Memory: [B][G][R][A] on little-endian
			switch (componentIndex) {
				case 0: return 16;	// R
				case 1: return 8;	// G
				case 2: return 0;	// B
				case 3: return 24;	// A
			}
			break;

		case PIXEL_FORMAT_BGRA8888:
			// Memory: [B][G][R][A]
			switch (componentIndex) {
				case 0: return 8;	// G
				case 1: return 16;	// R
				case 2: return 0;	// B
				case 3: return 24;	// A
			}
			break;

		case PIXEL_FORMAT_RGBA8888:
			// Memory: [R][G][B][A]
			switch (componentIndex) {
				case 0: return 0;	// R
				case 1: return 8;	// G
				case 2: return 16;	// B
				case 3: return 24;	// A
			}
			break;

		case PIXEL_FORMAT_RGBX8888:
			switch (componentIndex) {
				case 0: return 0;	// R
				case 1: return 8;	// G
				case 2: return 16;	// B
				case 3: return 24;	// X
			}
			break;

		case PIXEL_FORMAT_XRGB8888:
			switch (componentIndex) {
				case 0: return 16;	// R
				case 1: return 8;	// G
				case 2: return 0;	// B
				case 3: return 24;	// X
			}
			break;

		case PIXEL_FORMAT_RGB565:
			switch (componentIndex) {
				case 0: return 11;	// R
				case 1: return 5;	// G
				case 2: return 0;	// B
			}
			break;

		case PIXEL_FORMAT_NV12:
			if (planeIndex == 0)
				return 0;	// Y
			return (componentIndex == 0) ? 0 : 8;	// U, V

		case PIXEL_FORMAT_NV21:
			if (planeIndex == 0)
				return 0;	// Y
			return (componentIndex == 0) ? 8 : 0;	// V, U

		case PIXEL_FORMAT_YV12:
		case PIXEL_FORMAT_A8:
		case PIXEL_FORMAT_L8:
			return 0;

		default:
			break;
	}

	return 0;
}
