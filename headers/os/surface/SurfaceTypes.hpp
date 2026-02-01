/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_SURFACE_TYPES_HPP
#define _KOSM_SURFACE_TYPES_HPP

#include <SupportDefs.h>

typedef uint32 kosm_surface_id;

enum kosm_pixel_format {
	KOSM_PIXEL_FORMAT_ARGB8888 = 0,
	KOSM_PIXEL_FORMAT_BGRA8888,
	KOSM_PIXEL_FORMAT_RGBA8888,
	KOSM_PIXEL_FORMAT_RGBX8888,
	KOSM_PIXEL_FORMAT_XRGB8888,
	KOSM_PIXEL_FORMAT_RGB565,

	KOSM_PIXEL_FORMAT_NV12,
	KOSM_PIXEL_FORMAT_NV21,
	KOSM_PIXEL_FORMAT_YV12,

	KOSM_PIXEL_FORMAT_A8,
	KOSM_PIXEL_FORMAT_L8,

	KOSM_PIXEL_FORMAT_COUNT
};

enum {
	KOSM_SURFACE_USAGE_CPU_READ		= 0x0001,
	KOSM_SURFACE_USAGE_CPU_WRITE	= 0x0002,
	KOSM_SURFACE_USAGE_GPU_TEXTURE	= 0x0004,
	KOSM_SURFACE_USAGE_GPU_RENDER	= 0x0008,
	KOSM_SURFACE_USAGE_COMPOSITOR	= 0x0010,
	KOSM_SURFACE_USAGE_VIDEO		= 0x0020,
	KOSM_SURFACE_USAGE_CAMERA		= 0x0040,
	KOSM_SURFACE_USAGE_PROTECTED	= 0x0080,
	KOSM_SURFACE_USAGE_PURGEABLE	= 0x0100
};

enum {
	KOSM_SURFACE_LOCK_READ_ONLY		= 0x0001,
	KOSM_SURFACE_LOCK_AVOID_SYNC	= 0x0002
};

enum kosm_purgeable_state {
	KOSM_PURGEABLE_NON_VOLATILE	= 0,
	KOSM_PURGEABLE_VOLATILE		= 1,
	KOSM_PURGEABLE_EMPTY		= 2,
	KOSM_PURGEABLE_KEEP_CURRENT	= 3
};

enum {
	KOSM_CACHE_DEFAULT			= 0,
	KOSM_CACHE_INHIBIT			= 1,
	KOSM_CACHE_WRITE_THROUGH	= 2,
	KOSM_CACHE_WRITE_COMBINE	= 3
};

struct KosmSurfaceDesc {
	uint32				width;
	uint32				height;
	kosm_pixel_format	format;
	uint32				usage;
	uint32				bytesPerElement;
	uint32				bytesPerRow;
	uint32				cacheMode;

	KosmSurfaceDesc()
		:
		width(0),
		height(0),
		format(KOSM_PIXEL_FORMAT_ARGB8888),
		usage(KOSM_SURFACE_USAGE_CPU_READ | KOSM_SURFACE_USAGE_CPU_WRITE),
		bytesPerElement(0),
		bytesPerRow(0),
		cacheMode(KOSM_CACHE_DEFAULT)
	{
	}
};

struct KosmPlaneInfo {
	uint32			width;
	uint32			height;
	uint32			bytesPerElement;
	uint32			bytesPerRow;
	size_t			offset;
};

struct KosmSurfaceToken {
	kosm_surface_id	id;
	uint64			secret;
	uint32			generation;
};

enum {
	KOSM_SURFACE_NOT_LOCKED = B_ERRORS_END + 0x1000,
	KOSM_SURFACE_ALREADY_LOCKED,
	KOSM_SURFACE_IN_USE,
	KOSM_SURFACE_PURGED,
	KOSM_SURFACE_ID_EXISTS
};

#endif
