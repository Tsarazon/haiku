/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PlanarLayout.hpp"


typedef struct {
	uint32	planeCount;
	uint32	bitsPerPixel;
} format_info;


static const format_info kFormats[] = {
	// 32-bit RGBA variants
	{ 1, 32 },	// PIXEL_FORMAT_RGBA8888
	{ 1, 32 },	// PIXEL_FORMAT_BGRA8888
	{ 1, 16 },	// PIXEL_FORMAT_RGB565
	{ 1, 32 },	// PIXEL_FORMAT_RGBX8888

	// Planar YUV
	{ 2, 12 },	// PIXEL_FORMAT_NV12
	{ 2, 12 },	// PIXEL_FORMAT_NV21
	{ 3, 12 },	// PIXEL_FORMAT_YV12

	// ThorVG and compositor formats
	{ 1, 32 },	// PIXEL_FORMAT_ARGB8888
	{ 1, 32 },	// PIXEL_FORMAT_XRGB8888

	// Single-channel formats
	{ 1, 8 },	// PIXEL_FORMAT_A8
	{ 1, 8 }	// PIXEL_FORMAT_L8
};

static const uint32 kFormatCount = sizeof(kFormats) / sizeof(kFormats[0]);


uint32
planar_get_plane_count(pixel_format format)
{
	if (format >= kFormatCount)
		return 1;
	return kFormats[format].planeCount;
}


uint32
planar_get_bits_per_pixel(pixel_format format)
{
	if (format >= kFormatCount)
		return 32;
	return kFormats[format].bitsPerPixel;
}


bool
planar_is_format_planar(pixel_format format)
{
	return planar_get_plane_count(format) > 1;
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

	if (planeIndex == 0) {
		outInfo->width = width;
		outInfo->height = height;

		switch (format) {
			case PIXEL_FORMAT_RGBA8888:
			case PIXEL_FORMAT_BGRA8888:
			case PIXEL_FORMAT_RGBX8888:
			case PIXEL_FORMAT_ARGB8888:
			case PIXEL_FORMAT_XRGB8888:
				outInfo->bytesPerElement = 4;
				break;
			case PIXEL_FORMAT_RGB565:
				outInfo->bytesPerElement = 2;
				break;
			case PIXEL_FORMAT_A8:
			case PIXEL_FORMAT_L8:
			case PIXEL_FORMAT_NV12:
			case PIXEL_FORMAT_NV21:
			case PIXEL_FORMAT_YV12:
				outInfo->bytesPerElement = 1;
				break;
			default:
				outInfo->bytesPerElement = 4;
				break;
		}

		outInfo->bytesPerRow = (width * outInfo->bytesPerElement
			+ strideAlignment - 1) & ~(strideAlignment - 1);
		outInfo->offset = 0;
	} else if (format == PIXEL_FORMAT_NV12 || format == PIXEL_FORMAT_NV21) {
		outInfo->width = width / 2;
		outInfo->height = height / 2;
		outInfo->bytesPerElement = 2;
		outInfo->bytesPerRow = (outInfo->width * 2 + strideAlignment - 1)
			& ~(strideAlignment - 1);

		plane_info plane0;
		planar_calculate_plane(format, 0, width, height, strideAlignment,
			&plane0);
		outInfo->offset = plane0.bytesPerRow * plane0.height;
	} else if (format == PIXEL_FORMAT_YV12) {
		outInfo->width = width / 2;
		outInfo->height = height / 2;
		outInfo->bytesPerElement = 1;
		outInfo->bytesPerRow = (outInfo->width + strideAlignment - 1)
			& ~(strideAlignment - 1);

		plane_info plane0;
		planar_calculate_plane(format, 0, width, height, strideAlignment,
			&plane0);
		size_t plane0Size = plane0.bytesPerRow * plane0.height;
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
