/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Native Blend2D drawing modes - OPTIMIZED VERSION
 * Uses batch rendering helpers for 10-100x performance improvement
 */

#ifndef BLEND2D_DRAWING_MODE_NATIVE_H
#define BLEND2D_DRAWING_MODE_NATIVE_H

#include "PixelFormat.h"
#include "PatternHandler.h"
#include "Blend2DDrawingModeHelpers.h"
#include <blend2d.h>

// ============================================================================
// SINGLE PIXEL HELPERS (unchanged - only used for single pixels)
// ============================================================================

static inline void
blend2d_draw_pixel(int x, int y, const rgb_color& color, uint8 cover,
                  BLContext* ctx, BLCompOp compOp)
{
    ctx->setCompOp(compOp);

    if (cover == 255) {
        ctx->fillRect(BLRect(x, y, 1, 1),
                     BLRgba32(color.red, color.green, color.blue, color.alpha));
    } else {
        double prevAlpha = ctx->globalAlpha();
        ctx->setGlobalAlpha((cover / 255.0) * (color.alpha / 255.0));
        ctx->fillRect(BLRect(x, y, 1, 1),
                     BLRgba32(color.red, color.green, color.blue, 255));
        ctx->setGlobalAlpha(prevAlpha);
    }
}

// ============================================================================
// MACRO FOR GENERATING ALL 8 FUNCTIONS FOR A DRAWING MODE
// ============================================================================

