/*
 * Copyright 2005-2007, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * API to the Anti-Grain Geometry based "Painter" drawing backend. Manages
 * rendering pipe-lines for stroke, fills, bitmap and text rendering.
 *
 */
#ifndef PAINTER_H
#define PAINTER_H


#include "AGGTextRenderer.h"
#include "FontManager.h"
#include "PainterAggInterface.h"
#include "PatternHandler.h"
#include "ServerFont.h"
#include "Transformable.h"

#include "defines.h"

#include <agg_conv_curve.h>

#include <AffineTransform.h>
#include <Font.h>
#include <Rect.h>

#include <memory>


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


constexpr uint32 APPSERVER_SIMD_MMX = (1 << 0);
constexpr uint32 APPSERVER_SIMD_SSE = (1 << 1);


class Painter {
public:
								Painter();
	virtual						~Painter();

			void				AttachToBuffer(RenderingBuffer* buffer);
			void				DetachFromBuffer();
			BRect				Bounds() const;

			void				SetDrawState(const DrawState* data,
									int32 xOffset = 0,
									int32 yOffset = 0);

			void				ConstrainClipping(const BRegion* region);
			const BRegion*		ClippingRegion() const
									{ return fClippingRegion; }

			void				SetTransform(BAffineTransform transform,
									int32 xOffset, int32 yOffset);

	inline	bool				IsIdentityTransform() const
									{ return fIdentityTransform; }
			const Transformable& Transform() const
									{ return fTransform; }

			void				SetHighColor(const rgb_color& color);
	inline	rgb_color			HighColor() const
									{ return fPatternHandler.HighColor(); }

			void				SetLowColor(const rgb_color& color);
	inline	rgb_color			LowColor() const
									{ return fPatternHandler.LowColor(); }

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

			void				SetFont(const ServerFont& font);
			void				SetFont(const DrawState* state);
	inline	const ServerFont&	Font() const
									{ return fTextRenderer.Font(); }

			void				StrokeLine(BPoint a, BPoint b);

			bool				StraightLine(BPoint a, BPoint b,
									const rgb_color& c) const;

			BRect				StrokeTriangle(BPoint pt1, BPoint pt2,
									BPoint pt3) const;

			BRect				FillTriangle(BPoint pt1, BPoint pt2,
									BPoint pt3) const;
			BRect				FillTriangle(BPoint pt1, BPoint pt2,
									BPoint pt3,
									const BGradient& gradient);

			BRect				DrawPolygon(BPoint* ptArray, int32 numPts,
									bool filled, bool closed) const;
			BRect				FillPolygon(BPoint* ptArray, int32 numPts,
									const BGradient& gradient,
									bool closed);

			BRect				DrawBezier(BPoint* controlPoints,
									bool filled) const;
			BRect				FillBezier(BPoint* controlPoints,
									const BGradient& gradient);

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

			BRect				StrokeRoundRect(const BRect& r, float xRadius,
									float yRadius) const;

			BRect				FillRoundRect(const BRect& r, float xRadius,
									float yRadius) const;
			BRect				FillRoundRect(const BRect& r, float xRadius,
									float yRadius,
									const BGradient& gradient);

			void				AlignEllipseRect(BRect* rect,
									bool filled) const;

			BRect				DrawEllipse(BRect r, bool filled) const;
			BRect				FillEllipse(BRect r,
									const BGradient& gradient);

			BRect				StrokeArc(BPoint center, float xRadius,
									float yRadius, float angle,
									float span) const;

			BRect				FillArc(BPoint center, float xRadius,
									float yRadius, float angle,
									float span) const;
			BRect				FillArc(BPoint center, float xRadius,
									float yRadius, float angle, float span,
									const BGradient& gradient);

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
									FontCacheReference* cacheReference
										= NULL) const;
			BRect				BoundingBox(const char* utf8String,
									uint32 length, const BPoint* offsets,
									BPoint* penLocation,
									FontCacheReference* cacheReference
										= NULL) const;

			float				StringWidth(const char* utf8String,
									uint32 length,
									const escapement_delta* delta = NULL);

			BRect				DrawBitmap(const ServerBitmap* bitmap,
									BRect bitmapRect, BRect viewRect,
									uint32 options) const;

			BRect				FillRegion(const BRegion* region) const;
			BRect				FillRegion(const BRegion* region,
									const BGradient& gradient);

			BRect				InvertRect(const BRect& r) const;

	inline	BRect				TransformAndClipRect(BRect rect) const;
	inline	BRect				ClipRect(BRect rect) const;
	inline	BRect				TransformAlignAndClipRect(BRect rect) const;
	inline	BRect				AlignAndClipRect(BRect rect) const;
	inline	BRect				AlignRect(BRect rect) const;

			void				SetRendererOffset(int32 offsetX,
									int32 offsetY);

