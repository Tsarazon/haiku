/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Porter-Duff alpha compositing modes - OPTIMIZED VERSION
 */

#ifndef BLEND2D_DRAWING_MODE_ALPHA_H
#define BLEND2D_DRAWING_MODE_ALPHA_H

#include "PixelFormat.h"
#include "PatternHandler.h"
#include "Blend2DDrawingModeHelpers.h"
#include <blend2d.h>

// Forward declare helper from Native.h
static inline void blend2d_draw_pixel(int x, int y, const rgb_color& color,
                                     uint8 cover, BLContext* ctx, BLCompOp compOp);

// ============================================================================
// MACRO FOR PORTER-DUFF OPERATORS
// ============================================================================

#define GENERATE_PORTER_DUFF_FUNCTIONS(mode_name, compop) \
    /* 1. blend_pixel */ \
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
    /* 2. blend_hline */ \
    static void \
    blend_hline_##mode_name##_native(int x, int y, unsigned len, \
                                     const PixelFormat::color_type& c, \
                                     uint8 cover, BLImage* image, BLContext* ctx, \
                                     const PatternHandler* pattern) \
    { \
        if (pattern->IsSolid()) { \
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
            Blend2DHelpers::render_pattern_hspan_uniform(x, y, len, pattern, \
                                                         cover, ctx, compop); \
        } \
    } \
    \
    /* 3. blend_vline */ \
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
            for (unsigned i = 0; i < len; i++, y++) { \
                rgb_color color = pattern->ColorAt(x, y); \
                blend2d_draw_pixel(x, y, color, cover, ctx, compop); \
            } \
        } \
    } \
    \
    /* 4. blend_solid_hspan */ \
    static void \
    blend_solid_hspan_##mode_name##_native(int x, int y, unsigned len, \
                                           const PixelFormat::color_type& c, \
                                           const uint8* covers, \
                                           BLImage* image, BLContext* ctx, \
                                           const PatternHandler* pattern) \
    { \
        if (pattern->IsSolid()) { \
            rgb_color color = pattern->HighColor(); \
            Blend2DHelpers::render_solid_hspan_batch(x, y, len, color, \
                                                     covers, ctx, compop); \
        } else { \
            Blend2DHelpers::render_pattern_hspan(x, y, len, pattern, \
                                                 covers, ctx, compop); \
        } \
    } \
    \
    /* 5. blend_solid_vspan */ \
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
    /* 6. blend_color_hspan */ \
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
    /* 7. blend_color_vspan */ \
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
// GENERATE ALL PORTER-DUFF ALPHA MODES
// ============================================================================

GENERATE_PORTER_DUFF_FUNCTIONS(src_in, BL_COMP_OP_SRC_IN)
GENERATE_PORTER_DUFF_FUNCTIONS(src_out, BL_COMP_OP_SRC_OUT)
GENERATE_PORTER_DUFF_FUNCTIONS(src_atop, BL_COMP_OP_SRC_ATOP)
GENERATE_PORTER_DUFF_FUNCTIONS(dst_over, BL_COMP_OP_DST_OVER)
GENERATE_PORTER_DUFF_FUNCTIONS(dst_in, BL_COMP_OP_DST_IN)
GENERATE_PORTER_DUFF_FUNCTIONS(dst_out, BL_COMP_OP_DST_OUT)
GENERATE_PORTER_DUFF_FUNCTIONS(dst_atop, BL_COMP_OP_DST_ATOP)
GENERATE_PORTER_DUFF_FUNCTIONS(xor, BL_COMP_OP_XOR)
GENERATE_PORTER_DUFF_FUNCTIONS(clear, BL_COMP_OP_CLEAR)
GENERATE_PORTER_DUFF_FUNCTIONS(difference, BL_COMP_OP_DIFFERENCE)

#undef GENERATE_PORTER_DUFF_FUNCTIONS

#endif // BLEND2D_DRAWING_MODE_ALPHA_H
