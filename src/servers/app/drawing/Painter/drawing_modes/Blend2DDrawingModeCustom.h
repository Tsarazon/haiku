/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Custom drawing modes - OPTIMIZED VERSION
 * Uses BatchPixelAccess for efficient direct pixel manipulation
 */

#ifndef BLEND2D_DRAWING_MODE_CUSTOM_H
#define BLEND2D_DRAWING_MODE_CUSTOM_H

#include "PixelFormat.h"
#include "Blend2DDrawingMode.h"
#include "Blend2DDrawingModeHelpers.h"
#include "PatternHandler.h"
#include "drawing_support.h"
#include <blend2d.h>

// ============================================================================
// B_OP_BLEND - Averaging: (src + dst) / 2
// ============================================================================

static void
blend_pixel_blend_custom(int x, int y,
                        const PixelFormat::color_type& c,
                        uint8 cover, BLImage* image, BLContext* ctx,
                        const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    uint32_t* p = pixels.PixelAt(x, y);
    rgb_color color = pattern->ColorAt(x, y);

    // Extract destination (BGRA format)
    uint8_t dst_b = (*p) & 0xFF;
    uint8_t dst_g = (*p >> 8) & 0xFF;
    uint8_t dst_r = (*p >> 16) & 0xFF;

    // Average
    uint8_t avg_r = (color.red + dst_r) / 2;
    uint8_t avg_g = (color.green + dst_g) / 2;
    uint8_t avg_b = (color.blue + dst_b) / 2;

    if (cover == 255) {
        *p = 0xFF000000 | (avg_r << 16) | (avg_g << 8) | avg_b;
    } else {
        // Blend with coverage
        uint8_t final_r = (avg_r * cover + dst_r * (255 - cover)) / 255;
        uint8_t final_g = (avg_g * cover + dst_g * (255 - cover)) / 255;
        uint8_t final_b = (avg_b * cover + dst_b * (255 - cover)) / 255;
        *p = 0xFF000000 | (final_r << 16) | (final_g << 8) | final_b;
    }
}

static void
blend_hline_blend_custom(int x, int y, unsigned len,
                        const PixelFormat::color_type& c,
                        uint8 cover, BLImage* image, BLContext* ctx,
                        const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++) {
        blend_pixel_blend_custom(x + i, y, c, cover, image, ctx, pattern);
    }
}

static void
blend_vline_blend_custom(int x, int y, unsigned len,
                        const PixelFormat::color_type& c,
                        uint8 cover, BLImage* image, BLContext* ctx,
                        const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, y++) {
        blend_pixel_blend_custom(x, y, c, cover, image, ctx, pattern);
    }
}

static void
blend_solid_hspan_blend_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type& c,
                               const uint8* covers, BLImage* image,
                               BLContext* ctx, const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, x++, covers++) {
        if (*covers == 0)
            continue;
        blend_pixel_blend_custom(x, y, c, *covers, image, ctx, pattern);
    }
}

static void
blend_solid_vspan_blend_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type& c,
                               const uint8* covers, BLImage* image,
                               BLContext* ctx, const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, y++, covers++) {
        if (*covers == 0)
            continue;
        blend_pixel_blend_custom(x, y, c, *covers, image, ctx, pattern);
    }
}

static void
blend_color_hspan_blend_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type* colors,
                               const uint8* covers, uint8 cover,
                               BLImage* image, BLContext* ctx,
                               const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, x++) {
        uint8 alpha = covers ? covers[i] : cover;
        if (alpha == 0)
            continue;
        blend_pixel_blend_custom(x, y, colors[i], alpha, image, ctx, pattern);
    }
}

static void
blend_color_vspan_blend_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type* colors,
                               const uint8* covers, uint8 cover,
                               BLImage* image, BLContext* ctx,
                               const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, y++) {
        uint8 alpha = covers ? covers[i] : cover;
        if (alpha == 0)
            continue;
        blend_pixel_blend_custom(x, y, colors[i], alpha, image, ctx, pattern);
    }
}

// ============================================================================
// B_OP_INVERT - Invert RGB: 255 - dst
// ============================================================================

static void
blend_pixel_invert_custom(int x, int y,
                         const PixelFormat::color_type& c,
                         uint8 cover, BLImage* image, BLContext* ctx,
                         const PatternHandler* pattern)
{
    if (!pattern->IsHighColor(x, y))
        return;

    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    uint32_t* p = pixels.PixelAt(x, y);

    // Invert
    uint8_t r = 255 - ((*p >> 16) & 0xFF);
    uint8_t g = 255 - ((*p >> 8) & 0xFF);
    uint8_t b = 255 - ((*p) & 0xFF);

    if (cover == 255) {
        *p = 0xFF000000 | (r << 16) | (g << 8) | b;
    } else {
        uint8_t dst_r = (*p >> 16) & 0xFF;
        uint8_t dst_g = (*p >> 8) & 0xFF;
        uint8_t dst_b = (*p) & 0xFF;

        uint8_t final_r = (r * cover + dst_r * (255 - cover)) / 255;
        uint8_t final_g = (g * cover + dst_g * (255 - cover)) / 255;
        uint8_t final_b = (b * cover + dst_b * (255 - cover)) / 255;
        *p = 0xFF000000 | (final_r << 16) | (final_g << 8) | final_b;
    }
}

