/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Blend2D-based icon renderer for Icon-O-Matic
 */

#include "Blend2DIconRenderer.h"

#include <Bitmap.h>
#include "Icon.h"
#include "VectorPath.h"
#include "Style.h"

Blend2DIconRenderer::Blend2DIconRenderer(BBitmap* bitmap)
    : IconRenderer(bitmap)
    , fImage()
    , fContext()
    , fContextInitialized(false)
{
    _InitContext();
}

Blend2DIconRenderer::~Blend2DIconRenderer()
{
    _CleanupContext();
}

bool
Blend2DIconRenderer::_InitContext()
{
    if (fContextInitialized || !fBitmap)
        return false;

    BRect bounds = fBitmap->Bounds();
    uint32 width = (uint32)(bounds.Width() + 1);
    uint32 height = (uint32)(bounds.Height() + 1);
    uint32 bpr = fBitmap->BytesPerRow();
    uint8* bits = (uint8*)fBitmap->Bits();

    // Create Blend2D image from bitmap
    BLResult result = fImage.createFromData(width, height, BL_FORMAT_PRGB32,
                                           bits, bpr, BL_DATA_ACCESS_RW);
    if (result != BL_SUCCESS)
        return false;

    // Initialize context
    BLContextCreateInfo createInfo{};
    createInfo.threadCount = 1; // Single-threaded for UI
    result = fContext.begin(fImage, createInfo);
    
    if (result != BL_SUCCESS) {
        fImage.reset();
        return false;
    }

    // Set default rendering quality
    fContext.setRenderingQuality(BL_RENDERING_QUALITY_ANTIALIAS);
    fContext.setCompOp(BL_COMP_OP_SRC_OVER);
    
    fContextInitialized = true;
    return true;
}

void
Blend2DIconRenderer::_CleanupContext()
{
    if (fContextInitialized) {
        fContext.end();
        fImage.reset();
        fContextInitialized = false;
    }
}

void
Blend2DIconRenderer::Render()
{
    if (!fContextInitialized || !fIcon)
        return;

    // Clear the canvas
    fContext.clearAll();

    // Apply global transformation
    BLMatrix2D matrix;
    matrix.reset();
    matrix.scale(fScale, fScale);
    fContext.setTransform(matrix);

    // Render all shapes in the icon
    int32 shapeCount = fIcon->Shapes()->CountItems();
    for (int32 i = 0; i < shapeCount; i++) {
        Shape* shape = (Shape*)fIcon->Shapes()->ItemAtFast(i);
        if (shape && shape->Paths()->CountItems() > 0) {
            VectorPath* path = (VectorPath*)shape->Paths()->ItemAtFast(0);
            Style* style = shape->Style();
            if (path && style) {
                _RenderShape(path, style);
            }
        }
    }

    // Flush rendering
    fContext.flush(BL_CONTEXT_FLUSH_SYNC);
}

void
Blend2DIconRenderer::_RenderShape(VectorPath* path, Style* style)
{
    if (!path || !style)
        return;

    // Convert VectorPath to BLPath
    BLPath blPath;
    
    // Get path points and convert
    int32 pointCount = path->CountPoints();
    for (int32 i = 0; i < pointCount; i++) {
        BPoint point;
        BPoint pointIn;
        BPoint pointOut;
        bool connected;
        
        if (path->GetPointsAt(i, point, pointIn, pointOut, &connected)) {
            if (i == 0) {
                blPath.moveTo(point.x, point.y);
            } else {
                // Handle curve vs line
                if (pointIn != point || pointOut != point) {
                    BPoint prevPoint;
                    BPoint prevIn, prevOut;
                    if (path->GetPointsAt(i-1, prevPoint, prevIn, prevOut, &connected)) {
                        blPath.cubicTo(prevOut.x, prevOut.y,
                                      pointIn.x, pointIn.y,
                                      point.x, point.y);
                    }
                } else {
                    blPath.lineTo(point.x, point.y);
                }
            }
        }
    }
    
    if (path->IsClosed())
        blPath.close();

    // Render based on style type
    if (style->FillType() == FILL_TYPE_COLOR) {
        rgb_color color = style->Color();
        BLRgba32 blColor(color.red, color.green, color.blue, color.alpha);
        fContext.setFillStyle(blColor);
        fContext.fillPath(blPath);
    } else if (style->FillType() == FILL_TYPE_GRADIENT) {
        // Handle gradient rendering
        BGradient* gradient = style->Gradient();
        if (gradient) {
            _RenderGradient(blPath, *gradient);
        }
    }
}

void
Blend2DIconRenderer::_RenderPath(const BLPath& path, const rgb_color& color)
{
    BLRgba32 blColor(color.red, color.green, color.blue, color.alpha);
    fContext.setFillStyle(blColor);
    fContext.fillPath(path);
}

void
Blend2DIconRenderer::_RenderGradient(const BLPath& path, const BGradient& gradient)
{
    BLGradient blGradient;
    
    // Set gradient type
    if (gradient.Type() == BGradient::TYPE_LINEAR) {
        blGradient.setType(BL_GRADIENT_TYPE_LINEAR);
        BPoint start, end;
        gradient.GetLinearGradientCoordinates(start, end);
        blGradient.setValues(0, start.x, start.y, end.x, end.y);
    } else if (gradient.Type() == BGradient::TYPE_RADIAL) {
        blGradient.setType(BL_GRADIENT_TYPE_RADIAL);
        BPoint center;
        float radius;
        gradient.GetRadialGradientCoordinates(center, radius);
        blGradient.setValues(0, center.x, center.y, radius);
    }

    // Add gradient stops
    int32 stopCount = gradient.CountColorStops();
    for (int32 i = 0; i < stopCount; i++) {
        BGradient::ColorStop* stop = gradient.ColorStopAt(i);
        if (stop) {
            rgb_color color = stop->color;
            BLRgba32 blColor(color.red, color.green, color.blue, color.alpha);
            blGradient.addStop(stop->offset, blColor);
        }
    }

    fContext.setFillStyle(blGradient);
    fContext.fillPath(path);
}

void
Blend2DIconRenderer::SetScale(double scale)
{
    IconRenderer::SetScale(scale);
    
    if (fContextInitialized) {
        BLMatrix2D matrix;
        matrix.reset();
        matrix.scale(fScale, fScale);
        fContext.setTransform(matrix);
    }
}

void
Blend2DIconRenderer::SetIcon(Icon* icon)
{
    IconRenderer::SetIcon(icon);
}