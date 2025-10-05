/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Subpixel rendering - OPTIMIZED VERSION
 * Uses perceptual weighting (ITU-R BT.709 luma coefficients)
 */

#ifndef BLEND2D_DRAWING_MODE_SUBPIX_H
#define BLEND2D_DRAWING_MODE_SUBPIX_H

#include "PixelFormat.h"
#include "Blend2DDrawingMode.h"
#include "PatternHandler.h"
#include <blend2d.h>

// ============================================================================
// PERCEPTUAL SUBPIXEL COVERAGE AVERAGING
// ============================================================================

// ITU-R BT.709 luma coefficients for perceptually correct averaging
// More accurate than simple (R+G+B)/3
static inline uint8
subpix_average_coverage_perceptual(const uint8* covers)
{
    // covers[0] = R, covers[1] = G, covers[2] = B
    // Weights: R=0.2126, G=0.7152, B=0.0722
    // Integer approximation: (77*R + 150*G + 29*B + 128) >> 8
    return (covers[0] * 77 + covers[1] * 150 + covers[2] * 29 + 128) >> 8;
}

// Alternative: simple averaging (faster but less accurate)
static inline uint8
subpix_average_coverage_simple(const uint8* covers)
{
    return (covers[0] + covers[1] + covers[2]) / 3;
}

// Choose which to use (can be made configurable)
#define SUBPIX_AVERAGE subpix_average_coverage_perceptual

// ============================================================================
// SUBPIX WRAPPER MACRO
// ============================================================================

#define BLEND2D_SUBPIX_HSPAN(mode_name, pixel_func) \
static void \
blend_solid_hspan_##mode_name##_subpix(int x, int y, unsigned len, \
                                      const PixelFormat::color_type& c, \
                                      const uint8* covers, \
                                      BLImage* image, BLContext* ctx, \
                                      const PatternHandler* pattern) \
{ \
    unsigned pixelCount = len / 3; \
    \
    for (unsigned i = 0; i < pixelCount; i++, x++) { \
        uint8 avgCover = SUBPIX_AVERAGE(covers); \
        covers += 3; \
        \
        if (avgCover == 0) \
            continue; \
        \
        pixel_func(x, y, c, avgCover, image, ctx, pattern); \
    } \
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

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

// ============================================================================
// GENERATE SUBPIX VARIANTS
// ============================================================================

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

#undef BLEND2D_SUBPIX_HSPAN

#endif // BLEND2D_DRAWING_MODE_SUBPIX_H
