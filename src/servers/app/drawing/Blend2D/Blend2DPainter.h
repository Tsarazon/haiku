/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Painter compatibility layer with BLContext wrapper
 */
#ifndef BLEND2D_PAINTER_H
#define BLEND2D_PAINTER_H

#include "Blend2DInterface.h"
#include "Blend2DTypes.h"
#include <blend2d.h>

class Blend2DPainter {
public:
    Blend2DPainter(PainterBlend2DInterface& interface)
        : fInterface(interface)
        , fContext(interface.GetContext())
        , fPath(interface.GetPath())
    {
    }

    // Basic drawing operations
    BLResult StrokePath(const BLPath& path, const BLRgba32& color, double width = 1.0)
    {
        BLStrokeOptions strokeOptions;
        strokeOptions.width = width;
        strokeOptions.caps = fInterface.fStrokeOptions.startCap;
        strokeOptions.join = fInterface.fStrokeOptions.join;
        strokeOptions.miterLimit = fInterface.fStrokeOptions.miterLimit;

        fContext.setFillStyle(color);
        return fContext.strokePath(path, strokeOptions);
    }

    BLResult FillPath(const BLPath& path, const BLRgba32& color)
    {
        fContext.setFillStyle(color);
        return fContext.fillPath(path);
    }

    BLResult FillPath(const BLPath& path, const BLGradient& gradient)
    {
        fContext.setFillStyle(gradient);
        return fContext.fillPath(path);
    }

    // Rectangle operations
    BLResult StrokeRect(const BLRect& rect, const BLRgba32& color, double width = 1.0)
    {
        BLStrokeOptions strokeOptions;
        strokeOptions.width = width;
        
        fContext.setStrokeStyle(color);
        return fContext.strokeRect(rect, strokeOptions);
    }

    BLResult FillRect(const BLRect& rect, const BLRgba32& color)
    {
        fContext.setFillStyle(color);
        return fContext.fillRect(rect);
    }

    BLResult FillRect(const BLRect& rect, const BLGradient& gradient)
    {
        fContext.setFillStyle(gradient);
        return fContext.fillRect(rect);
    }

    // Circle/ellipse operations
    BLResult StrokeCircle(const BLCircle& circle, const BLRgba32& color, double width = 1.0)
    {
        BLStrokeOptions strokeOptions;
        strokeOptions.width = width;
        
        fContext.setStrokeStyle(color);
        return fContext.strokeCircle(circle, strokeOptions);
    }

    BLResult FillCircle(const BLCircle& circle, const BLRgba32& color)
    {
        fContext.setFillStyle(color);
        return fContext.fillCircle(circle);
    }

    BLResult FillEllipse(const BLEllipse& ellipse, const BLRgba32& color)
    {
        fContext.setFillStyle(color);
        return fContext.fillEllipse(ellipse);
    }

    // Text rendering
    BLResult DrawText(const BLPoint& origin, const BLFont& font, const char* text, const BLRgba32& color)
    {
        fContext.setFillStyle(color);
        return fContext.fillText(origin, font, text);
    }

    BLResult DrawGlyphRun(const BLPoint& origin, const BLFont& font, const BLGlyphRun& glyphRun, const BLRgba32& color)
    {
        fContext.setFillStyle(color);
        return fContext.fillGlyphRun(origin, font, glyphRun);
    }

    // Image drawing  
    BLResult DrawImage(const BLRect& dst, const BLImage& img)
    {
        return fContext.blitImage(dst, img);
    }

    BLResult DrawImage(const BLRect& dst, const BLImage& img, const BLRect& src)
    {
        return fContext.blitImage(dst, img, src);
    }

    // Transformation operations
    void SetTransform(const BLMatrix2D& transform)
    {
        fInterface.SetTransform(transform);
    }

    void ResetTransform()
    {
        BLMatrix2D identity;
        identity.reset();
        fInterface.SetTransform(identity);
    }

    void Translate(double x, double y)
    {
        BLMatrix2D current = fContext.userTransform();
        current.translate(x, y);
        fInterface.SetTransform(current);
    }

    void Scale(double x, double y)
    {
        BLMatrix2D current = fContext.userTransform();
        current.scale(x, y);
        fInterface.SetTransform(current);
    }

    void Rotate(double angle)
    {
        BLMatrix2D current = fContext.userTransform();
        current.rotate(angle);
        fInterface.SetTransform(current);
    }

    // Clipping operations
    void SetClipRect(const BLRect& rect)
    {
        fInterface.SetClipRect(rect);
    }

    void ClipToPath(const BLPath& path, BLFillRule fillRule = BL_FILL_RULE_NON_ZERO)
    {
        fContext.clipToPath(path, fillRule);
    }

    void ResetClip()
    {
        fContext.restoreClipping();
    }

    // Path building helpers
    void MoveTo(double x, double y) { fPath.moveTo(x, y); }
    void LineTo(double x, double y) { fPath.lineTo(x, y); }
    void QuadTo(double x1, double y1, double x2, double y2) { fPath.quadTo(x1, y1, x2, y2); }
    void CubicTo(double x1, double y1, double x2, double y2, double x3, double y3) { fPath.cubicTo(x1, y1, x2, y2, x3, y3); }
    void Close() { fPath.close(); }
    void ClearPath() { fInterface.ClearPath(); }

    // Style settings
    void SetRenderingQuality(BLRenderingQuality quality)
    {
        fInterface.fRenderingHints.renderingQuality = quality;
        fContext.setRenderingQuality(quality);
    }

    void SetStrokeWidth(double width)
    {
        fInterface.fStrokeOptions.width = width;
    }

    void SetStrokeCaps(BLStrokeCap cap)
    {
        fInterface.fStrokeOptions.startCap = cap;
        fInterface.fStrokeOptions.endCap = cap;
    }

    void SetStrokeJoin(BLStrokeJoin join)
    {
        fInterface.fStrokeOptions.join = join;
    }

    // Access to underlying objects
    BLContext& GetContext() { return fContext; }
    BLPath& GetPath() { return fPath; }
    PainterBlend2DInterface& GetInterface() { return fInterface; }

private:
    PainterBlend2DInterface& fInterface;
    BLContext& fContext;
    BLPath& fPath;
};

#endif // BLEND2D_PAINTER_H