private:
	class BitmapPainter;
	class SolidPatternGuard;

	friend class BitmapPainter;

	static constexpr float kPixelCenterOffset = 0.5f;
	static constexpr int kDefaultEllipseDivisions = 12;
	static constexpr int kMaxEllipseDivisions = 4096;
	static constexpr float kDefaultGradientDistance = 100.0f;
	static constexpr float kGradientOffsetScale = 255.0f;
	static constexpr uint8 kMaxAlpha = 255;
	static constexpr float kGammaThreshold = 0.5f;
	static constexpr float kDefaultApproximationScale = 2.0f;

			float				_Align(float coord, bool round,
									bool centerOffset) const;
			void				_Align(BPoint* point, bool round,
									bool centerOffset) const;
			void				_Align(BPoint* point,
									bool centerOffset = true) const;
			BPoint				_Align(const BPoint& point,
									bool centerOffset = true) const;
			BRect				_Clipped(const BRect& rect) const;

			void				_UpdateDrawingMode();
			void				_SetRendererColor(const rgb_color& color) const;

			bool				_CanOptimizeSolidDraw() const;
			bool				_TryOptimizedRectFill(const BPoint& a,
									const BPoint& b) const;
			bool				_GetPatternColor(rgb_color& color) const;

			std::unique_ptr<BPoint[]>
								_CreateRoundedOffsets(const BPoint* offsets,
									uint32 count) const;

			void				_FinalizeGradientTransform(
									agg::trans_affine& matrix) const;

	template<typename DrawFunc>
			void				_IterateClipBoxes(DrawFunc func) const;

			BRect				_DrawTriangle(BPoint pt1, BPoint pt2,
									BPoint pt3, bool fill) const;

			void				_IterateShapeData(const int32& opCount,
									const uint32* opList, const int32& ptCount,
									const BPoint* points,
									const BPoint& viewToScreenOffset,
									float viewScale) const;

			void				_InvertRect32(BRect r) const;
			void				_BlendRect32(const BRect& r,
									const rgb_color& c) const;

			template<class VertexSource>
			BRect				_BoundingBox(VertexSource& path) const;

			template<class VertexSource>
			BRect				_StrokePath(VertexSource& path) const;
			template<class VertexSource>
			BRect				_StrokePath(VertexSource& path,
									cap_mode capMode) const;
			template<class VertexSource>
			BRect				_FillPath(VertexSource& path) const;
			template<class VertexSource>
			BRect				_RasterizePath(VertexSource& path) const;

			template<class VertexSource>
			BRect				_FillPath(VertexSource& path,
									const BGradient& gradient);
			template<class VertexSource>
			BRect				_RasterizePath(VertexSource& path,
									const BGradient& gradient);

			void				_CalcLinearGradientTransform(BPoint startPoint,
									BPoint endPoint, agg::trans_affine& mtx,
									float gradient_d2 = kDefaultGradientDistance) const;
			void				_CalcRadialGradientTransform(BPoint center,
									agg::trans_affine& mtx,
									float gradient_d2 = kDefaultGradientDistance) const;

			void				_MakeGradient(const BGradient& gradient,
									int32 colorCount, uint32* colors,
									int32 arrayOffset, int32 arraySize) const;

			template<class Array>
			void				_MakeGradient(Array& array,
									const BGradient& gradient) const;

			template<class VertexSource, typename GradientFunction>
			void				_RasterizePath(VertexSource& path,
									const BGradient& gradient,
									GradientFunction function,
									agg::trans_affine& gradientTransform,
									int gradientStop = 100);

private:
			bool				fSubpixelPrecise : 1;
			bool				fValidClipping : 1;
			bool				fAttached : 1;
			bool				fIdentityTransform : 1;

			Transformable		fTransform;
			float				fPenSize;
			const BRegion*		fClippingRegion;
			drawing_mode		fDrawingMode;
			source_alpha		fAlphaSrcMode;
			alpha_function		fAlphaFncMode;
			cap_mode			fLineCapMode;
			join_mode			fLineJoinMode;
			float				fMiterLimit;

			PatternHandler		fPatternHandler;

	mutable	AGGTextRenderer		fTextRenderer;

	mutable	PainterAggInterface	fInternal;
};


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
