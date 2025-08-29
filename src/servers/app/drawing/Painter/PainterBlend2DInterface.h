/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Blend2D interface replacing PainterAggInterface.h
 */
#ifndef PAINTER_BLEND2D_INTERFACE_H
#define PAINTER_BLEND2D_INTERFACE_H

#include "defines.h"
#include <blend2d.h>

class PatternHandler;

struct PainterBlend2DInterface {
    PainterBlend2DInterface(PatternHandler& patternHandler)
        :
        fImage(),
        fContext(),
        fPath(),
        fMatrix(),
        fPatternHandler(&patternHandler),
        fContextCreated(false)
    {
        // Initialize path and matrix
        fMatrix.reset();
    }

    ~PainterBlend2DInterface()
    {
        if (fContextCreated) {
            fContext.end();
        }
    }

    // Core Blend2D objects replacing AGG pipeline
    BLImage                 fImage;          // Replaces agg::rendering_buffer
    BLContext               fContext;        // Replaces all AGG renderer/rasterizer pipeline
    BLPath                  fPath;           // Replaces agg::path_storage  
    BLMatrix2D              fMatrix;         // Replaces agg::trans_affine

    // Pattern handling (preserved from AGG implementation)
    PatternHandler*         fPatternHandler;

    // Context management
    bool                    fContextCreated;

    // Initialize context with rendering buffer
    bool Initialize(uint8_t* buffer, uint32_t width, uint32_t height, int32_t stride)
    {
        if (fContextCreated) {
            fContext.end();
            fContextCreated = false;
        }

        // Create image from external buffer
        BLResult result = fImage.createFromData(width, height, BL_FORMAT_PRGB32, 
                                               buffer, stride, BL_DATA_ACCESS_RW);
        if (result != BL_SUCCESS) {
            return false;
        }

        // Create rendering context
        BLContextCreateInfo createInfo{};
        createInfo.threadCount = 2;  // Optimized for Haiku
        result = fContext.begin(fImage, createInfo);
        if (result != BL_SUCCESS) {
            return false;
        }

        fContextCreated = true;

        // Set default rendering settings
        fContext.setCompOp(BL_COMP_OP_SRC_OVER);
        fContext.setRenderingQuality(BL_RENDERING_QUALITY_ANTIALIAS);
        fContext.setTransform(fMatrix);

        return true;
    }

    // Update transformation matrix
    void SetTransform(const BLMatrix2D& matrix)
    {
        fMatrix = matrix;
        if (fContextCreated) {
            fContext.setTransform(fMatrix);
        }
    }

    // Path operations
    void ClearPath()
    {
        fPath.clear();
    }

    BLPath& GetPath() { return fPath; }
    const BLPath& GetPath() const { return fPath; }

    // Context access
    BLContext& GetContext() { return fContext; }
    const BLContext& GetContext() const { return fContext; }

    // Image access  
    BLImage& GetImage() { return fImage; }
    const BLImage& GetImage() const { return fImage; }
};

#endif // PAINTER_BLEND2D_INTERFACE_H