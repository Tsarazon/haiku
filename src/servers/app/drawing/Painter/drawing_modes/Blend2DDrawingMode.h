/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Base macros and utilities for Blend2D drawing modes.
 * This replaces the AGG-based DrawingMode.h infrastructure.
 */

#ifndef BLEND2D_DRAWING_MODE_H
#define BLEND2D_DRAWING_MODE_H

#include <blend2d.h>
#include <GraphicsDefs.h>
#include "drawing_support.h"

class PatternHandler;

// BLEND2D_BLEND
//
// This macro assumes source alpha in range 0..255 and performs
// standard alpha blending using Blend2D's native compositing.
// Format: BGRA32 (0xAARRGGBB in memory as BB GG RR AA)
#define BLEND2D_BLEND(d, r, g, b, a) \
{ \
	pixel32 _p; \
	_p.data32 = *(uint32*)d; \
	d[0] = (((((b) - _p.data8[0]) * (a)) + (_p.data8[0] << 8)) >> 8); \
	d[1] = (((((g) - _p.data8[1]) * (a)) + (_p.data8[1] << 8)) >> 8); \
	d[2] = (((((r) - _p.data8[2]) * (a)) + (_p.data8[2] << 8)) >> 8); \
	d[3] = 255; \
}

// BLEND2D_BLEND_SUBPIX
//
// Subpixel blending with per-channel alpha (for LCD subpixel rendering)
// NOTE: Blend2D doesn't natively support per-channel alpha, so we use
// simple grayscale averaging: alpha_avg = (a1 + a2 + a3) / 3
#define BLEND2D_BLEND_SUBPIX(d, r, g, b, a1, a2, a3) \
{ \
	uint8 alpha_avg = ((a1) + (a2) + (a3)) / 3; \
	BLEND2D_BLEND(d, r, g, b, alpha_avg); \
}

// BLEND2D_BLEND_FROM
//
// Blends between two colors (r1,g1,b1) and (r2,g2,b2) with alpha
// and writes the result to destination d.
#define BLEND2D_BLEND_FROM(d, r1, g1, b1, r2, g2, b2, a) \
{ \
	d[0] = (((((b2) - (b1)) * (a)) + ((b1) << 8)) >> 8); \
	d[1] = (((((g2) - (g1)) * (a)) + ((g1) << 8)) >> 8); \
	d[2] = (((((r2) - (r1)) * (a)) + ((r1) << 8)) >> 8); \
	d[3] = 255; \
}

// BLEND2D_BLEND_FROM_SUBPIX
#define BLEND2D_BLEND_FROM_SUBPIX(d, r1, g1, b1, r2, g2, b2, a1, a2, a3) \
{ \
	uint8 alpha_avg = ((a1) + (a2) + (a3)) / 3; \
	BLEND2D_BLEND_FROM(d, r1, g1, b1, r2, g2, b2, alpha_avg); \
}

// BLEND2D_BLEND16
//
// This macro assumes source alpha in range 0..65025
#define BLEND2D_BLEND16(d, r, g, b, a) \
{ \
	pixel32 _p; \
	_p.data32 = *(uint32*)d; \
	d[0] = (((((b) - _p.data8[0]) * (a)) + (_p.data8[0] << 16)) >> 16); \
	d[1] = (((((g) - _p.data8[1]) * (a)) + (_p.data8[1] << 16)) >> 16); \
	d[2] = (((((r) - _p.data8[2]) * (a)) + (_p.data8[2] << 16)) >> 16); \
	d[3] = 255; \
}

// BLEND2D_BLEND16_SUBPIX
#define BLEND2D_BLEND16_SUBPIX(d, r, g, b, a1, a2, a3) \
{ \
	uint16 alpha_avg = (uint16)(((a1) + (a2) + (a3)) / 3); \
	BLEND2D_BLEND16(d, r, g, b, alpha_avg); \
}

// BLEND2D_COMPOSITE
//
// Composite blending with semi-transparent background support
#define BLEND2D_COMPOSITE(d, r, g, b, a) \
{ \
	pixel32 _p; \
	_p.data32 = *(uint32*)d; \
	if (_p.data8[3] == 255) { \
		d[0] = (((((b) - _p.data8[0]) * (a)) + (_p.data8[0] << 8)) >> 8); \
		d[1] = (((((g) - _p.data8[1]) * (a)) + (_p.data8[1] << 8)) >> 8); \
		d[2] = (((((r) - _p.data8[2]) * (a)) + (_p.data8[2] << 8)) >> 8); \
		d[3] = 255; \
	} else { \
		if (_p.data8[3] == 0) { \
			d[0] = (b); \
			d[1] = (g); \
			d[2] = (r); \
			d[3] = (a); \
		} else { \
			uint8 alphaRest = 255 - (a); \
			uint32 alphaTemp = (65025 - alphaRest * (255 - _p.data8[3])); \
			uint32 alphaDest = _p.data8[3] * alphaRest; \
			uint32 alphaSrc = 255 * (a); \
			d[0] = (_p.data8[0] * alphaDest + (b) * alphaSrc) / alphaTemp; \
			d[1] = (_p.data8[1] * alphaDest + (g) * alphaSrc) / alphaTemp; \
			d[2] = (_p.data8[2] * alphaDest + (r) * alphaSrc) / alphaTemp; \
			d[3] = alphaTemp / 255; \
		} \
	} \
}

// BLEND2D_COMPOSITE_SUBPIX
#define BLEND2D_COMPOSITE_SUBPIX(d, r, g, b, a1, a2, a3) \
{ \
	uint8 alpha_avg = ((a1) + (a2) + (a3)) / 3; \
	BLEND2D_COMPOSITE(d, r, g, b, alpha_avg); \
}

// BLEND2D_COMPOSITE16
#define BLEND2D_COMPOSITE16(d, r, g, b, a) \
{ \
	uint16 _a = (a) / 255; \
	BLEND2D_COMPOSITE(d, r, g, b, _a); \
}

// BLEND2D_COMPOSITE16_SUBPIX
#define BLEND2D_COMPOSITE16_SUBPIX(d, r, g, b, a1, a2, a3) \
{ \
	uint16 alpha_avg = (uint16)(((a1) + (a2) + (a3)) / 3); \
	BLEND2D_COMPOSITE(d, r, g, b, alpha_avg); \
}

// Brightness calculation (same as AGG version)
static inline uint8
brightness_for(uint8 red, uint8 green, uint8 blue)
{
	// brightness = 0.301 * red + 0.586 * green + 0.113 * blue
	// we use for performance reasons:
	// brightness = (308 * red + 600 * green + 116 * blue) / 1024
	return uint8((308 * red + 600 * green + 116 * blue) / 1024);
}

#endif // BLEND2D_DRAWING_MODE_H