static void
blend_hline_invert_custom(int x, int y, unsigned len,
                         const PixelFormat::color_type& c,
                         uint8 cover, BLImage* image, BLContext* ctx,
                         const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++) {
        blend_pixel_invert_custom(x + i, y, c, cover, image, ctx, pattern);
    }
}

static void
blend_vline_invert_custom(int x, int y, unsigned len,
                         const PixelFormat::color_type& c,
                         uint8 cover, BLImage* image, BLContext* ctx,
                         const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, y++) {
        blend_pixel_invert_custom(x, y, c, cover, image, ctx, pattern);
    }
}

static void
blend_solid_hspan_invert_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type& c,
                               const uint8* covers, BLImage* image,
                               BLContext* ctx, const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, x++, covers++) {
        if (*covers == 0)
            continue;
        blend_pixel_invert_custom(x, y, c, *covers, image, ctx, pattern);
    }
}

static void
blend_solid_vspan_invert_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type& c,
                               const uint8* covers, BLImage* image,
                               BLContext* ctx, const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, y++, covers++) {
        if (*covers == 0)
            continue;
        blend_pixel_invert_custom(x, y, c, *covers, image, ctx, pattern);
    }
}

static void
blend_color_hspan_invert_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type* colors,
                               const uint8* covers, uint8 cover,
                               BLImage* image, BLContext* ctx,
                               const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, x++) {
        uint8 alpha = covers ? covers[i] : cover;
        if (alpha == 0)
            continue;
        blend_pixel_invert_custom(x, y, colors[i], alpha, image, ctx, pattern);
    }
}

static void
blend_color_vspan_invert_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type* colors,
                               const uint8* covers, uint8 cover,
                               BLImage* image, BLContext* ctx,
                               const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, y++) {
        uint8 alpha = covers ? covers[i] : cover;
        if (alpha == 0)
            continue;
        blend_pixel_invert_custom(x, y, colors[i], alpha, image, ctx, pattern);
    }
}

// ============================================================================
// B_OP_SELECT - Conditional color swap
// ============================================================================

static void
blend_pixel_select_custom(int x, int y,
                         const PixelFormat::color_type& c,
                         uint8 cover, BLImage* image, BLContext* ctx,
                         const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    uint32_t* p = pixels.PixelAt(x, y);
    rgb_color color = pattern->ColorAt(x, y);

    if (cover == 255) {
        *p = 0xFF000000 | (color.red << 16) | (color.green << 8) | color.blue;
    } else {
        uint8_t dst_r = (*p >> 16) & 0xFF;
        uint8_t dst_g = (*p >> 8) & 0xFF;
        uint8_t dst_b = (*p) & 0xFF;

        uint8_t final_r = (color.red * cover + dst_r * (255 - cover)) / 255;
        uint8_t final_g = (color.green * cover + dst_g * (255 - cover)) / 255;
        uint8_t final_b = (color.blue * cover + dst_b * (255 - cover)) / 255;
        *p = 0xFF000000 | (final_r << 16) | (final_g << 8) | final_b;
    }
}

static void
blend_hline_select_custom(int x, int y, unsigned len,
                         const PixelFormat::color_type& c,
                         uint8 cover, BLImage* image, BLContext* ctx,
                         const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++) {
        blend_pixel_select_custom(x + i, y, c, cover, image, ctx, pattern);
    }
}

static void
blend_vline_select_custom(int x, int y, unsigned len,
                         const PixelFormat::color_type& c,
                         uint8 cover, BLImage* image, BLContext* ctx,
                         const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, y++) {
        blend_pixel_select_custom(x, y, c, cover, image, ctx, pattern);
    }
}

static void
blend_solid_hspan_select_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type& c,
                               const uint8* covers, BLImage* image,
                               BLContext* ctx, const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, x++, covers++) {
        if (*covers == 0)
            continue;
        blend_pixel_select_custom(x, y, c, *covers, image, ctx, pattern);
    }
}

static void
blend_solid_vspan_select_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type& c,
                               const uint8* covers, BLImage* image,
                               BLContext* ctx, const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, y++, covers++) {
        if (*covers == 0)
            continue;
        blend_pixel_select_custom(x, y, c, *covers, image, ctx, pattern);
    }
}