#define GENERATE_NATIVE_MODE_FUNCTIONS(mode_name, compop) \
    /* 1. blend_pixel - single pixel */ \
    static void \
    blend_pixel_##mode_name##_native(int x, int y, \
                                     const PixelFormat::color_type& c, \
                                     uint8 cover, BLImage* image, BLContext* ctx, \
                                     const PatternHandler* pattern) \
    { \
        rgb_color color = pattern->ColorAt(x, y); \
        blend2d_draw_pixel(x, y, color, cover, ctx, compop); \
    } \
    \
    /* 2. blend_hline - horizontal line with uniform coverage */ \
    static void \
    blend_hline_##mode_name##_native(int x, int y, unsigned len, \
                                     const PixelFormat::color_type& c, \
                                     uint8 cover, BLImage* image, BLContext* ctx, \
                                     const PatternHandler* pattern) \
    { \
        if (pattern->IsSolid()) { \
            /* Fast path: solid color */ \
            rgb_color color = pattern->HighColor(); \
            ctx->setCompOp(compop); \
            if (cover == 255) { \
                ctx->fillRect(BLRect(x, y, len, 1), \
                             BLRgba32(color.red, color.green, color.blue, \
                                     color.alpha)); \
            } else { \
                double prevAlpha = ctx->globalAlpha(); \
                ctx->setGlobalAlpha((cover / 255.0) * (color.alpha / 255.0)); \
                ctx->fillRect(BLRect(x, y, len, 1), \
                             BLRgba32(color.red, color.green, color.blue, 255)); \
                ctx->setGlobalAlpha(prevAlpha); \
            } \
        } else { \
            /* Pattern: use helper with temporary buffer */ \
            Blend2DHelpers::render_pattern_hspan_uniform(x, y, len, pattern, \
                                                         cover, ctx, compop); \
        } \
    } \
    \
    /* 3. blend_vline - vertical line */ \
    static void \
    blend_vline_##mode_name##_native(int x, int y, unsigned len, \
                                     const PixelFormat::color_type& c, \
                                     uint8 cover, BLImage* image, BLContext* ctx, \
                                     const PatternHandler* pattern) \
    { \
        if (pattern->IsSolid()) { \
            rgb_color color = pattern->HighColor(); \
            ctx->setCompOp(compop); \
            if (cover == 255) { \
                ctx->fillRect(BLRect(x, y, 1, len), \
                             BLRgba32(color.red, color.green, color.blue, \
                                     color.alpha)); \
            } else { \
                double prevAlpha = ctx->globalAlpha(); \
                ctx->setGlobalAlpha((cover / 255.0) * (color.alpha / 255.0)); \
                ctx->fillRect(BLRect(x, y, 1, len), \
                             BLRgba32(color.red, color.green, color.blue, 255)); \
                ctx->setGlobalAlpha(prevAlpha); \
            } \
        } else { \
            /* Pattern: render pixel by pixel for vertical (rare case) */ \
            for (unsigned i = 0; i < len; i++, y++) { \
                rgb_color color = pattern->ColorAt(x, y); \
                blend2d_draw_pixel(x, y, color, cover, ctx, compop); \
            } \
        } \
    } \
    \
    /* 4. blend_solid_hspan - horizontal span with coverage array */ \
    static void \
    blend_solid_hspan_##mode_name##_native(int x, int y, unsigned len, \
                                           const PixelFormat::color_type& c, \
                                           const uint8* covers, \
                                           BLImage* image, BLContext* ctx, \
                                           const PatternHandler* pattern) \
    { \
        if (pattern->IsSolid()) { \
            /* Fast path: batch render with grouped runs */ \
            rgb_color color = pattern->HighColor(); \
            Blend2DHelpers::render_solid_hspan_batch(x, y, len, color, \
                                                     covers, ctx, compop); \
        } else { \
            /* Pattern: use helper with temporary buffer */ \
            Blend2DHelpers::render_pattern_hspan(x, y, len, pattern, \
                                                 covers, ctx, compop); \
        } \
    } \
    \
    /* 5. blend_solid_vspan - vertical span */ \
    static void \
    blend_solid_vspan_##mode_name##_native(int x, int y, unsigned len, \
                                           const PixelFormat::color_type& c, \
                                           const uint8* covers, \
                                           BLImage* image, BLContext* ctx, \
                                           const PatternHandler* pattern) \
    { \
        ctx->setCompOp(compop); \
        for (unsigned i = 0; i < len; i++, y++, covers++) { \
            if (*covers == 0) \
                continue; \
            rgb_color color = pattern->ColorAt(x, y); \
            blend2d_draw_pixel(x, y, color, *covers, ctx, compop); \
        } \
    } \
    \
    /* 6. blend_color_hspan - horizontal span with color array */ \
    static void \
    blend_color_hspan_##mode_name##_native(int x, int y, unsigned len, \
                                           const PixelFormat::color_type* colors, \
                                           const uint8* covers, uint8 cover, \
                                           BLImage* image, BLContext* ctx, \
                                           const PatternHandler* pattern) \
    { \
        ctx->setCompOp(compop); \
        for (unsigned i = 0; i < len; i++, x++) { \
            uint8 alpha = covers ? covers[i] : cover; \
            if (alpha == 0) \
                continue; \
            const PixelFormat::color_type& pixelColor = colors[i]; \
            if (alpha == 255) { \
                ctx->fillRect(BLRect(x, y, 1, 1), \
                             BLRgba32(pixelColor.r, pixelColor.g, pixelColor.b, \
                                     pixelColor.a)); \
            } else { \
                double prevAlpha = ctx->globalAlpha(); \
                ctx->setGlobalAlpha((alpha / 255.0) * (pixelColor.a / 255.0)); \
                ctx->fillRect(BLRect(x, y, 1, 1), \
                             BLRgba32(pixelColor.r, pixelColor.g, pixelColor.b, 255)); \
                ctx->setGlobalAlpha(prevAlpha); \
            } \
        } \
    } \
    \
    /* 7. blend_color_vspan - vertical span with color array */ \
    static void \
    blend_color_vspan_##mode_name##_native(int x, int y, unsigned len, \
                                           const PixelFormat::color_type* colors, \
                                           const uint8* covers, uint8 cover, \
                                           BLImage* image, BLContext* ctx, \
                                           const PatternHandler* pattern) \
    { \
        ctx->setCompOp(compop); \
        for (unsigned i = 0; i < len; i++, y++) { \
            uint8 alpha = covers ? covers[i] : cover; \
            if (alpha == 0) \
                continue; \
            const PixelFormat::color_type& pixelColor = colors[i]; \
            if (alpha == 255) { \
                ctx->fillRect(BLRect(x, y, 1, 1), \
                             BLRgba32(pixelColor.r, pixelColor.g, pixelColor.b, \
                                     pixelColor.a)); \
            } else { \
                double prevAlpha = ctx->globalAlpha(); \
                ctx->setGlobalAlpha((alpha / 255.0) * (pixelColor.a / 255.0)); \
                ctx->fillRect(BLRect(x, y, 1, 1), \
                             BLRgba32(pixelColor.r, pixelColor.g, pixelColor.b, 255)); \
                ctx->setGlobalAlpha(prevAlpha); \
            } \
        } \
    }

// ============================================================================
// GENERATE ALL NATIVE DRAWING MODES
// ============================================================================

GENERATE_NATIVE_MODE_FUNCTIONS(copy, BL_COMP_OP_SRC_COPY)
GENERATE_NATIVE_MODE_FUNCTIONS(over, BL_COMP_OP_SRC_OVER)
GENERATE_NATIVE_MODE_FUNCTIONS(add, BL_COMP_OP_PLUS)
GENERATE_NATIVE_MODE_FUNCTIONS(subtract, BL_COMP_OP_MINUS)
GENERATE_NATIVE_MODE_FUNCTIONS(min, BL_COMP_OP_DARKEN)
GENERATE_NATIVE_MODE_FUNCTIONS(max, BL_COMP_OP_LIGHTEN)

#undef GENERATE_NATIVE_MODE_FUNCTIONS

#endif // BLEND2D_DRAWING_MODE_NATIVE_H
