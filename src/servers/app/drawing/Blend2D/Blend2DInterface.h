/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Main Blend2D pipeline interface replacing PainterAggInterface
 */
#ifndef BLEND2D_INTERFACE_H
#define BLEND2D_INTERFACE_H

#include "Blend2DDefines.h"
#include "Blend2DTypes.h"
#include "PatternHandler.h"

#include <blend2d.h>

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
    }

    ~PainterBlend2DInterface()
    {
        if (fContextCreated) {
            fContext.end();
        }
    }

    // Main rendering objects
    BLImage                 fImage;         // Replaces agg::rendering_buffer
    BLContext               fContext;       // Replaces all AGG renderer/rasterizer objects
    BLPath                  fPath;          // Replaces agg::path_storage
    BLMatrix2D              fMatrix;        // Replaces agg::trans_affine

    // Style and pattern handling
    PatternHandler*         fPatternHandler;

    // Context state
    bool                    fContextCreated;
    HaikuRenderingHints     fRenderingHints;
    HaikuStrokeOptions      fStrokeOptions;

    // Initialize context with image target
    bool InitContext(uint8_t* buffer, uint32_t width, uint32_t height, int32_t stride)
    {
        if (fContextCreated) {
            fContext.end();
        }

        // Create image from buffer
        BLImageData imageData;
        imageData.pixelData = buffer;
        imageData.stride = stride;
        imageData.size.w = int(width);
        imageData.size.h = int(height);
        imageData.format = BL_FORMAT_PRGB32;

        BLResult result = fImage.createFromData(width, height, BL_FORMAT_PRGB32, 
                                               buffer, stride, BL_DATA_ACCESS_RW);
        if (result != BL_SUCCESS) {
            return false;
        }

        // Create context
        BLContextCreateInfo createInfo{};
        createInfo.threadCount = 2; // Configure for Haiku
        result = fContext.begin(fImage, createInfo);
        if (result != BL_SUCCESS) {
            return false;
        }

        fContextCreated = true;

        // Set default rendering hints
        fContext.setRenderingQuality(fRenderingHints.renderingQuality);
        fContext.setGradientQuality(fRenderingHints.gradientQuality);
        fContext.setPatternQuality(fRenderingHints.patternQuality);

        // Set default composition
        fContext.setCompOp(BL_COMP_OP_SRC_OVER);
        
        return true;
    }

    // Set transformation matrix
    void SetTransform(const BLMatrix2D& matrix)
    {
        fMatrix = matrix;
        if (fContextCreated) {
            fContext.setTransform(fMatrix);
        }
    }

    // Set clipping region 
    void SetClipRect(const BLRect& rect)
    {
        if (fContextCreated) {
            fContext.clipToRect(rect);
        }
    }

    // Clear path for new drawing operation
    void ClearPath()
    {
        fPath.clear();
    }

    // Get current path for building
    BLPath& GetPath() { return fPath; }
    const BLPath& GetPath() const { return fPath; }

    // Get context for direct rendering
    BLContext& GetContext() { return fContext; }
    const BLContext& GetContext() const { return fContext; }
};

#endif // BLEND2D_INTERFACE_H