static void
blend_color_hspan_select_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type* colors,
                               const uint8* covers, uint8 cover,
                               BLImage* image, BLContext* ctx,
                               const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, x++) {
        uint8 alpha = covers ? covers[i] : cover;
        if (alpha == 0)
            continue;
        blend_pixel_select_custom(x, y, colors[i], alpha, image, ctx, pattern);
    }
}

static void
blend_color_vspan_select_custom(int x, int y, unsigned len,
                               const PixelFormat::color_type* colors,
                               const uint8* covers, uint8 cover,
                               BLImage* image, BLContext* ctx,
                               const PatternHandler* pattern)
{
    Blend2DHelpers::BatchPixelAccess pixels(image);
    if (!pixels.IsValid())
        return;

    for (unsigned i = 0; i < len; i++, y++) {
        uint8 alpha = covers ? covers[i] : cover;
        if (alpha == 0)
            continue;
        blend_pixel_select_custom(x, y, colors[i], alpha, image, ctx, pattern);
    }
}

// ============================================================================
// B_OP_ERASE - Pattern-based erase using DST_OUT
// ============================================================================

static void
blend_pixel_erase_custom(int x, int y,
                        const PixelFormat::color_type& c,
                        uint8 cover, BLImage* image, BLContext* ctx,
                        const PatternHandler* pattern)
{
    if (!pattern->IsHighColor(x, y))
        return;

    rgb_color lowColor = pattern->LowColor();

    ctx->setCompOp(BL_COMP_OP_DST_OUT);

    if (cover == 255) {
        ctx->fillRect(BLRect(x, y, 1, 1),
                     BLRgba32(lowColor.red, lowColor.green, lowColor.blue, 255));
    } else {
        double prevAlpha = ctx->globalAlpha();
        ctx->setGlobalAlpha(cover / 255.0);
        ctx->fillRect(BLRect(x, y, 1, 1),
                     BLRgba32(lowColor.red, lowColor.green, lowColor.blue, 255));
        ctx->setGlobalAlpha(prevAlpha);
    }
}

static void
blend_hline_erase_custom(int x, int y, unsigned len,
                        const PixelFormat::color_type& c,
                        uint8 cover, BLImage* image, BLContext* ctx,
                        const PatternHandler* pattern)
{
    for (unsigned i = 0; i < len; i++, x++) {
        blend_pixel_erase_custom(x, y, c, cover, image, ctx, pattern);
    }
}

static void
blend_vline_erase_custom(int x, int y, unsigned len,
                        const PixelFormat::color_type& c,
                        uint8 cover, BLImage* image, BLContext* ctx,
                        const PatternHandler* pattern)
{
    for (unsigned i = 0; i < len; i++, y++) {
        blend_pixel_erase_custom(x, y, c, cover, image, ctx, pattern);
    }
}

static void
blend_solid_hspan_erase_custom(int x, int y, unsigned len,
                              const PixelFormat::color_type& c,
                              const uint8* covers, BLImage* image,
                              BLContext* ctx, const PatternHandler* pattern)
{
    for (unsigned i = 0; i < len; i++, x++, covers++) {
        if (*covers == 0)
            continue;
        blend_pixel_erase_custom(x, y, c, *covers, image, ctx, pattern);
    }
}

static void
blend_solid_vspan_erase_custom(int x, int y, unsigned len,
                              const PixelFormat::color_type& c,
                              const uint8* covers, BLImage* image,
                              BLContext* ctx, const PatternHandler* pattern)
{
    for (unsigned i = 0; i < len; i++, y++, covers++) {
        if (*covers == 0)
            continue;
        blend_pixel_erase_custom(x, y, c, *covers, image, ctx, pattern);
    }
}

static void
blend_color_hspan_erase_custom(int x, int y, unsigned len,
                              const PixelFormat::color_type* colors,
                              const uint8* covers, uint8 cover,
                              BLImage* image, BLContext* ctx,
                              const PatternHandler* pattern)
{
    for (unsigned i = 0; i < len; i++, x++) {
        uint8 alpha = covers ? covers[i] : cover;
        if (alpha == 0)
            continue;
        blend_pixel_erase_custom(x, y, colors[i], alpha, image, ctx, pattern);
    }
}

static void
blend_color_vspan_erase_custom(int x, int y, unsigned len,
                              const PixelFormat::color_type* colors,
                              const uint8* covers, uint8 cover,
                              BLImage* image, BLContext* ctx,
                              const PatternHandler* pattern)
{
    for (unsigned i = 0; i < len; i++, y++) {
        uint8 alpha = covers ? covers[i] : cover;
        if (alpha == 0)
            continue;
        blend_pixel_erase_custom(x, y, colors[i], alpha, image, ctx, pattern);
    }
}

#endif // BLEND2D_DRAWING_MODE_CUSTOM_H
