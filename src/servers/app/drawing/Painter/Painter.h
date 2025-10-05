/*
 * Copyright 2005-2007, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>
 * Copyright 2025, Haiku Blend2D Migration
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * API to the Blend2D based "Painter" drawing backend.
 */
#ifndef PAINTER_H
#define PAINTER_H

#include "Blend2dTextRenderer.h"
#include "FontManager.h"
#include "PatternHandler.h"
#include "ServerFont.h"
#include "Transformable.h"
#include "defines.h"

#include <blend2d.h>
#include <AffineTransform.h>
#include <Font.h>
#include <Rect.h>

class BBitmap;
class BRegion;
class BGradient;
class BGradientLinear;
class BGradientRadial;
class BGradientRadialFocus;
class BGradientDiamond;
class BGradientConic;
class DrawState;
class FontCacheReference;
class RenderingBuffer;
class ServerBitmap;
class ServerFont;

class Painter {
public:
								Painter();
	virtual						~Painter();

	// ========================================================================
	// Frame Buffer Management
	// ========================================================================
	
			void				AttachToBuffer(RenderingBuffer* buffer);
			void				DetachFromBuffer();
			BRect				Bounds() const;

			void				SetDrawState(const DrawState* data,
									int32 xOffset = 0, int32 yOffset = 0);

	// ========================================================================
	// Clipping Management
	// ========================================================================
	
			void				ConstrainClipping(const BRegion* region);
			const BRegion*		ClippingRegion() const
									{ return fClippingRegion; }

	// ========================================================================
	// Transformation
	// ========================================================================
	
			void				SetTransform(BAffineTransform transform,
									int32 xOffset, int32 yOffset);

	inline	bool				IsIdentityTransform() const
									{ return fIdentityTransform; }
			
			const Transformable& Transform() const
									{ return fTransform; }

	// ========================================================================
	// Drawing State - Colors
	// ========================================================================
	
			void				SetHighColor(const rgb_color& color);
	inline	rgb_color			HighColor() const
									{ return fPatternHandler.HighColor(); }

			void				SetLowColor(const rgb_color& color);
	inline	rgb_color			LowColor() const
									{ return fPatternHandler.LowColor(); }

	// ========================================================================
	// Drawing State - Pen and Stroke
	// ========================================================================
	
			void				SetPenSize(float size);
	inline	float				PenSize() const
									{ return fPenSize; }
	
			void				SetStrokeMode(cap_mode lineCap,
									join_mode joinMode, float miterLimit);
			
			void				SetFillRule(int32 fillRule);
			
			void				SetPattern(const pattern& p);
	inline	pattern				Pattern() const
									{ return *fPatternHandler.GetR5Pattern(); }
	
			void				SetDrawingMode(drawing_mode mode);
	inline	drawing_mode		DrawingMode() const
									{ return fDrawingMode; }
	
			void				SetBlendingMode(source_alpha srcAlpha,
									alpha_function alphaFunc);

	// ========================================================================
	// Font Management
	// ========================================================================
	
			void				SetFont(const ServerFont& font);
			void				SetFont(const DrawState* state);
	inline	const ServerFont&	Font() const
									{ return fTextRenderer.Font(); }

	// ========================================================================
	// DRAWING FUNCTIONS
	// ========================================================================

	// Lines
			void				StrokeLine(BPoint a, BPoint b);
			bool				StraightLine(BPoint a, BPoint b,
									const rgb_color& c) const;

	// Triangles
			BRect				StrokeTriangle(BPoint pt1, BPoint pt2,
									BPoint pt3) const;
			BRect				FillTriangle(BPoint pt1, BPoint pt2,
									BPoint pt3) const;
			BRect				FillTriangle(BPoint pt1, BPoint pt2,
									BPoint pt3, const BGradient& gradient);

	// Polygons
			BRect				DrawPolygon(BPoint* ptArray, int32 numPts,
									bool filled, bool closed) const;
			BRect				FillPolygon(BPoint* ptArray, int32 numPts,
									const BGradient& gradient, bool closed);

	// Bezier Curves
			BRect				DrawBezier(BPoint* controlPoints,
									bool filled) const;
			BRect				FillBezier(BPoint* controlPoints,
									const BGradient& gradient);

