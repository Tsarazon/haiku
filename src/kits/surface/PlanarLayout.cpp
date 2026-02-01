/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PlanarLayout.hpp"


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
		outInfo->bytesPerRow = align_stride(
			width * outInfo->bytesPerElement, strideAlignment);
		outInfo->offset = 0;
		return;
	}

	KosmPlaneInfo plane0;
	kosm_planar_calculate_plane(format, 0, width, height, strideAlignment,
		&plane0);
	size_t plane0Size = plane0.bytesPerRow * plane0.height;

	if (format == KOSM_PIXEL_FORMAT_NV12 || format == KOSM_PIXEL_FORMAT_NV21) {
		outInfo->width = (width + 1) / 2;
		outInfo->height = (height + 1) / 2;
		outInfo->bytesPerElement = 2;
		outInfo->bytesPerRow = align_stride(
			outInfo->width * 2, strideAlignment);
		outInfo->offset = plane0Size;
	} else if (format == KOSM_PIXEL_FORMAT_YV12) {
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
kosm_planar_calculate_total_size(kosm_pixel_format format,
	uint32 width, uint32 height, size_t strideAlignment)
{
	size_t total = 0;
	uint32 planeCount = kosm_planar_get_plane_count(format);

	for (uint32 i = 0; i < planeCount; i++) {
		KosmPlaneInfo plane;
		kosm_planar_calculate_plane(format, i, width, height, strideAlignment,
			&plane);
		size_t planeEnd = plane.offset + plane.bytesPerRow * plane.height;
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


// Component index convention: R=0, G=1, B=2, A=3
// Bit offsets are within a little-endian uint32 pixel.

uint32
kosm_planar_get_bit_offset(kosm_pixel_format format, uint32 planeIndex,
	uint32 componentIndex)
{
	switch (format) {
		case KOSM_PIXEL_FORMAT_ARGB8888:
			// LE uint32: 0xAARRGGBB
			// Memory: [B][G][R][A]
			switch (componentIndex) {
				case 0: return 16;	// R
				case 1: return 8;	// G
				case 2: return 0;	// B
				case 3: return 24;	// A
			}
			break;

		case KOSM_PIXEL_FORMAT_BGRA8888:
			// LE uint32: 0xAARRGGBB (same as ARGB8888)
			// Memory: [B][G][R][A]
			switch (componentIndex) {
				case 0: return 16;	// R
				case 1: return 8;	// G
				case 2: return 0;	// B
				case 3: return 24;	// A
			}
			break;

		case KOSM_PIXEL_FORMAT_RGBA8888:
			// LE uint32: 0xAABBGGRR
			// Memory: [R][G][B][A]
			switch (componentIndex) {
				case 0: return 0;	// R
				case 1: return 8;	// G
				case 2: return 16;	// B
				case 3: return 24;	// A
			}
			break;

		case KOSM_PIXEL_FORMAT_RGBX8888:
			// Memory: [R][G][B][X]
			switch (componentIndex) {
				case 0: return 0;	// R
				case 1: return 8;	// G
				case 2: return 16;	// B
				case 3: return 24;	// X
			}
			break;

		case KOSM_PIXEL_FORMAT_XRGB8888:
			// Memory: [B][G][R][X]
			switch (componentIndex) {
				case 0: return 16;	// R
				case 1: return 8;	// G
				case 2: return 0;	// B
				case 3: return 24;	// X
			}
			break;

		case KOSM_PIXEL_FORMAT_RGB565:
			switch (componentIndex) {
				case 0: return 11;	// R
				case 1: return 5;	// G
				case 2: return 0;	// B
			}
			break;

		case KOSM_PIXEL_FORMAT_NV12:
			if (planeIndex == 0)
				return 0;	// Y
			return (componentIndex == 0) ? 0 : 8;	// U, V

		case KOSM_PIXEL_FORMAT_NV21:
			if (planeIndex == 0)
				return 0;	// Y
			return (componentIndex == 0) ? 8 : 0;	// V, U

		case KOSM_PIXEL_FORMAT_YV12:
		case KOSM_PIXEL_FORMAT_A8:
		case KOSM_PIXEL_FORMAT_L8:
			return 0;

		default:
			break;
	}

	return 0;
}
