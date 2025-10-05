/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Unified subpixel rendering for all Blend2D drawing modes.
 * Uses Simple Grayscale Averaging: alpha_avg = (a1 + a2 + a3) / 3
 */

#ifndef BLEND2D_DRAWING_MODE_SUBPIX_H
#define BLEND2D_DRAWING_MODE_SUBPIX_H

#include "PixelFormat.h"
#include "Blend2DDrawingMode.h"
#include "PatternHandler.h"
#include <blend2d.h>

// ============================================================================
// Subpixel Coverage Averaging Helper
// ============================================================================

// Simple Grayscale Averaging: converts 3 subpixel coverage values (R,G,B)
// into a single averaged alpha value suitable for standard blending.
static inline uint8
subpix_average_coverage(const uint8* covers)
{
	// covers[0] = R coverage, covers[1] = G coverage, covers[2] = B coverage
	return (covers[0] + covers[1] + covers[2]) / 3;
}

// ============================================================================
// SUBPIX Wrapper Macro
// ============================================================================

// This macro wraps any drawing mode's pixel function to handle subpixel
// rendering by averaging the three subpixel coverage values.
#define BLEND2D_SUBPIX_HSPAN(mode_name, pixel_func) \
static void \
blend_solid_hspan_##mode_name##_subpix(int x, int y, unsigned len, \
									  const PixelFormat::color_type& c, \
									  const uint8* covers, \
									  BLImage* image, BLContext* ctx, \
									  const PatternHandler* pattern) \
{ \
	/* Subpixel array has 3 values per pixel */ \
	unsigned pixelCount = len / 3; \
	\
	for (unsigned i = 0; i < pixelCount; i++, x++) { \
		/* Average the 3 subpixel coverage values */ \
		uint8 avgCover = subpix_average_coverage(covers); \
		covers += 3; \
		\
		if (avgCover == 0) \
			continue; \
		\
		/* Call the base pixel function with averaged coverage */ \
		pixel_func(x, y, c, avgCover, image, ctx, pattern); \
	} \
}

// ============================================================================
// SUBPIX implementations for all drawing modes
// ============================================================================

// Forward declare all pixel functions
// (These will be defined in Blend2DDrawingModeNative.h and Custom.h)
static void blend_pixel_copy_native(int x, int y,
	const PixelFormat::color_type& c, uint8 cover,
	BLImage* image, BLContext* ctx, const PatternHandler* pattern);

static void blend_pixel_over_native(int x, int y,
	const PixelFormat::color_type& c, uint8 cover,
	BLImage* image, BLContext* ctx, const PatternHandler* pattern);

static void blend_pixel_add_native(int x, int y,
	const PixelFormat::color_type& c, uint8 cover,
	BLImage* image, BLContext* ctx, const PatternHandler* pattern);

static void blend_pixel_subtract_native(int x, int y,
	const PixelFormat::color_type& c, uint8 cover,
	BLImage* image, BLContext* ctx, const PatternHandler* pattern);

static void blend_pixel_min_native(int x, int y,
	const PixelFormat::color_type& c, uint8 cover,
	BLImage* image, BLContext* ctx, const PatternHandler* pattern);

static void blend_pixel_max_native(int x, int y,
	const PixelFormat::color_type& c, uint8 cover,
	BLImage* image, BLContext* ctx, const PatternHandler* pattern);

static void blend_pixel_blend_custom(int x, int y,
	const PixelFormat::color_type& c, uint8 cover,
	BLImage* image, BLContext* ctx, const PatternHandler* pattern);

static void blend_pixel_invert_custom(int x, int y,
	const PixelFormat::color_type& c, uint8 cover,
	BLImage* image, BLContext* ctx, const PatternHandler* pattern);

static void blend_pixel_select_custom(int x, int y,
	const PixelFormat::color_type& c, uint8 cover,
	BLImage* image, BLContext* ctx, const PatternHandler* pattern);

static void blend_pixel_erase_custom(int x, int y,
	const PixelFormat::color_type& c, uint8 cover,
	BLImage* image, BLContext* ctx, const PatternHandler* pattern);

// Generate SUBPIX variants for all modes using the macro
BLEND2D_SUBPIX_HSPAN(copy, blend_pixel_copy_native)
BLEND2D_SUBPIX_HSPAN(over, blend_pixel_over_native)
BLEND2D_SUBPIX_HSPAN(add, blend_pixel_add_native)
BLEND2D_SUBPIX_HSPAN(subtract, blend_pixel_subtract_native)
BLEND2D_SUBPIX_HSPAN(min, blend_pixel_min_native)
BLEND2D_SUBPIX_HSPAN(max, blend_pixel_max_native)
BLEND2D_SUBPIX_HSPAN(blend, blend_pixel_blend_custom)
BLEND2D_SUBPIX_HSPAN(invert, blend_pixel_invert_custom)
BLEND2D_SUBPIX_HSPAN(select, blend_pixel_select_custom)
BLEND2D_SUBPIX_HSPAN(erase, blend_pixel_erase_custom)

// Clean up macro
#undef BLEND2D_SUBPIX_HSPAN

// ============================================================================
// Notes on Implementation
// ============================================================================

/*
 * SIMPLE GRAYSCALE AVERAGING:
 * 
 * Blend2D doesn't natively support per-channel subpixel alpha rendering.
 * Instead of separate R/G/B alpha values for LCD subpixel anti-aliasing,
 * we compute a single averaged alpha:
 * 
 *   alpha_avg = (coverR + coverG + coverB) / 3
 * 
 * This provides acceptable visual results for LCD text rendering while
 * keeping the implementation simple and maintainable.
 * 
 * FUTURE IMPROVEMENTS:
 * 
 * 1. Weighted averaging (more perceptually accurate):
 *    alpha_avg = (coverR * 30 + coverG * 59 + coverB * 11) / 100
 *    (just change subpix_average_coverage() function)
 * 
 * 2. SIMD optimization for averaging operation
 * 
 * 3. True per-channel rendering if Blend2D adds support
 */

#endif // BLEND2D_DRAWING_MODE_SUBPIX_H