	// Shapes (BShape support)
			BRect				DrawShape(const int32& opCount,
									const uint32* opList, const int32& ptCount,
									const BPoint* ptList, bool filled,
									const BPoint& viewToScreenOffset,
									float viewScale) const;
			BRect				FillShape(const int32& opCount,
									const uint32* opList, const int32& ptCount,
									const BPoint* ptList,
									const BGradient& gradient,
									const BPoint& viewToScreenOffset,
									float viewScale);

	// Rectangles
			BRect				StrokeRect(const BRect& r) const;
			void				StrokeRect(const BRect& r,
									const rgb_color& c) const;

			BRect				FillRect(const BRect& r) const;
			BRect				FillRect(const BRect& r,
									const BGradient& gradient);
			void				FillRect(const BRect& r,
									const rgb_color& c) const;
			void				FillRectVerticalGradient(BRect r,
									const BGradientLinear& gradient) const;
			void				FillRectNoClipping(const clipping_rect& r,
									const rgb_color& c) const;

	// Rounded Rectangles
			BRect				StrokeRoundRect(const BRect& r, float xRadius,
									float yRadius) const;
			BRect				FillRoundRect(const BRect& r, float xRadius,
									float yRadius) const;
			BRect				FillRoundRect(const BRect& r, float xRadius,
									float yRadius, const BGradient& gradient);

	// Ellipses
			void				AlignEllipseRect(BRect* rect, bool filled) const;
			BRect				DrawEllipse(BRect r, bool filled) const;
			BRect				FillEllipse(BRect r, const BGradient& gradient);

	// Arcs
			BRect				StrokeArc(BPoint center, float xRadius,
									float yRadius, float angle, float span) const;
			BRect				FillArc(BPoint center, float xRadius,
									float yRadius, float angle, float span) const;
			BRect				FillArc(BPoint center, float xRadius,
									float yRadius, float angle, float span,
									const BGradient& gradient);

	// Text Rendering
			BRect				DrawString(const char* utf8String,
									uint32 length, BPoint baseLine,
									const escapement_delta* delta,
									FontCacheReference* cacheReference = NULL);
			BRect				DrawString(const char* utf8String,
									uint32 length, const BPoint* offsets,
									FontCacheReference* cacheReference = NULL);

			BRect				BoundingBox(const char* utf8String,
									uint32 length, BPoint baseLine,
									BPoint* penLocation,
									const escapement_delta* delta,
									FontCacheReference* cacheReference = NULL) const;
			BRect				BoundingBox(const char* utf8String,
									uint32 length, const BPoint* offsets,
									BPoint* penLocation,
									FontCacheReference* cacheReference = NULL) const;

			float				StringWidth(const char* utf8String,
									uint32 length,
									const escapement_delta* delta = NULL);

	// Bitmaps
			BRect				DrawBitmap(const ServerBitmap* bitmap,
									BRect bitmapRect, BRect viewRect,
									uint32 options) const;

	// Region Operations
			BRect				FillRegion(const BRegion* region) const;
			BRect				FillRegion(const BRegion* region,
									const BGradient& gradient);
			BRect				InvertRect(const BRect& r) const;

	// Coordinate Transformation Helpers
	inline	BRect				TransformAndClipRect(BRect rect) const;
	inline	BRect				ClipRect(BRect rect) const;
	inline	BRect				TransformAlignAndClipRect(BRect rect) const;
	inline	BRect				AlignAndClipRect(BRect rect) const;
	inline	BRect				AlignRect(BRect rect) const;

			void				SetRendererOffset(int32 offsetX, int32 offsetY);

private:
	class BitmapPainter;
	friend class BitmapPainter;

	// ========================================================================
	// Private Helper Methods
	// ========================================================================

	// Coordinate Alignment
			float				_Align(float coord, bool round,
									bool centerOffset) const;
			void				_Align(BPoint* point, bool round,
									bool centerOffset) const;
			void				_Align(BPoint* point,
									bool centerOffset = true) const;
			BPoint				_Align(const BPoint& point,
									bool centerOffset = true) const;
			BRect				_Clipped(const BRect& rect) const;

