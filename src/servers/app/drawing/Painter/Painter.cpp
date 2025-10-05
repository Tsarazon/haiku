/*
 * Copyright 2009, Christian Packmann.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2005-2014, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>
 * Copyright 2025, Haiku Blend2D Migration
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "Painter.h"
#include "Blend2dDebug.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <Bitmap.h>
#include <GraphicsDefs.h>
#include <Region.h>
#include <String.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GradientRadialFocus.h>
#include <GradientDiamond.h>
#include <GradientConic.h>

#include "drawing_support.h"
#include "DrawState.h"
#include "BitmapPainter.h"
#include "GlobalSubpixelSettings.h"
#include "PatternHandler.h"
#include "RenderingBuffer.h"
#include "ServerBitmap.h"
#include "ServerFont.h"

using std::nothrow;

#undef TRACE
//#define TRACE_PAINTER
#ifdef TRACE_PAINTER
#	define TRACE(x...)		printf(x)
#else
#	define TRACE(x...)
#endif

//#define TRACE_GRADIENTS
#ifdef TRACE_GRADIENTS
#	define GTRACE(x...)		debug_printf(x)
#else
#	define GTRACE(x...)
#endif

// ============================================================================
// Helper: Pattern Guard for Gradients
// ============================================================================

class SolidPatternGuard {
public:
	SolidPatternGuard(Painter* painter)
		: fPainter(painter), fPattern(fPainter->Pattern())
	{
		fPainter->SetPattern(B_SOLID_HIGH);
	}

	~SolidPatternGuard()
	{
		fPainter->SetPattern(fPattern);
	}

private:
	Painter*	fPainter;
	pattern		fPattern;
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

Painter::Painter()
	:
	fSubpixelPrecise(false),
	fValidClipping(false),
	fAttached(false),
	fIdentityTransform(true),
	fPenSize(1.0),
	fClippingRegion(NULL),
	fDrawingMode(B_OP_COPY),
	fAlphaSrcMode(B_PIXEL_ALPHA),
	fAlphaFncMode(B_ALPHA_OVERLAY),
	fLineCapMode(B_BUTT_CAP),
	fLineJoinMode(B_MITER_JOIN),
	fMiterLimit(B_DEFAULT_MITER_LIMIT),
	fPatternHandler(),
	fTextRenderer(fInternal.fBLContext, fTransform),
	fInternal(fPatternHandler)
{
	_PixelFormat().SetDrawingMode(fDrawingMode, fAlphaSrcMode, fAlphaFncMode);
}

Painter::~Painter()
{
}

// ============================================================================
// Buffer Attachment
// ============================================================================

void
Painter::AttachToBuffer(RenderingBuffer* buffer)
{
	if (buffer && buffer->InitCheck() >= B_OK
		&& (buffer->ColorSpace() == B_RGBA32
			|| buffer->ColorSpace() == B_RGB32)) {
		
		// Attach to external buffer via PainterInterface
		bool success = fInternal.AttachToBuffer(
			(uint8*)buffer->Bits(),
			buffer->Width(),
			buffer->Height(),
			buffer->BytesPerRow()
		);

		if (!success) {
			BLEND2D_ERROR("Painter::AttachToBuffer() - Failed to attach\n");
			return;
		}

		fAttached = true;
		fValidClipping = fClippingRegion != NULL
			&& fClippingRegion->Frame().IsValid();

		_SetRendererColor(fPatternHandler.HighColor());
	}
}

void
Painter::DetachFromBuffer()
{
	fAttached = false;
	fValidClipping = false;
}

BRect
Painter::Bounds() const
{
	if (!fAttached || !fInternal.fImageValid)
		return BRect(0, 0, -1, -1);
	
	return BRect(0, 0, 
				 fInternal.fBLImage.width() - 1,
				 fInternal.fBLImage.height() - 1);
}

// ============================================================================
// State Management
// ============================================================================

void
Painter::SetDrawState(const DrawState* state, int32 xOffset, int32 yOffset)
{
	SetTransform(state->CombinedTransform(), xOffset, yOffset);
	SetPenSize(state->PenSize());
	SetFont(state);

	fSubpixelPrecise = state->SubPixelPrecise();

	// Alpha mask support (for transparency effects)
	if (state->GetAlphaMask() != NULL) {
		fInternal.fMaskedUnpackedScanline = state->GetAlphaMask()->Scanline();
		fInternal.fClippedAlphaMask = state->GetAlphaMask()->Mask();
	} else {
		fInternal.fMaskedUnpackedScanline = NULL;
		fInternal.fClippedAlphaMask = NULL;
	}

	// Update drawing mode
	bool updateDrawingMode = 
		state->GetPattern() == fPatternHandler.GetPattern()
		&& (state->GetDrawingMode() != fDrawingMode
			|| (state->GetDrawingMode() == B_OP_ALPHA
				&& (state->AlphaSrcMode() != fAlphaSrcMode
					|| state->AlphaFncMode() != fAlphaFncMode)));

	fDrawingMode = state->GetDrawingMode();
	fAlphaSrcMode = state->AlphaSrcMode();
	fAlphaFncMode = state->AlphaFncMode();
	
	SetPattern(state->GetPattern().GetPattern());
	fPatternHandler.SetOffsets(xOffset, yOffset);
	
	fLineCapMode = state->LineCapMode();
	fLineJoinMode = state->LineJoinMode();
	fMiterLimit = state->MiterLimit();

	SetHighColor(state->HighColor());
	SetLowColor(state->LowColor());

	if (updateDrawingMode)
		_UpdateDrawingMode();
}

void
Painter::ConstrainClipping(const BRegion* region)
{
	fClippingRegion = region;
	fValidClipping = region->Frame().IsValid() && fAttached;

	if (fValidClipping) {
		// Set clipping region in Blend2D context
		BRect bounds = region->Frame();
		
		BLResult result = fInternal.fBLContext.setClipRect(
			BLRect(bounds.left, bounds.top, 
				   bounds.Width() + 1, bounds.Height() + 1)
		);
		
		BLEND2D_CHECK_WARN(result);
	}
}

void
Painter::SetTransform(BAffineTransform transform, int32 xOffset, int32 yOffset)
{
	fIdentityTransform = transform.IsIdentity();
	
	if (!fIdentityTransform) {
		// Convert BAffineTransform to BLMatrix2D
		fTransform.fMatrix.reset(
			transform.sx, transform.shy,
			transform.shx, transform.sy,
			transform.tx + xOffset, transform.ty + yOffset
		);
	} else {
		fTransform.fMatrix.reset();
		if (xOffset != 0 || yOffset != 0) {
			fTransform.fMatrix.translate(xOffset, yOffset);
		}
	}
}

void
Painter::SetHighColor(const rgb_color& color)
{
	if (fPatternHandler.HighColor() == color)
		return;
	
	fPatternHandler.SetHighColor(color);
	
	if (*(fPatternHandler.GetR5Pattern()) == B_SOLID_HIGH)
		_SetRendererColor(color);
}

void
Painter::SetLowColor(const rgb_color& color)
{
	fPatternHandler.SetLowColor(color);
	
	if (*(fPatternHandler.GetR5Pattern()) == B_SOLID_LOW)
		_SetRendererColor(color);
}

void
Painter::SetDrawingMode(drawing_mode mode)
{
	if (fDrawingMode != mode) {
		fDrawingMode = mode;
		_UpdateDrawingMode();
	}
}

void
Painter::SetBlendingMode(source_alpha srcAlpha, alpha_function alphaFunc)
{
	if (fAlphaSrcMode != srcAlpha || fAlphaFncMode != alphaFunc) {
		fAlphaSrcMode = srcAlpha;
		fAlphaFncMode = alphaFunc;
		
		if (fDrawingMode == B_OP_ALPHA)
			_UpdateDrawingMode();
	}
}

void
Painter::SetPenSize(float size)
{
	fPenSize = size;
}

void
Painter::SetStrokeMode(cap_mode lineCap, join_mode joinMode, float miterLimit)
{
	fLineCapMode = lineCap;
	fLineJoinMode = joinMode;
	fMiterLimit = miterLimit;
}

void
Painter::SetFillRule(int32 fillRule)
{
	// Blend2D fill rule
	BLFillRule blFillRule = (fillRule == B_EVEN_ODD)
		? BL_FILL_RULE_EVEN_ODD
		: BL_FILL_RULE_NON_ZERO;
	
	fInternal.fBLContext.setFillRule(blFillRule);
}

void
Painter::SetPattern(const pattern& p)
{
	if (p != *fPatternHandler.GetR5Pattern()) {
		fPatternHandler.SetPattern(p);
		_UpdateDrawingMode();

		if (fPatternHandler.IsSolidHigh()) {
			_SetRendererColor(fPatternHandler.HighColor());
		} else if (fPatternHandler.IsSolidLow()) {
			_SetRendererColor(fPatternHandler.LowColor());
		}
	}
}

void
Painter::SetFont(const ServerFont& font)
{
	fTextRenderer.SetFont(font);
	fTextRenderer.SetAntialiasing(!(font.Flags() & B_DISABLE_ANTIALIASING));
}

void
Painter::SetFont(const DrawState* state)
{
	fTextRenderer.SetFont(state->Font());
	fTextRenderer.SetAntialiasing(!state->ForceFontAliasing()
		&& (state->Font().Flags() & B_DISABLE_ANTIALIASING) == 0);
}

// ============================================================================
// Line Drawing
// ============================================================================

void
Painter::StrokeLine(BPoint a, BPoint b)
{
	if (!fValidClipping)
		return;

	_Align(&a, false);
	_Align(&b, false);

	// Optimized version for simple cases
	if (fPenSize == 1.0 && fIdentityTransform
		&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& fInternal.fMaskedUnpackedScanline == NULL) {
		
		pattern pat = *fPatternHandler.GetR5Pattern();
		if (pat == B_SOLID_HIGH && StraightLine(a, b, fPatternHandler.HighColor()))
			return;
		if (pat == B_SOLID_LOW && StraightLine(a, b, fPatternHandler.LowColor()))
			return;
	}

	// Use Blend2D path for line
	BLPath path;
	
	if (a == b) {
		// Special case: dot
		if (fPenSize == 1.0 && !fSubpixelPrecise && fIdentityTransform) {
			if (fClippingRegion->Contains(a)) {
				rgb_color color = fPatternHandler.HighColor();
				_PixelFormat().blend_pixel((int)a.x, (int)a.y,
					PixelFormat::color_type(color.red, color.green, 
											color.blue, color.alpha),
					255);
			}
		} else {
			// Draw small rectangle for dot
			path.moveTo(a.x, a.y);
			path.lineTo(a.x + 1, a.y);
			path.lineTo(a.x + 1, a.y + 1);
			path.lineTo(a.x, a.y + 1);
			path.close();
			_FillPath(path);
		}
	} else {
		// Align for pixel-perfect rendering
		if (!fSubpixelPrecise && fmodf(fPenSize, 2.0) != 0.0) {
			_Align(&a, true);
			_Align(&b, true);
		}

		path.moveTo(a.x, a.y);
		path.lineTo(b.x, b.y);

		if (!fSubpixelPrecise && fPenSize == 1.0f) {
			_StrokePath(path, B_SQUARE_CAP);
		} else {
			_StrokePath(path);
		}
	}
}

bool
Painter::StraightLine(BPoint a, BPoint b, const rgb_color& c) const
{
	if (!fValidClipping)
		return false;

	// Optimized horizontal/vertical line drawing
	if (a.x == b.x) {
		// Vertical line
		int32 x = (int32)a.x;
		int32 y1 = (int32)min_c(a.y, b.y);
		int32 y2 = (int32)max_c(a.y, b.y);

		for (int32 y = y1; y <= y2; y++) {
			if (fClippingRegion->Contains(BPoint(x, y))) {
				_PixelFormat().blend_pixel(x, y,
					PixelFormat::color_type(c.red, c.green, c.blue, c.alpha),
					255);
			}
		}
		return true;
	}

	if (a.y == b.y) {
		// Horizontal line
		int32 y = (int32)a.y;
		int32 x1 = (int32)min_c(a.x, b.x);
		int32 x2 = (int32)max_c(a.x, b.x);

		for (int32 x = x1; x <= x2; x++) {
			if (fClippingRegion->Contains(BPoint(x, y))) {
				_PixelFormat().blend_pixel(x, y,
					PixelFormat::color_type(c.red, c.green, c.blue, c.alpha),
					255);
			}
		}
		return true;
	}

	return false;
}

// ============================================================================
// Triangle Drawing
// ============================================================================

BRect
Painter::StrokeTriangle(BPoint pt1, BPoint pt2, BPoint pt3) const
{
	return _DrawTriangle(pt1, pt2, pt3, false);
}

BRect
Painter::FillTriangle(BPoint pt1, BPoint pt2, BPoint pt3) const
{
	return _DrawTriangle(pt1, pt2, pt3, true);
}

BRect
Painter::FillTriangle(BPoint pt1, BPoint pt2, BPoint pt3,
	const BGradient& gradient)
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	_Align(&pt1);
	_Align(&pt2);
	_Align(&pt3);

	BLPath path;
	path.moveTo(pt1.x, pt1.y);
	path.lineTo(pt2.x, pt2.y);
	path.lineTo(pt3.x, pt3.y);
	path.close();

	return _FillPath(path, gradient);
}

BRect
Painter::_DrawTriangle(BPoint pt1, BPoint pt2, BPoint pt3, bool fill) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	_Align(&pt1);
	_Align(&pt2);
	_Align(&pt3);

	BLPath path;
	path.moveTo(pt1.x, pt1.y);
	path.lineTo(pt2.x, pt2.y);
	path.lineTo(pt3.x, pt3.y);
	path.close();

	if (fill)
		return _FillPath(path);
	
	return _StrokePath(path);
}

// ============================================================================
// Polygon Drawing
// ============================================================================

BRect
Painter::DrawPolygon(BPoint* p, int32 numPts, bool filled, bool closed) const
{
	if (!fValidClipping || numPts == 0)
		return BRect(0, 0, -1, -1);

	bool centerOffset = !filled && fIdentityTransform
		&& fmodf(fPenSize, 2.0) != 0.0;

	BLPath path;
	
	_Align(p, centerOffset);
	path.moveTo(p->x, p->y);

	for (int32 i = 1; i < numPts; i++) {
		p++;
		_Align(p, centerOffset);
		path.lineTo(p->x, p->y);
	}

	if (closed)
		path.close();

	if (filled)
		return _FillPath(path);

	return _StrokePath(path);
}

BRect
Painter::FillPolygon(BPoint* p, int32 numPts, const BGradient& gradient,
	bool closed)
{
	if (!fValidClipping || numPts == 0)
		return BRect(0, 0, -1, -1);

	BLPath path;
	
	_Align(p);
	path.moveTo(p->x, p->y);

	for (int32 i = 1; i < numPts; i++) {
		p++;
		_Align(p);
		path.lineTo(p->x, p->y);
	}

	if (closed)
		path.close();

	return _FillPath(path, gradient);
}

// ============================================================================
// Bezier Curves
// ============================================================================

BRect
Painter::DrawBezier(BPoint* p, bool filled) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	_Align(&p[0]);
	_Align(&p[1]);
	_Align(&p[2]);
	_Align(&p[3]);

	BLPath path;
	path.moveTo(p[0].x, p[0].y);
	path.cubicTo(p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);

	if (filled) {
		path.close();
		return _FillPath(path);
	}

	return _StrokePath(path);
}

BRect
Painter::FillBezier(BPoint* p, const BGradient& gradient)
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	_Align(&p[0]);
	_Align(&p[1]);
	_Align(&p[2]);
	_Align(&p[3]);

	BLPath path;
	path.moveTo(p[0].x, p[0].y);
	path.cubicTo(p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);
	path.close();

	return _FillPath(path, gradient);
}

// ============================================================================
// Shape Drawing (BShape support)
// ============================================================================

BRect
Painter::DrawShape(const int32& opCount, const uint32* opList,
	const int32& ptCount, const BPoint* points, bool filled,
	const BPoint& viewToScreenOffset, float viewScale) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BLPath path;
	_IterateShapeData(opCount, opList, ptCount, points, 
					  viewToScreenOffset, viewScale, path);

	if (filled)
		return _FillPath(path);

	return _StrokePath(path);
}

BRect
Painter::FillShape(const int32& opCount, const uint32* opList,
	const int32& ptCount, const BPoint* points, const BGradient& gradient,
	const BPoint& viewToScreenOffset, float viewScale)
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BLPath path;
	_IterateShapeData(opCount, opList, ptCount, points,
					  viewToScreenOffset, viewScale, path);

	return _FillPath(path, gradient);
}

void
Painter::_IterateShapeData(const int32& opCount, const uint32* opList,
	const int32& ptCount, const BPoint* points,
	const BPoint& viewToScreenOffset, float viewScale, BLPath& path) const
{
	for (int32 i = 0; i < opCount; i++) {
		uint32 op = opList[i] & 0xFF000000;
		
		if ((op & OP_MOVETO) != 0) {
			path.moveTo(
				points->x * viewScale + viewToScreenOffset.x,
				points->y * viewScale + viewToScreenOffset.y
			);
			points++;
		}

		if ((op & OP_LINETO) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			while (count--) {
				path.lineTo(
					points->x * viewScale + viewToScreenOffset.x,
					points->y * viewScale + viewToScreenOffset.y
				);
				points++;
			}
		}

		if ((op & OP_BEZIERTO) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			while (count >= 3) {
				path.cubicTo(
					points[0].x * viewScale + viewToScreenOffset.x,
					points[0].y * viewScale + viewToScreenOffset.y,
					points[1].x * viewScale + viewToScreenOffset.x,
					points[1].y * viewScale + viewToScreenOffset.y,
					points[2].x * viewScale + viewToScreenOffset.x,
					points[2].y * viewScale + viewToScreenOffset.y
				);
				points += 3;
				count -= 3;
			}
		}

		// Arc support
		if ((op & OP_LARGE_ARC_TO_CW) != 0 || (op & OP_LARGE_ARC_TO_CCW) != 0
			|| (op & OP_SMALL_ARC_TO_CW) != 0 || (op & OP_SMALL_ARC_TO_CCW) != 0) {
			
			int32 count = opList[i] & 0x00FFFFFF;
			while (count >= 3) {
				double rx = points[0].x * viewScale;
				double ry = points[0].y * viewScale;
				double angle = points[1].x;
				bool largeArc = (op & (OP_LARGE_ARC_TO_CW | OP_LARGE_ARC_TO_CCW)) != 0;
				bool sweep = (op & (OP_SMALL_ARC_TO_CW | OP_LARGE_ARC_TO_CW)) != 0;
				double x = points[2].x * viewScale + viewToScreenOffset.x;
				double y = points[2].y * viewScale + viewToScreenOffset.y;

				path.arcTo(rx, ry, angle, largeArc, sweep, x, y);
				
				points += 3;
				count -= 3;
			}
		}

		if ((op & OP_CLOSE) != 0)
			path.close();
	}
}

// ============================================================================
// Rectangle Drawing  
// ============================================================================

BRect
Painter::StrokeRect(const BRect& r) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BPoint a(r.left, r.top);
	BPoint b(r.right, r.bottom);
	_Align(&a, false);
	_Align(&b, false);

	// Optimized version
	if (fPenSize == 1.0 && fIdentityTransform
		&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& fInternal.fMaskedUnpackedScanline == NULL) {
		
		pattern p = *fPatternHandler.GetR5Pattern();
		if (p == B_SOLID_HIGH) {
			BRect rect(a, b);
			StrokeRect(rect, fPatternHandler.HighColor());
			return _Clipped(rect);
		} else if (p == B_SOLID_LOW) {
			BRect rect(a, b);
			StrokeRect(rect, fPatternHandler.LowColor());
			return _Clipped(rect);
		}
	}

	// Pixel center offset
	if (fIdentityTransform && fmodf(fPenSize, 2.0) != 0.0) {
		a.x += 0.5;
		a.y += 0.5;
		b.x += 0.5;
		b.y += 0.5;
	}

	BLPath path;
	path.moveTo(a.x, a.y);
	
	if (a.x == b.x || a.y == b.y) {
		path.lineTo(b.x, b.y);
	} else {
		path.lineTo(b.x, a.y);
		path.lineTo(b.x, b.y);
		path.lineTo(a.x, b.y);
	}
	path.close();

	return _StrokePath(path);
}

void
Painter::StrokeRect(const BRect& r, const rgb_color& c) const
{
	StraightLine(BPoint(r.left, r.top), BPoint(r.right - 1, r.top), c);
	StraightLine(BPoint(r.right, r.top), BPoint(r.right, r.bottom - 1), c);
	StraightLine(BPoint(r.right, r.bottom), BPoint(r.left + 1, r.bottom), c);
	StraightLine(BPoint(r.left, r.bottom), BPoint(r.left, r.top + 1), c);
}

BRect
Painter::FillRect(const BRect& r) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BPoint a(min_c(r.left, r.right), min_c(r.top, r.bottom));
	BPoint b(max_c(r.left, r.right), max_c(r.top, r.bottom));
	_Align(&a, true, false);
	_Align(&b, true, false);

	// Optimized versions
	if ((fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& fInternal.fMaskedUnpackedScanline == NULL && fIdentityTransform) {
		
		pattern p = *fPatternHandler.GetR5Pattern();
		if (p == B_SOLID_HIGH) {
			BRect rect(a, b);
			FillRect(rect, fPatternHandler.HighColor());
			return _Clipped(rect);
		} else if (p == B_SOLID_LOW) {
			BRect rect(a, b);
			FillRect(rect, fPatternHandler.LowColor());
			return _Clipped(rect);
		}
	}

	// Blend2D path
	b.x += 1.0;
	b.y += 1.0;

	BLPath path;
	path.moveTo(a.x, a.y);
	path.lineTo(b.x, a.y);
	path.lineTo(b.x, b.y);
	path.lineTo(a.x, b.y);
	path.close();

	return _FillPath(path);
}

void
Painter::FillRect(const BRect& r, const rgb_color& c) const
{
	if (!fValidClipping)
		return;

	int32 left = (int32)r.left;
	int32 top = (int32)r.top;
	int32 right = (int32)r.right;
	int32 bottom = (int32)r.bottom;

	PixelFormat::color_type color(c.red, c.green, c.blue, c.alpha);

	for (int32 y = top; y <= bottom; y++) {
		for (int32 x = left; x <= right; x++) {
			if (fClippingRegion->Contains(BPoint(x, y))) {
				_PixelFormat().blend_pixel(x, y, color, 255);
			}
		}
	}
}

BRect
Painter::FillRect(const BRect& r, const BGradient& gradient)
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BPoint a(min_c(r.left, r.right), min_c(r.top, r.bottom));
	BPoint b(max_c(r.left, r.right), max_c(r.top, r.bottom));
	_Align(&a, true, false);
	_Align(&b, true, false);

	// Optimized vertical gradient
	if (gradient.GetType() == BGradient::TYPE_LINEAR
		&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& fInternal.fMaskedUnpackedScanline == NULL && fIdentityTransform) {
		
		const BGradientLinear* linearGradient =
			dynamic_cast<const BGradientLinear*>(&gradient);
		
		if (linearGradient->Start().x == linearGradient->End().x
			&& linearGradient->Start().y <= linearGradient->End().y) {
			BRect rect(a, b);
			FillRectVerticalGradient(rect, *linearGradient);
			return _Clipped(rect);
		}
	}

	b.x += 1.0;
	b.y += 1.0;

	BLPath path;
	path.moveTo(a.x, a.y);
	path.lineTo(b.x, a.y);
	path.lineTo(b.x, b.y);
	path.lineTo(a.x, b.y);
	path.close();

	return _FillPath(path, gradient);
}

void
Painter::FillRectVerticalGradient(BRect r, const BGradientLinear& gradient) const
{
	if (!fValidClipping)
		return;

	r = r & fClippingRegion->Frame();

	int32 gradientArraySize = r.IntegerHeight() + 1;
	uint32* gradientArray = new(std::nothrow) uint32[gradientArraySize];
	if (!gradientArray)
		return;

	int32 gradientTop = (int32)gradient.Start().y;
	int32 gradientBottom = (int32)gradient.End().y;
	int32 colorCount = gradientBottom - gradientTop + 1;

	if (colorCount < 0) {
		delete[] gradientArray;
		return;
	}

	_MakeGradient(gradient, colorCount, gradientArray,
		gradientTop - (int32)r.top, gradientArraySize);

	int32 left = (int32)r.left;
	int32 top = (int32)r.top;
	int32 right = (int32)r.right;
	int32 bottom = (int32)r.bottom;

	for (int32 y = top; y <= bottom; y++) {
		uint32 color32 = gradientArray[y - top];
		rgb_color color;
		color.blue = color32 & 0xFF;
		color.green = (color32 >> 8) & 0xFF;
		color.red = (color32 >> 16) & 0xFF;
		color.alpha = (color32 >> 24) & 0xFF;

		PixelFormat::color_type c(color.red, color.green, color.blue, color.alpha);

		for (int32 x = left; x <= right; x++) {
			if (fClippingRegion->Contains(BPoint(x, y))) {
				_PixelFormat().blend_pixel(x, y, c, 255);
			}
		}
	}

	delete[] gradientArray;
}

void
Painter::FillRectNoClipping(const clipping_rect& r, const rgb_color& c) const
{
	PixelFormat::color_type color(c.red, c.green, c.blue, c.alpha);

	for (int32 y = r.top; y <= r.bottom; y++) {
		for (int32 x = r.left; x <= r.right; x++) {
			_PixelFormat().blend_pixel(x, y, color, 255);
		}
	}
}

// ============================================================================
// Rounded Rectangle
// ============================================================================

BRect
Painter::StrokeRoundRect(const BRect& r, float xRadius, float yRadius) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	bool centerOffset = fmodf(fPenSize, 2.0) != 0.0;
	_Align(&lt, centerOffset);
	_Align(&rb, centerOffset);

	BLPath path;
	path.addRoundRect(BLRoundRect(lt.x, lt.y, rb.x - lt.x, rb.y - lt.y,
								  xRadius, yRadius));

	return _StrokePath(path);
}

BRect
Painter::FillRoundRect(const BRect& r, float xRadius, float yRadius) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	_Align(&lt, false);
	_Align(&rb, false);

	rb.x += 1.0;
	rb.y += 1.0;

	BLPath path;
	path.addRoundRect(BLRoundRect(lt.x, lt.y, rb.x - lt.x, rb.y - lt.y,
								  xRadius, yRadius));

	return _FillPath(path);
}

BRect
Painter::FillRoundRect(const BRect& r, float xRadius, float yRadius,
	const BGradient& gradient)
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	_Align(&lt, false);
	_Align(&rb, false);

	rb.x += 1.0;
	rb.y += 1.0;

	BLPath path;
	path.addRoundRect(BLRoundRect(lt.x, lt.y, rb.x - lt.x, rb.y - lt.y,
								  xRadius, yRadius));

	return _FillPath(path, gradient);
}

// ============================================================================
// Ellipse Drawing
// ============================================================================

void
Painter::AlignEllipseRect(BRect* rect, bool filled) const
{
	if (!fSubpixelPrecise) {
		align_rect_to_pixels(rect);
		rect->right++;
		rect->bottom++;
		
		if (!filled && fmodf(fPenSize, 2.0) != 0.0) {
			rect->InsetBy(0.5, 0.5);
		}
	}
}

BRect
Painter::DrawEllipse(BRect r, bool fill) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	AlignEllipseRect(&r, fill);

	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius, r.top + yRadius);

	BLPath path;
	path.addEllipse(BLEllipse(center.x, center.y, xRadius, yRadius));

	if (fill)
		return _FillPath(path);
	
	return _StrokePath(path);
}

BRect
Painter::FillEllipse(BRect r, const BGradient& gradient)
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	AlignEllipseRect(&r, true);

	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius, r.top + yRadius);

	BLPath path;
	path.addEllipse(BLEllipse(center.x, center.y, xRadius, yRadius));

	return _FillPath(path, gradient);
}

// ============================================================================
// Arc Drawing
// ============================================================================

BRect
Painter::StrokeArc(BPoint center, float xRadius, float yRadius,
	float angle, float span) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	_Align(&center);

	BLPath path;
	
	// Convert to radians
	double startAngle = (angle * M_PI) / 180.0;
	double endAngle = ((angle + span) * M_PI) / 180.0;

	// Add arc (Blend2D uses radians)
	path.addArc(BLArc(center.x, center.y, xRadius, yRadius, 
					  startAngle, endAngle));

	return _StrokePath(path);
}

BRect
Painter::FillArc(BPoint center, float xRadius, float yRadius,
	float angle, float span) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	_Align(&center);

	double startAngle = (angle * M_PI) / 180.0;
	double endAngle = ((angle + span) * M_PI) / 180.0;

	BLPath path;
	path.moveTo(center.x, center.y);
	path.addArc(BLArc(center.x, center.y, xRadius, yRadius,
					  startAngle, endAngle));
	path.close();

	return _FillPath(path);
}

BRect
Painter::FillArc(BPoint center, float xRadius, float yRadius,
	float angle, float span, const BGradient& gradient)
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	_Align(&center);

	double startAngle = (angle * M_PI) / 180.0;
	double endAngle = ((angle + span) * M_PI) / 180.0;

	BLPath path;
	path.moveTo(center.x, center.y);
	path.addArc(BLArc(center.x, center.y, xRadius, yRadius,
					  startAngle, endAngle));
	path.close();

	return _FillPath(path, gradient);
}

// ============================================================================
// Text Rendering (delegated to Blend2dTextRenderer)
// ============================================================================

BRect
Painter::DrawString(const char* utf8String, uint32 length, BPoint baseLine,
	const escapement_delta* delta, FontCacheReference* cacheReference)
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	if (!fSubpixelPrecise) {
		baseLine.x = roundf(baseLine.x);
		baseLine.y = roundf(baseLine.y);
	}

	SolidPatternGuard _(this);

	BRect bounds = fTextRenderer.RenderString(utf8String, length,
		baseLine, fClippingRegion->Frame(), false, NULL, delta,
		cacheReference);

	return _Clipped(bounds);
}

BRect
Painter::DrawString(const char* utf8String, uint32 length,
	const BPoint* offsets, FontCacheReference* cacheReference)
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	SolidPatternGuard _(this);

	BRect bounds = fTextRenderer.RenderString(utf8String, length,
		offsets, fClippingRegion->Frame(), false, NULL,
		cacheReference);

	return _Clipped(bounds);
}

BRect
Painter::BoundingBox(const char* utf8String, uint32 length, BPoint baseLine,
	BPoint* penLocation, const escapement_delta* delta,
	FontCacheReference* cacheReference) const
{
	if (!fSubpixelPrecise) {
		baseLine.x = roundf(baseLine.x);
		baseLine.y = roundf(baseLine.y);
	}

	static BRect dummy;
	return fTextRenderer.RenderString(utf8String, length,
		baseLine, dummy, true, penLocation, delta, cacheReference);
}

BRect
Painter::BoundingBox(const char* utf8String, uint32 length,
	const BPoint* offsets, BPoint* penLocation,
	FontCacheReference* cacheReference) const
{
	static BRect dummy;
	return fTextRenderer.RenderString(utf8String, length,
		offsets, dummy, true, penLocation, cacheReference);
}

float
Painter::StringWidth(const char* utf8String, uint32 length,
	const escapement_delta* delta)
{
	return Font().StringWidth(utf8String, length, delta);
}

// ============================================================================
// Bitmap Drawing (delegated to BitmapPainter)
// ============================================================================

BRect
Painter::DrawBitmap(const ServerBitmap* bitmap, BRect bitmapRect,
	BRect viewRect, uint32 options) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BRect touched = TransformAlignAndClipRect(viewRect);

	if (touched.IsValid()) {
		BitmapPainter bitmapPainter(this, bitmap, options);
		bitmapPainter.Draw(bitmapRect, viewRect);
	}

	return touched;
}

// ============================================================================
// Region Operations
// ============================================================================

BRect
Painter::FillRegion(const BRegion* region) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BRegion copy(*region);
	int32 count = copy.CountRects();
	BRect touched = FillRect(copy.RectAt(0));
	
	for (int32 i = 1; i < count; i++) {
		touched = touched | FillRect(copy.RectAt(i));
	}
	
	return touched;
}

BRect
Painter::FillRegion(const BRegion* region, const BGradient& gradient)
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BRegion copy(*region);
	int32 count = copy.CountRects();
	BRect touched = FillRect(copy.RectAt(0), gradient);
	
	for (int32 i = 1; i < count; i++) {
		touched = touched | FillRect(copy.RectAt(i), gradient);
	}
	
	return touched;
}

BRect
Painter::InvertRect(const BRect& r) const
{
	if (!fValidClipping)
		return BRect(0, 0, -1, -1);

	BRegion region(r);
	region.IntersectWith(fClippingRegion);

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++)
		_InvertRect32(region.RectAt(i));

	return _Clipped(r);
}

void
Painter::_InvertRect32(BRect r) const
{
	int32 left = (int32)r.left;
	int32 top = (int32)r.top;
	int32 right = (int32)r.right;
	int32 bottom = (int32)r.bottom;

	for (int32 y = top; y <= bottom; y++) {
		for (int32 x = left; x <= right; x++) {
			if (!fClippingRegion->Contains(BPoint(x, y)))
				continue;

			const uint8* p = _PixelFormat().pix_ptr(x, y);
			uint8 r = 255 - p[2];
			uint8 g = 255 - p[1];
			uint8 b = 255 - p[0];

			_PixelFormat().blend_pixel(x, y,
				PixelFormat::color_type(r, g, b, 255), 255);
		}
	}
}

void
Painter::SetRendererOffset(int32 offsetX, int32 offsetY)
{
	// Note: In Blend2D we handle offsets through transformation matrix
	// This method is kept for compatibility but may not be needed
}

// ============================================================================
// Private Helper Methods
// ============================================================================

float
Painter::_Align(float coord, bool round, bool centerOffset) const
{
	if (round)
		coord = (int32)coord;

	if (centerOffset)
		coord += 0.5;

	return coord;
}

void
Painter::_Align(BPoint* point, bool centerOffset) const
{
	_Align(point, !fSubpixelPrecise, centerOffset);
}

void
Painter::_Align(BPoint* point, bool round, bool centerOffset) const
{
	point->x = _Align(point->x, round, centerOffset);
	point->y = _Align(point->y, round, centerOffset);
}

BPoint
Painter::_Align(const BPoint& point, bool centerOffset) const
{
	BPoint ret(point);
	_Align(&ret, centerOffset);
	return ret;
}

BRect
Painter::_Clipped(const BRect& rect) const
{
	if (rect.IsValid())
		return BRect(rect & fClippingRegion->Frame());
	return BRect(rect);
}

void
Painter::_UpdateDrawingMode()
{
	_PixelFormat().SetDrawingMode(fDrawingMode, fAlphaSrcMode, fAlphaFncMode);
}

void
Painter::_SetRendererColor(const rgb_color& color) const
{
	fTextRenderer.SetColor(BLRgba32(color.red, color.green, 
									color.blue, color.alpha));
}

// ============================================================================
// Path Operations - Core Blend2D Integration
// ============================================================================

BRect
Painter::_BoundingBox(const BLPath& path) const
{
	BLBox box;
	path.getBoundingBox(&box);
	return BRect(box.x0, box.y0, box.x1, box.y1);
}

BRect
Painter::_StrokePath(const BLPath& path) const
{
	return _StrokePath(path, fLineCapMode);
}

BRect
Painter::_StrokePath(const BLPath& path, cap_mode capMode) const
{
	BLPath transformedPath = path;

	// Apply transformation if needed
	if (!fIdentityTransform) {
		transformedPath.transform(fTransform.Matrix());
	}

	// Setup stroke parameters
	BLStrokeOptions strokeOptions;
	strokeOptions.width = fPenSize;
	
	strokeOptions.startCap = blend2d_stroke_cap_for(capMode);
	strokeOptions.endCap = blend2d_stroke_cap_for(capMode);
	strokeOptions.join = blend2d_stroke_join_for(fLineJoinMode);
	strokeOptions.miterLimit = fMiterLimit;

	// Get color from pattern
	rgb_color color = fPatternHandler.IsSolidHigh() 
		? fPatternHandler.HighColor()
		: fPatternHandler.LowColor();

	// Stroke the path
	BLResult result = fInternal.fBLContext.strokePath(
		transformedPath,
		BLRgba32(color.red, color.green, color.blue, color.alpha),
		strokeOptions
	);

	BLEND2D_CHECK_WARN(result);

	return _Clipped(_BoundingBox(transformedPath));
}

BRect
Painter::_FillPath(const BLPath& path) const
{
	BLPath transformedPath = path;

	if (!fIdentityTransform) {
		transformedPath.transform(fTransform.Matrix());
	}

	rgb_color color = fPatternHandler.IsSolidHigh()
		? fPatternHandler.HighColor()
		: fPatternHandler.LowColor();

	BLResult result = fInternal.fBLContext.fillPath(
		transformedPath,
		BLRgba32(color.red, color.green, color.blue, color.alpha)
	);

	BLEND2D_CHECK_WARN(result);

	return _Clipped(_BoundingBox(transformedPath));
}

// ============================================================================
// Gradient Rendering
// ============================================================================

BRect
Painter::_FillPath(const BLPath& path, const BGradient& gradient)
{
	GTRACE("Painter::_FillPath with gradient\n");

	BLPath transformedPath = path;
	if (!fIdentityTransform) {
		transformedPath.transform(fTransform.Matrix());
	}

	SolidPatternGuard _(this);

	switch (gradient.GetType()) {
		case BGradient::TYPE_LINEAR:
		{
			const BGradientLinear& linearGradient =
				static_cast<const BGradientLinear&>(gradient);
			_ApplyLinearGradient(transformedPath, linearGradient);
			break;
		}
		
		case BGradient::TYPE_RADIAL:
		{
			const BGradientRadial& radialGradient =
				static_cast<const BGradientRadial&>(gradient);
			_ApplyRadialGradient(transformedPath, radialGradient);
			break;
		}
		
		case BGradient::TYPE_RADIAL_FOCUS:
		{
			const BGradientRadialFocus& radialGradient =
				static_cast<const BGradientRadialFocus&>(gradient);
			_ApplyRadialFocusGradient(transformedPath, radialGradient);
			break;
		}
		
		case BGradient::TYPE_DIAMOND:
		{
			const BGradientDiamond& diamondGradient =
				static_cast<const BGradientDiamond&>(gradient);
			_ApplyDiamondGradient(transformedPath, diamondGradient);
			break;
		}
		
		case BGradient::TYPE_CONIC:
		{
			const BGradientConic& conicGradient =
				static_cast<const BGradientConic&>(gradient);
			_ApplyConicGradient(transformedPath, conicGradient);
			break;
		}
		
		default:
			GTRACE("Painter::_FillPath - unknown gradient type\n");
			break;
	}

	return _Clipped(_BoundingBox(transformedPath));
}

void
Painter::_ApplyLinearGradient(const BLPath& path,
	const BGradientLinear& gradient)
{
	GTRACE("Painter::_ApplyLinearGradient\n");

	BLGradient blGradient(BLLinearGradientValues(
		gradient.Start().x, gradient.Start().y,
		gradient.End().x, gradient.End().y
	));

	_MakeGradient(blGradient, gradient);

	BLResult result = fInternal.fBLContext.fillPath(path, blGradient);
	BLEND2D_CHECK_WARN(result);
}

void
Painter::_ApplyRadialGradient(const BLPath& path,
	const BGradientRadial& gradient)
{
	GTRACE("Painter::_ApplyRadialGradient\n");

	BLGradient blGradient(BLRadialGradientValues(
		gradient.Center().x, gradient.Center().y,
		gradient.Center().x, gradient.Center().y,
		gradient.Radius()
	));

	_MakeGradient(blGradient, gradient);

	BLResult result = fInternal.fBLContext.fillPath(path, blGradient);
	BLEND2D_CHECK_WARN(result);
}

void
Painter::_ApplyRadialFocusGradient(const BLPath& path,
	const BGradientRadialFocus& gradient)
{
	GTRACE("Painter::_ApplyRadialFocusGradient\n");

	BLGradient blGradient(BLRadialGradientValues(
		gradient.Center().x, gradient.Center().y,
		gradient.Focal().x, gradient.Focal().y,
		gradient.Radius()
	));

	_MakeGradient(blGradient, gradient);

	BLResult result = fInternal.fBLContext.fillPath(path, blGradient);
	BLEND2D_CHECK_WARN(result);
}

void
Painter::_ApplyDiamondGradient(const BLPath& path,
	const BGradientDiamond& gradient)
{
	GTRACE("Painter::_ApplyDiamondGradient\n");

	// Blend2D doesn't have native diamond gradient
	// Approximate with radial gradient
	BLGradient blGradient(BLRadialGradientValues(
		gradient.Center().x, gradient.Center().y,
		gradient.Center().x, gradient.Center().y,
		100.0  // Default radius
	));

	_MakeGradient(blGradient, gradient);

	BLResult result = fInternal.fBLContext.fillPath(path, blGradient);
	BLEND2D_CHECK_WARN(result);
}

void
Painter::_ApplyConicGradient(const BLPath& path,
	const BGradientConic& gradient)
{
	GTRACE("Painter::_ApplyConicGradient\n");

	BLGradient blGradient(BLConicGradientValues(
		gradient.Center().x, gradient.Center().y,
		0.0  // angle
	));

	_MakeGradient(blGradient, gradient);

	BLResult result = fInternal.fBLContext.fillPath(path, blGradient);
	BLEND2D_CHECK_WARN(result);
}

void
Painter::_MakeGradient(BLGradient& blGradient, const BGradient& gradient) const
{
	int32 stopCount = gradient.CountColorStops();
	
	for (int32 i = 0; i < stopCount; i++) {
		BGradient::ColorStop* stop = gradient.ColorStopAtFast(i);
		
		blGradient.addStop(
			stop->offset / 255.0,
			BLRgba32(stop->color.red, stop->color.green,
					 stop->color.blue, stop->color.alpha)
		);
	}
}

void
Painter::_MakeGradient(const BGradient& gradient, int32 colorCount,
	uint32* colors, int32 arrayOffset, int32 arraySize) const
{
	BGradient::ColorStop* from = gradient.ColorStopAt(0);
	if (!from)
		return;

	int32 index = (int32)floorf(colorCount * from->offset / 255 + 0.5)
		+ arrayOffset;
	
	if (index > arraySize)
		index = arraySize;

	// Fill initial colors
	if (index > 0) {
		uint8* c = (uint8*)&colors[0];
		for (int32 i = 0; i < index; i++) {
			c[0] = from->color.blue;
			c[1] = from->color.green;
			c[2] = from->color.red;
			c[3] = from->color.alpha;
			c += 4;
		}
	}

	// Interpolate between stops
	int32 stopCount = gradient.CountColorStops();
	for (int32 i = 1; i < stopCount; i++) {
		BGradient::ColorStop* to = gradient.ColorStopAtFast(i);

		int32 offset = (int32)floorf((colorCount - 1) * to->offset / 255 + 0.5);
		if (offset > colorCount - 1)
			offset = colorCount - 1;
		
		offset += arrayOffset;
		int32 dist = offset - index;
		
		if (dist >= 0) {
			int32 startIndex = max_c(index, 0);
			int32 stopIndex = min_c(offset, arraySize - 1);
			uint8* c = (uint8*)&colors[startIndex];
			
			for (int32 j = startIndex; j <= stopIndex; j++) {
				float f = (float)(offset - j) / (float)(dist + 1);
				float t = 1.0 - f;
				
				c[0] = (uint8)floorf(from->color.blue * f + to->color.blue * t + 0.5);
				c[1] = (uint8)floorf(from->color.green * f + to->color.green * t + 0.5);
				c[2] = (uint8)floorf(from->color.red * f + to->color.red * t + 0.5);
				c[3] = (uint8)floorf(from->color.alpha * f + to->color.alpha * t + 0.5);
				c += 4;
			}
		}
		
		index = offset + 1;
		from = to;
	}

	// Fill remaining colors
	if (index < arraySize) {
		int32 startIndex = max_c(index, 0);
		uint8* c = (uint8*)&colors[startIndex];
		
		for (int32 i = startIndex; i < arraySize; i++) {
			c[0] = from->color.blue;
			c[1] = from->color.green;
			c[2] = from->color.red;
			c[3] = from->color.alpha;
			c += 4;
		}
	}
}
