/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Helper functions for efficient Blend2D drawing mode rendering.
 * Pattern-agnostic - works with existing PatternHandler.
 */

#ifndef BLEND2D_DRAWING_MODE_HELPERS_H
#define BLEND2D_DRAWING_MODE_HELPERS_H

#include "PixelFormat.h"
#include "PatternHandler.h"
#include <blend2d.h>

// ============================================================================
// BATCH SOLID COLOR RENDERING
// ============================================================================

namespace Blend2DHelpers {

// Batch render solid color span with coverage array
// Groups runs with same coverage for efficiency
static inline void
render_solid_hspan_batch(int x, int y, unsigned len,
                         const rgb_color& color,
                         const uint8* covers,
                         BLContext* ctx, BLCompOp compOp)
{
    if (len == 0) return;

    ctx->setCompOp(compOp);

    unsigned i = 0;
    while (i < len) {
        // Skip zero coverage
        while (i < len && covers[i] == 0)
            i++;
        if (i >= len)
            break;

        // Find run with same coverage
        uint8 cover = covers[i];
        unsigned start = i;

        while (i < len && covers[i] == cover)
            i++;

        unsigned runLen = i - start;

        // Render entire run with one API call
        if (cover == 255) {
            ctx->fillRect(BLRect(x + start, y, runLen, 1),
                         BLRgba32(color.red, color.green, color.blue,
                                 color.alpha));
        } else {
            double prevAlpha = ctx->globalAlpha();
            ctx->setGlobalAlpha((cover / 255.0) * (color.alpha / 255.0));
            ctx->fillRect(BLRect(x + start, y, runLen, 1),
                         BLRgba32(color.red, color.green, color.blue, 255));
            ctx->setGlobalAlpha(prevAlpha);
        }
    }
}

// ============================================================================
// PATTERN RENDERING VIA TEMPORARY BUFFER
// ============================================================================

// Maximum span length for stack allocation
// 256 pixels * 4 bytes = 1KB stack usage (safe)
static const unsigned MAX_STACK_SPAN = 256;

// Render pattern into temporary buffer, then blit to context
// This avoids pixel-by-pixel API calls while keeping PatternHandler clean
static inline void
render_pattern_hspan(int x, int y, unsigned len,
                    const PatternHandler* pattern,
                    const uint8* covers,
                    BLContext* ctx, BLCompOp compOp)
{
    if (len == 0) return;

    // Use stack allocation for small spans, heap for large
    uint32_t stackBuffer[MAX_STACK_SPAN];
    uint32_t* heapBuffer = nullptr;
    uint32_t* pixels = nullptr;

    if (len <= MAX_STACK_SPAN) {
        pixels = stackBuffer;
    } else {
        heapBuffer = new(std::nothrow) uint32_t[len];
        if (!heapBuffer) {
            // Fallback to slow path on allocation failure
            for (unsigned i = 0; i < len; i++) {
                if (covers[i] == 0)
                    continue;
                rgb_color c = pattern->ColorAt(x + i, y);
                if (covers[i] == 255) {
                    ctx->fillRect(BLRect(x + i, y, 1, 1),
                                 BLRgba32(c.red, c.green, c.blue, c.alpha));
                } else {
                    double prevAlpha = ctx->globalAlpha();
                    ctx->setGlobalAlpha((covers[i] / 255.0) * (c.alpha / 255.0));
                    ctx->fillRect(BLRect(x + i, y, 1, 1),
                                 BLRgba32(c.red, c.green, c.blue, 255));
                    ctx->setGlobalAlpha(prevAlpha);
                }
            }
            return;
        }
        pixels = heapBuffer;
    }

    // Fill buffer with pattern colors + apply coverage
    for (unsigned i = 0; i < len; i++) {
        rgb_color c = pattern->ColorAt(x + i, y);

        // Apply coverage to alpha
        uint8 alpha = (c.alpha * covers[i]) / 255;

        // Store as BGRA32 (Blend2D native format)
        pixels[i] = (uint32_t(alpha) << 24) |
                    (uint32_t(c.red) << 16) |
                    (uint32_t(c.green) << 8) |
                    uint32_t(c.blue);
    }

    // Create temporary BLImage from buffer (zero-copy read-only)
    BLImage tempImage;
    BLResult result = tempImage.createFromData(
        len, 1,                    // width x height
        BL_FORMAT_PRGB32,          // premultiplied RGBA
        pixels,                    // data
        len * 4,                   // stride
        BL_DATA_ACCESS_READ,       // read-only
        nullptr, nullptr           // no cleanup needed
    );

    if (result == BL_SUCCESS) {
        ctx->setCompOp(compOp);
        ctx->blitImage(BLPoint(x, y), tempImage);
    }

    // Cleanup heap allocation if used
    delete[] heapBuffer;
}

// Same as above but with single coverage value (not array)
static inline void
render_pattern_hspan_uniform(int x, int y, unsigned len,
                             const PatternHandler* pattern,
                             uint8 cover,
                             BLContext* ctx, BLCompOp compOp)
{
    if (len == 0 || cover == 0)
        return;

    // Use stack allocation for small spans, heap for large
    uint32_t stackBuffer[MAX_STACK_SPAN];
    uint32_t* heapBuffer = nullptr;
    uint32_t* pixels = nullptr;

    if (len <= MAX_STACK_SPAN) {
        pixels = stackBuffer;
    } else {
        heapBuffer = new(std::nothrow) uint32_t[len];
        if (!heapBuffer) {
            // Fallback to slow path
            for (unsigned i = 0; i < len; i++) {
                rgb_color c = pattern->ColorAt(x + i, y);
                if (cover == 255) {
                    ctx->fillRect(BLRect(x + i, y, 1, 1),
                                 BLRgba32(c.red, c.green, c.blue, c.alpha));
                } else {
                    double prevAlpha = ctx->globalAlpha();
                    ctx->setGlobalAlpha((cover / 255.0) * (c.alpha / 255.0));
                    ctx->fillRect(BLRect(x + i, y, 1, 1),
                                 BLRgba32(c.red, c.green, c.blue, 255));
                    ctx->setGlobalAlpha(prevAlpha);
                }
            }
            return;
        }
        pixels = heapBuffer;
    }

    // Fill buffer with pattern colors + apply coverage
    for (unsigned i = 0; i < len; i++) {
        rgb_color c = pattern->ColorAt(x + i, y);
        uint8 alpha = (c.alpha * cover) / 255;

        pixels[i] = (uint32_t(alpha) << 24) |
                    (uint32_t(c.red) << 16) |
                    (uint32_t(c.green) << 8) |
                    uint32_t(c.blue);
    }

    // Create temporary BLImage and blit
    BLImage tempImage;
    BLResult result = tempImage.createFromData(
        len, 1, BL_FORMAT_PRGB32, pixels, len * 4,
        BL_DATA_ACCESS_READ, nullptr, nullptr);

    if (result == BL_SUCCESS) {
        ctx->setCompOp(compOp);
        ctx->blitImage(BLPoint(x, y), tempImage);
    }

    delete[] heapBuffer;
}

// ============================================================================
// BATCH PIXEL ACCESS FOR CUSTOM MODES
// ============================================================================

// Get mutable pixel access once for entire span
// Much more efficient than calling makeMutable() per pixel
class BatchPixelAccess {
public:
    BatchPixelAccess(BLImage* image)
        : fImage(image)
        , fPixels(nullptr)
        , fStride(0)
        , fValid(false)
    {
        BLImageData data;
        BLResult result = fImage->makeMutable(&data);
        if (result == BL_SUCCESS) {
            fPixels = (uint32_t*)data.pixelData;
            fStride = data.stride / 4;
            fValid = true;
        }
    }

    inline bool IsValid() const { return fValid; }

    inline uint32_t* PixelAt(int x, int y) const {
        return &fPixels[y * fStride + x];
    }

private:
    BLImage* fImage;
    uint32_t* fPixels;
    int fStride;
    bool fValid;
};

} // namespace Blend2DHelpers

#endif // BLEND2D_DRAWING_MODE_HELPERS_H
