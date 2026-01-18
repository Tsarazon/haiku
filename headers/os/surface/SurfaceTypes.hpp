/*
 * Copyright 2025 Mobile Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SURFACE_TYPES_HPP
#define _SURFACE_TYPES_HPP

#include <SupportDefs.h>

typedef uint32 surface_id;

enum {
	// 32-bit RGBA variants
	PIXEL_FORMAT_RGBA8888 = 0,
	PIXEL_FORMAT_BGRA8888,
	PIXEL_FORMAT_RGB565,
	PIXEL_FORMAT_RGBX8888,

	// Planar YUV
	PIXEL_FORMAT_NV12,
	PIXEL_FORMAT_NV21,
	PIXEL_FORMAT_YV12,

	// ThorVG and compositor formats
	PIXEL_FORMAT_ARGB8888,			// ThorVG native
	PIXEL_FORMAT_XRGB8888,			// Compositor, X11/DRM

	// Single-channel formats
	PIXEL_FORMAT_A8,				// Font glyphs, masks
	PIXEL_FORMAT_L8					// Grayscale
};
typedef uint32 pixel_format;

enum {
	SURFACE_USAGE_CPU_READ		= 0x0001,
	SURFACE_USAGE_CPU_WRITE		= 0x0002,
	SURFACE_USAGE_GPU_TEXTURE	= 0x0004,
	SURFACE_USAGE_GPU_RENDER	= 0x0008,
	SURFACE_USAGE_COMPOSITOR	= 0x0010,
	SURFACE_USAGE_VIDEO			= 0x0020,
	SURFACE_USAGE_CAMERA		= 0x0040,
	SURFACE_USAGE_PROTECTED		= 0x0080,
	SURFACE_USAGE_PURGEABLE		= 0x0100
};

enum {
	SURFACE_LOCK_READ_ONLY		= 0x0001,
	SURFACE_LOCK_AVOID_SYNC		= 0x0002
};

enum {
	SURFACE_PURGEABLE_NON_VOLATILE	= 0,
	SURFACE_PURGEABLE_VOLATILE		= 1,
	SURFACE_PURGEABLE_EMPTY			= 2,
	SURFACE_PURGEABLE_KEEP_CURRENT	= 3
};
typedef uint32 surface_purgeable_state;

enum {
	SURFACE_CACHE_DEFAULT			= 0,
	SURFACE_CACHE_INHIBIT			= 1,
	SURFACE_CACHE_WRITE_THROUGH		= 2,
	SURFACE_CACHE_WRITE_COMBINE		= 3
};

typedef struct {
	uint32			width;
	uint32			height;
	pixel_format	format;
	uint32			usage;
	uint32			bytesPerElement;
	uint32			bytesPerRow;
	uint32			cacheMode;
} surface_desc;

typedef struct {
	uint32			width;
	uint32			height;
	uint32			bytesPerElement;
	uint32			bytesPerRow;
	size_t			offset;
} plane_info;

enum {
	B_SURFACE_NOT_LOCKED = B_ERRORS_END + 0x1000,
	B_SURFACE_ALREADY_LOCKED,
	B_SURFACE_IN_USE,
	B_SURFACE_PURGEABLE
};

#ifdef __cplusplus

inline void
surface_desc_init(surface_desc* desc)
{
	desc->width = 0;
	desc->height = 0;
	desc->format = PIXEL_FORMAT_BGRA8888;
	desc->usage = SURFACE_USAGE_CPU_READ | SURFACE_USAGE_CPU_WRITE;
	desc->bytesPerElement = 0;
	desc->bytesPerRow = 0;
	desc->cacheMode = SURFACE_CACHE_DEFAULT;
}

#endif

#endif /* _SURFACE_TYPES_HPP */