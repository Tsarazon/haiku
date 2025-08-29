/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Rendering pipeline definitions for Blend2D - replaces AGG defines.h
 */
#ifndef BLEND2D_DEFINES_H
#define BLEND2D_DEFINES_H

#include <blend2d.h>
#include "Blend2DTypes.h"
#include "GlobalSubpixelSettings.h"

// Rendering quality settings
#define BLEND2D_ANTIALIASED_DRAWING 1

// Main pixel format for Haiku (32-bit BGRA)
typedef BLFormat                        pixfmt;
typedef BLContext                       renderer_base;

#if BLEND2D_ANTIALIASED_DRAWING
    // Anti-aliased rendering configuration
    typedef BLContext                   outline_renderer_type;
    typedef BLContext                   outline_rasterizer_type;
    
    // Context acts as both scanline and renderer
    typedef BLContext                   scanline_unpacked_type;
    typedef BLContext                   scanline_packed_type;
    typedef BLContext                   scanline_packed_subpix_type;
    typedef BLContext                   scanline_unpacked_subpix_type;
    typedef BLContext                   scanline_unpacked_masked_type;
    
    typedef BLContext                   renderer_type;
    typedef BLContext                   renderer_bin_type;
    typedef BLContext                   renderer_subpix_type;
    
    typedef BLContext                   rasterizer_type;
    typedef BLContext                   rasterizer_subpix_type;
#else
    // Aliased rendering fallback (not recommended)
    typedef BLContext                   outline_renderer_type;
    typedef BLContext                   outline_rasterizer_type;
    typedef BLContext                   scanline_unpacked_type;
    typedef BLContext                   scanline_packed_type;
    typedef BLContext                   renderer_type;
    typedef BLContext                   rasterizer_type;
#endif // BLEND2D_ANTIALIASED_DRAWING

// Blend2D context creation info for Haiku
struct HaikuContextCreateInfo {
    uint32_t threadCount;       // Number of worker threads (0 = auto)
    uint32_t cpuFeatures;       // CPU features to use (0 = auto-detect)
    uint32_t commandQueueLimit; // Maximum commands in queue
    
    HaikuContextCreateInfo() 
        : threadCount(0)
        , cpuFeatures(0) 
        , commandQueueLimit(0) {}
};

// Quality hint configuration for Haiku graphics
struct HaikuRenderingHints {
    BLRenderingQuality renderingQuality;    // Overall rendering quality
    BLRenderingQuality gradientQuality;     // Gradient rendering quality  
    BLRenderingQuality patternQuality;      // Pattern rendering quality
    
    HaikuRenderingHints()
        : renderingQuality(BL_RENDERING_QUALITY_ANTIALIAS)
        , gradientQuality(BL_RENDERING_QUALITY_ANTIALIAS)
        , patternQuality(BL_RENDERING_QUALITY_ANTIALIAS) {}
};

// Stroke options for path rendering
struct HaikuStrokeOptions {
    double width;
    BLStrokeCap startCap;
    BLStrokeCap endCap;
    BLStrokeJoin join;
    double miterLimit;
    
    HaikuStrokeOptions()
        : width(1.0)
        , startCap(BL_STROKE_CAP_BUTT)
        , endCap(BL_STROKE_CAP_BUTT)
        , join(BL_STROKE_JOIN_MITER_CLIP)
        , miterLimit(4.0) {}
};

#endif // BLEND2D_DEFINES_H