	// Drawing State Management
			void				_UpdateDrawingMode();
			void				_SetRendererColor(const rgb_color& color) const;

	// Primitive Drawing Helpers
			BRect				_DrawTriangle(BPoint pt1, BPoint pt2,
									BPoint pt3, bool fill) const;

			void				_IterateShapeData(const int32& opCount,
									const uint32* opList, const int32& ptCount,
									const BPoint* points,
									const BPoint& viewToScreenOffset,
									float viewScale, BLPath& path) const;

			void				_InvertRect32(BRect r) const;

	// Blend2D Path Rendering
			BRect				_BoundingBox(const BLPath& path) const;
			
			BRect				_StrokePath(const BLPath& path) const;
			BRect				_StrokePath(const BLPath& path,
									cap_mode capMode) const;
			
			BRect				_FillPath(const BLPath& path) const;

	// Gradient Rendering
			BRect				_FillPath(const BLPath& path,
									const BGradient& gradient);

			void				_ApplyLinearGradient(const BLPath& path,
									const BGradientLinear& gradient);
			void				_ApplyRadialGradient(const BLPath& path,
									const BGradientRadial& gradient);
			void				_ApplyRadialFocusGradient(const BLPath& path,
									const BGradientRadialFocus& gradient);
			void				_ApplyDiamondGradient(const BLPath& path,
									const BGradientDiamond& gradient);
			void				_ApplyConicGradient(const BLPath& path,
									const BGradientConic& gradient);

			void				_MakeGradient(BLGradient& blGradient,
									const BGradient& gradient) const;
			void				_MakeGradient(const BGradient& gradient,
									int32 colorCount, uint32* colors,
									int32 arrayOffset, int32 arraySize) const;

private:
	// ========================================================================
	// Member Variables
	// ========================================================================

	// Rendering state flags
			bool				fSubpixelPrecise : 1;
			bool				fValidClipping : 1;
			bool				fAttached : 1;
			bool				fIdentityTransform : 1;

	// Transformation
			Transformable		fTransform;
			
	// Drawing parameters
			float				fPenSize;
			const BRegion*		fClippingRegion;
			drawing_mode		fDrawingMode;
			source_alpha		fAlphaSrcMode;
			alpha_function		fAlphaFncMode;
			cap_mode			fLineCapMode;
			join_mode			fLineJoinMode;
			float				fMiterLimit;

	// Pattern handling
			PatternHandler		fPatternHandler;

	// Text rendering through Blend2D
	mutable	Blend2dTextRenderer	fTextRenderer;

	// Blend2D rendering interface (contains BLContext, BLImage, PixelFormat)
	mutable	PainterInterface	fInternal;

	// Shortcut for PixelFormat access
	inline PixelFormat& _PixelFormat() const { return fInternal.fPixelFormat; }
};

// ============================================================================
// Inline Implementations
// ============================================================================

inline BRect
Painter::TransformAndClipRect(BRect rect) const
{
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	rect.right = ceilf(rect.right);
	rect.bottom = ceilf(rect.bottom);

	if (!fIdentityTransform)
		rect = fTransform.TransformBounds(rect);

	return _Clipped(rect);
}

inline BRect
Painter::ClipRect(BRect rect) const
{
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	rect.right = ceilf(rect.right);
	rect.bottom = ceilf(rect.bottom);
	
	return _Clipped(rect);
}

inline BRect
Painter::AlignAndClipRect(BRect rect) const
{
	return _Clipped(AlignRect(rect));
}

inline BRect
Painter::TransformAlignAndClipRect(BRect rect) const
{
	rect = AlignRect(rect);
	
	if (!fIdentityTransform)
		rect = fTransform.TransformBounds(rect);
	
	return _Clipped(rect);
}

inline BRect
Painter::AlignRect(BRect rect) const
{
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	
	if (fSubpixelPrecise) {
		rect.right = ceilf(rect.right);
		rect.bottom = ceilf(rect.bottom);
	} else {
		rect.right = floorf(rect.right);
		rect.bottom = floorf(rect.bottom);
	}

	return rect;
}

#endif // PAINTER_H
