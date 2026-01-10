/*
 * Copyright 2009, Christian Packmann.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2005-2014, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT License.
 */


/*!	API to the Anti-Grain Geometry based "Painter" drawing backend. Manages
	rendering pipe-lines for stroke, fills, bitmap and text rendering.
*/


#include "Painter.h"

#include <new>
#include <memory>

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

#include <ShapePrivate.h>

#include <agg_bezier_arc.h>
#include <agg_bounding_rect.h>
#include <agg_conv_clip_polygon.h>
#include <agg_conv_curve.h>
#include <agg_conv_stroke.h>
#include <agg_ellipse.h>
#include <agg_image_accessors.h>
#include <agg_path_storage.h>
#include <agg_pixfmt_rgba.h>
#include <agg_rounded_rect.h>
#include <agg_span_allocator.h>
#include <agg_span_image_filter_rgba.h>
#include <agg_span_interpolator_linear.h>

#include "drawing_support.h"

#include "DrawState.h"

#include <AutoDeleter.h>
#include <View.h>

#include "AlphaMask.h"
#include "BitmapPainter.h"
#include "DrawingMode.h"
#include "GlobalSubpixelSettings.h"
#include "PatternHandler.h"
#include "RenderingBuffer.h"
#include "ServerBitmap.h"
#include "ServerFont.h"
#include "SystemPalette.h"

#include "AppServer.h"

using std::nothrow;

#undef TRACE
// #define TRACE_PAINTER
#ifdef TRACE_PAINTER
#	define TRACE(x...)		printf(x)
#else
#	define TRACE(x...)
#endif

//#define TRACE_GRADIENTS
#ifdef TRACE_GRADIENTS
#	include <OS.h>
#	define GTRACE(x...)		debug_printf(x)
#else
#	define GTRACE(x...)
#endif


#define CHECK_CLIPPING	if (!fValidClipping) return BRect(0, 0, -1, -1);
#define CHECK_CLIPPING_NO_RETURN	if (!fValidClipping) return;


#define fBuffer					fInternal.fBuffer
#define fPixelFormat			fInternal.fPixelFormat
#define fBaseRenderer			fInternal.fBaseRenderer
#define fUnpackedScanline		fInternal.fUnpackedScanline
#define fPackedScanline			fInternal.fPackedScanline
#define fRasterizer				fInternal.fRasterizer
#define fRenderer				fInternal.fRenderer
#define fRendererBin			fInternal.fRendererBin
#define fSubpixPackedScanline	fInternal.fSubpixPackedScanline
#define fSubpixUnpackedScanline	fInternal.fSubpixUnpackedScanline
#define fSubpixRasterizer		fInternal.fSubpixRasterizer
#define fSubpixRenderer			fInternal.fSubpixRenderer
#define fMaskedUnpackedScanline	fInternal.fMaskedUnpackedScanline
#define fClippedAlphaMask		fInternal.fClippedAlphaMask
#define fPath					fInternal.fPath
#define fCurve					fInternal.fCurve


static uint32 detect_simd();

uint32 gSIMDFlags = detect_simd();


static uint32
detect_simd()
{
#if __i386__
	const char* vendorNames[] = {
		"GenuineIntel",
		"AuthenticAMD",
		"CentaurHauls",
		"RiseRiseRise",
		"CyrixInstead",
		"GenuineTMx86",
		nullptr
	};

	system_info systemInfo;
	if (get_system_info(&systemInfo) != B_OK)
		return 0;

	uint32 systemSIMD = 0xffffffff;

	for (uint32 cpu = 0; cpu < systemInfo.cpu_count; cpu++) {
		cpuid_info cpuInfo;
		get_cpuid(&cpuInfo, 0, cpu);

		char vendor[13];
		memcpy(vendor, cpuInfo.eax_0.vendor_id, 12);
		vendor[12] = 0;

		bool vendorFound = false;
		for (uint32 i = 0; vendorNames[i] != nullptr; i++) {
			if (strcmp(vendor, vendorNames[i]) == 0)
				vendorFound = true;
		}

		uint32 cpuSIMD = 0;
		uint32 maxStdFunc = cpuInfo.regs.eax;
		if (vendorFound && maxStdFunc >= 1) {
			get_cpuid(&cpuInfo, 1, 0);
			uint32 edx = cpuInfo.regs.edx;
			if (edx & (1 << 23))
				cpuSIMD |= APPSERVER_SIMD_MMX;
			if (edx & (1 << 25))
				cpuSIMD |= APPSERVER_SIMD_SSE;
		} else {
			cpuSIMD = 0;
		}
		systemSIMD &= cpuSIMD;
	}
	return systemSIMD;
#else
	return 0;
#endif
}


class Painter::SolidPatternGuard {
public:
	SolidPatternGuard(Painter* painter)
		:
		fPainter(painter),
		fPattern(fPainter->Pattern())
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


Painter::Painter()
	:
	fSubpixelPrecise(false),
	fValidClipping(false),
	fAttached(false),
	fIdentityTransform(true),

	fPenSize(1.0),
	fClippingRegion(nullptr),
	fDrawingMode(B_OP_COPY),
	fAlphaSrcMode(B_PIXEL_ALPHA),
	fAlphaFncMode(B_ALPHA_OVERLAY),
	fLineCapMode(B_BUTT_CAP),
	fLineJoinMode(B_MITER_JOIN),
	fMiterLimit(B_DEFAULT_MITER_LIMIT),

	fPatternHandler(),
	fTextRenderer(fSubpixRenderer, fRenderer, fRendererBin, fUnpackedScanline,
		fSubpixUnpackedScanline, fSubpixRasterizer, fMaskedUnpackedScanline,
		fTransform),
	fInternal(fPatternHandler)
{
	fPixelFormat.SetDrawingMode(fDrawingMode, fAlphaSrcMode, fAlphaFncMode);

#if ALIASED_DRAWING
	fRasterizer.gamma(agg::gamma_threshold(kGammaThreshold));
	fSubpixRasterizer.gamma(agg::gamma_threshold(kGammaThreshold));
#endif
}


Painter::~Painter()
{
}


void
Painter::AttachToBuffer(RenderingBuffer* buffer)
{
	if (buffer && buffer->InitCheck() >= B_OK
		&& (buffer->ColorSpace() == B_RGBA32
			|| buffer->ColorSpace() == B_RGB32)) {

		fBuffer.attach((uint8*)buffer->Bits(),
			buffer->Width(), buffer->Height(), buffer->BytesPerRow());

		fAttached = true;
		fValidClipping = fClippingRegion != nullptr
			&& fClippingRegion->Frame().IsValid();

		_SetRendererColor(fPatternHandler.HighColor());
	}
}


void
Painter::DetachFromBuffer()
{
	fBuffer.attach(nullptr, 0, 0, 0);
	fAttached = false;
	fValidClipping = false;
}


BRect
Painter::Bounds() const
{
	return BRect(0, 0, fBuffer.width() - 1, fBuffer.height() - 1);
}


void
Painter::SetDrawState(const DrawState* state, int32 xOffset, int32 yOffset)
{
	SetTransform(state->CombinedTransform(), xOffset, yOffset);

	SetPenSize(state->PenSize());

	SetFont(state);

	fSubpixelPrecise = state->SubPixelPrecise();

	if (state->GetAlphaMask() != nullptr) {
		fMaskedUnpackedScanline = state->GetAlphaMask()->Scanline();
		fClippedAlphaMask = state->GetAlphaMask()->Mask();
	} else {
		fMaskedUnpackedScanline = nullptr;
		fClippedAlphaMask = nullptr;
	}

	bool updateDrawingMode
		= state->GetPattern() == fPatternHandler.GetPattern()
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

	SetFillRule(state->FillRule());

	SetHighColor(state->HighColor());
	SetLowColor(state->LowColor());

	if (updateDrawingMode)
		_UpdateDrawingMode();
}


void
Painter::ConstrainClipping(const BRegion* region)
{
	fClippingRegion = region;
	fBaseRenderer.set_clipping_region(const_cast<BRegion*>(region));
	fValidClipping = region->Frame().IsValid() && fAttached;

	if (fValidClipping) {
		clipping_rect cb = fClippingRegion->FrameInt();
		fRasterizer.clip_box(cb.left, cb.top, cb.right + 1, cb.bottom + 1);
		fSubpixRasterizer.clip_box(cb.left, cb.top, cb.right + 1, cb.bottom + 1);
	}
}


void
Painter::SetTransform(BAffineTransform transform, int32 xOffset, int32 yOffset)
{
	fIdentityTransform = transform.IsIdentity();
	if (!fIdentityTransform) {
		fTransform = agg::trans_affine_translation(-xOffset, -yOffset);
		fTransform *= agg::trans_affine(transform.sx, transform.shy,
			transform.shx, transform.sy, transform.tx, transform.ty);
		fTransform *= agg::trans_affine_translation(xOffset, yOffset);
	} else {
		fTransform.reset();
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
	agg::filling_rule_e aggFillRule = fillRule == B_EVEN_ODD
		? agg::fill_even_odd : agg::fill_non_zero;

	fRasterizer.filling_rule(aggFillRule);
	fSubpixRasterizer.filling_rule(aggFillRule);
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


bool
Painter::_CanOptimizeSolidDraw() const
{
	return fPenSize == 1.0f
		&& fIdentityTransform
		&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& fMaskedUnpackedScanline == nullptr;
}


bool
Painter::_GetPatternColor(rgb_color& color) const
{
	pattern p = *fPatternHandler.GetR5Pattern();
	if (p == B_SOLID_HIGH) {
		color = fPatternHandler.HighColor();
		return true;
	}
	if (p == B_SOLID_LOW) {
		color = fPatternHandler.LowColor();
		return true;
	}
	return false;
}


bool
Painter::_TryOptimizedRectFill(const BPoint& a, const BPoint& b) const
{
	if (!fIdentityTransform || fMaskedUnpackedScanline != nullptr)
		return false;

	rgb_color color;
	if (!_GetPatternColor(color))
		return false;

	BRect rect(a, b);

	if (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER) {
		FillRect(rect, color);
		return true;
	}

	if (fDrawingMode == B_OP_ALPHA && fAlphaFncMode == B_ALPHA_OVERLAY) {
		pattern p = *fPatternHandler.GetR5Pattern();
		if (p == B_SOLID_LOW && fAlphaSrcMode == B_CONSTANT_ALPHA)
			color.alpha = fPatternHandler.HighColor().alpha;
		_BlendRect32(rect, color);
		return true;
	}

	return false;
}


std::unique_ptr<BPoint[]>
Painter::_CreateRoundedOffsets(const BPoint* offsets, uint32 count) const
{
	if (fSubpixelPrecise)
		return nullptr;

	std::unique_ptr<BPoint[]> rounded(new (nothrow) BPoint[count]);
	if (rounded) {
		for (uint32 i = 0; i < count; i++) {
			rounded[i].x = roundf(offsets[i].x);
			rounded[i].y = roundf(offsets[i].y);
		}
	}
	return rounded;
}


void
Painter::_FinalizeGradientTransform(agg::trans_affine& matrix) const
{
	matrix *= fTransform;
	matrix.invert();
}


template<typename DrawFunc>
void
Painter::_IterateClipBoxes(DrawFunc func) const
{
	fBaseRenderer.first_clip_box();
	do {
		func(fBaseRenderer.xmin(), fBaseRenderer.ymin(),
			 fBaseRenderer.xmax(), fBaseRenderer.ymax());
	} while (fBaseRenderer.next_clip_box());
}


void
Painter::StrokeLine(BPoint a, BPoint b)
{
	CHECK_CLIPPING_NO_RETURN

	_Align(&a, false);
	_Align(&b, false);

	if (_CanOptimizeSolidDraw()) {
		rgb_color color;
		if (_GetPatternColor(color) && StraightLine(a, b, color))
			return;
	}

	fPath.remove_all();

	if (a == b) {
		if (fPenSize == 1.0 && !fSubpixelPrecise && fIdentityTransform) {
			if (fClippingRegion->Contains(a)) {
				int dotX = (int)a.x;
				int dotY = (int)a.y;
				fBaseRenderer.translate_to_base_ren(dotX, dotY);
				fPixelFormat.blend_pixel(dotX, dotY, fRenderer.color(), kMaxAlpha);
			}
		} else {
			fPath.move_to(a.x, a.y);
			fPath.line_to(a.x + 1, a.y);
			fPath.line_to(a.x + 1, a.y + 1);
			fPath.line_to(a.x, a.y + 1);

			_FillPath(fPath);
		}
	} else {
		if (!fSubpixelPrecise && fmodf(fPenSize, 2.0f) != 0.0f) {
			a.x += kPixelCenterOffset;
			a.y += kPixelCenterOffset;
			b.x += kPixelCenterOffset;
			b.y += kPixelCenterOffset;
		}

		fPath.move_to(a.x, a.y);
		fPath.line_to(b.x, b.y);

		if (!fSubpixelPrecise && fPenSize == 1.0f) {
			_StrokePath(fPath, B_SQUARE_CAP);
		} else
			_StrokePath(fPath);
	}
}


bool
Painter::StraightLine(BPoint a, BPoint b, const rgb_color& c) const
{
	if (!fValidClipping)
		return false;

	if (a.x == b.x) {
		uint8* dst = fBuffer.row_ptr(0);
		uint32 bpr = fBuffer.stride();
		int32 x = (int32)a.x;
		dst += x * 4;
		int32 y1 = (int32)min_c(a.y, b.y);
		int32 y2 = (int32)max_c(a.y, b.y);
		pixel32 color;
		color.data8[0] = c.blue;
		color.data8[1] = c.green;
		color.data8[2] = c.red;
		color.data8[3] = kMaxAlpha;

		_IterateClipBoxes([&](int32 xmin, int32 ymin, int32 xmax, int32 ymax) {
			if (xmin <= x && xmax >= x) {
				int32 i = max_c(ymin, y1);
				int32 end = min_c(ymax, y2);
				uint8* handle = dst + i * bpr;
				for (; i <= end; i++) {
					*(uint32*)handle = color.data32;
					handle += bpr;
				}
			}
		});
		return true;
	}

	if (a.y == b.y) {
		int32 y = (int32)a.y;
		if (y < 0 || y >= (int32)fBuffer.height())
			return true;

		uint8* dst = fBuffer.row_ptr(y);
		int32 x1 = (int32)min_c(a.x, b.x);
		int32 x2 = (int32)max_c(a.x, b.x);
		pixel32 color;
		color.data8[0] = c.blue;
		color.data8[1] = c.green;
		color.data8[2] = c.red;
		color.data8[3] = kMaxAlpha;

		_IterateClipBoxes([&](int32 xmin, int32 ymin, int32 xmax, int32 ymax) {
			if (ymin <= y && ymax >= y) {
				int32 i = max_c(xmin, x1);
				int32 end = min_c(xmax, x2);
				uint32* handle = (uint32*)(dst + i * 4);
				for (; i <= end; i++) {
					*handle++ = color.data32;
				}
			}
		});
		return true;
	}
	return false;
}


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
	CHECK_CLIPPING

	_Align(&pt1);
	_Align(&pt2);
	_Align(&pt3);

	fPath.remove_all();

	fPath.move_to(pt1.x, pt1.y);
	fPath.line_to(pt2.x, pt2.y);
	fPath.line_to(pt3.x, pt3.y);

	fPath.close_polygon();

	return _FillPath(fPath, gradient);
}


BRect
Painter::DrawPolygon(BPoint* p, int32 numPts, bool filled, bool closed) const
{
	CHECK_CLIPPING

	if (numPts == 0)
		return BRect(0.0, 0.0, -1.0, -1.0);

	bool centerOffset = !filled && fIdentityTransform
		&& fmodf(fPenSize, 2.0) != 0.0;

	fPath.remove_all();

	_Align(p, centerOffset);
	fPath.move_to(p->x, p->y);

	for (int32 i = 1; i < numPts; i++) {
		p++;
		_Align(p, centerOffset);
		fPath.line_to(p->x, p->y);
	}

	if (closed)
		fPath.close_polygon();

	if (filled)
		return _FillPath(fPath);

	return _StrokePath(fPath);
}


BRect
Painter::FillPolygon(BPoint* p, int32 numPts, const BGradient& gradient,
	bool closed)
{
	CHECK_CLIPPING

	if (numPts > 0) {
		fPath.remove_all();

		_Align(p);
		fPath.move_to(p->x, p->y);

		for (int32 i = 1; i < numPts; i++) {
			p++;
			_Align(p);
			fPath.line_to(p->x, p->y);
		}

		if (closed)
			fPath.close_polygon();

		return _FillPath(fPath, gradient);
	}
	return BRect(0.0, 0.0, -1.0, -1.0);
}


BRect
Painter::DrawBezier(BPoint* p, bool filled) const
{
	CHECK_CLIPPING

	fPath.remove_all();

	_Align(&(p[0]));
	_Align(&(p[1]));
	_Align(&(p[2]));
	_Align(&(p[3]));

	fPath.move_to(p[0].x, p[0].y);
	fPath.curve4(p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);

	if (filled) {
		fPath.close_polygon();
		return _FillPath(fCurve);
	}

	return _StrokePath(fCurve);
}


BRect
Painter::FillBezier(BPoint* p, const BGradient& gradient)
{
	CHECK_CLIPPING

	fPath.remove_all();

	_Align(&(p[0]));
	_Align(&(p[1]));
	_Align(&(p[2]));
	_Align(&(p[3]));

	fPath.move_to(p[0].x, p[0].y);
	fPath.curve4(p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);

	fPath.close_polygon();
	return _FillPath(fCurve, gradient);
}


BRect
Painter::DrawShape(const int32& opCount, const uint32* opList,
	const int32& ptCount, const BPoint* points, bool filled,
	const BPoint& viewToScreenOffset, float viewScale) const
{
	CHECK_CLIPPING

	_IterateShapeData(opCount, opList, ptCount, points, viewToScreenOffset,
		viewScale);

	if (filled)
		return _FillPath(fCurve);

	return _StrokePath(fCurve);
}


BRect
Painter::FillShape(const int32& opCount, const uint32* opList,
	const int32& ptCount, const BPoint* points, const BGradient& gradient,
	const BPoint& viewToScreenOffset, float viewScale)
{
	CHECK_CLIPPING

	_IterateShapeData(opCount, opList, ptCount, points, viewToScreenOffset,
		viewScale);

	return _FillPath(fCurve, gradient);
}


BRect
Painter::StrokeRect(const BRect& r) const
{
	CHECK_CLIPPING

	BPoint a(r.left, r.top);
	BPoint b(r.right, r.bottom);
	_Align(&a, false);
	_Align(&b, false);

	if (_CanOptimizeSolidDraw()) {
		rgb_color color;
		if (_GetPatternColor(color)) {
			BRect rect(a, b);
			StrokeRect(rect, color);
			return _Clipped(rect);
		}
	}

	if (fIdentityTransform && fmodf(fPenSize, 2.0) != 0.0) {
		a.x += kPixelCenterOffset;
		a.y += kPixelCenterOffset;
		b.x += kPixelCenterOffset;
		b.y += kPixelCenterOffset;
	}

	fPath.remove_all();
	fPath.move_to(a.x, a.y);
	if (a.x == b.x || a.y == b.y) {
		fPath.line_to(b.x, b.y);
	} else {
		fPath.line_to(b.x, a.y);
		fPath.line_to(b.x, b.y);
		fPath.line_to(a.x, b.y);
	}
	fPath.close_polygon();

	return _StrokePath(fPath);
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
	CHECK_CLIPPING

	BPoint a(min_c(r.left, r.right), min_c(r.top, r.bottom));
	BPoint b(max_c(r.left, r.right), max_c(r.top, r.bottom));
	_Align(&a, true, false);
	_Align(&b, true, false);

	if (_TryOptimizedRectFill(a, b))
		return _Clipped(BRect(a, b));

	b.x += 1.0;
	b.y += 1.0;

	fPath.remove_all();
	fPath.move_to(a.x, a.y);
	fPath.line_to(b.x, a.y);
	fPath.line_to(b.x, b.y);
	fPath.line_to(a.x, b.y);
	fPath.close_polygon();

	return _FillPath(fPath);
}


BRect
Painter::FillRect(const BRect& r, const BGradient& gradient)
{
	CHECK_CLIPPING

	BPoint a(min_c(r.left, r.right), min_c(r.top, r.bottom));
	BPoint b(max_c(r.left, r.right), max_c(r.top, r.bottom));
	_Align(&a, true, false);
	_Align(&b, true, false);

	if (gradient.GetType() == BGradient::TYPE_LINEAR
		&& (fDrawingMode == B_OP_COPY || fDrawingMode == B_OP_OVER)
		&& fMaskedUnpackedScanline == nullptr && fIdentityTransform) {
		const BGradientLinear* linearGradient
			= dynamic_cast<const BGradientLinear*>(&gradient);
		if (linearGradient->Start().x == linearGradient->End().x) {
			BRect rect(a, b);
			FillRectVerticalGradient(rect, *linearGradient);
			return _Clipped(rect);
		}
	}

	b.x += 1.0;
	b.y += 1.0;

	fPath.remove_all();
	fPath.move_to(a.x, a.y);
	fPath.line_to(b.x, a.y);
	fPath.line_to(b.x, b.y);
	fPath.line_to(a.x, b.y);
	fPath.close_polygon();

	return _FillPath(fPath, gradient);
}


void
Painter::FillRect(const BRect& r, const rgb_color& c) const
{
	if (!fValidClipping)
		return;

	uint8* dst = fBuffer.row_ptr(0);
	uint32 bpr = fBuffer.stride();
	int32 left = (int32)r.left;
	int32 top = (int32)r.top;
	int32 right = (int32)r.right;
	int32 bottom = (int32)r.bottom;

	pixel32 color;
	color.data8[0] = c.blue;
	color.data8[1] = c.green;
	color.data8[2] = c.red;
	color.data8[3] = c.alpha;

	_IterateClipBoxes([&](int32 xmin, int32 ymin, int32 xmax, int32 ymax) {
		int32 x1 = max_c(xmin, left);
		int32 x2 = min_c(xmax, right);
		if (x1 <= x2) {
			int32 y1 = max_c(ymin, top);
			int32 y2 = min_c(ymax, bottom);
			uint8* offset = dst + x1 * 4;
			for (; y1 <= y2; y1++) {
				gfxset32(offset + y1 * bpr, color.data32, (x2 - x1 + 1) * 4);
			}
		}
	});
}


void
Painter::FillRectVerticalGradient(BRect r,
	const BGradientLinear& gradient) const
{
	if (!fValidClipping)
		return;

	r = r & fClippingRegion->Frame();

	int32 gradientArraySize = r.IntegerHeight() + 1;
	uint32 gradientArray[gradientArraySize];
	int32 gradientTop = (int32)gradient.Start().y;
	int32 gradientBottom = (int32)gradient.End().y;
	
	bool upsideDown = gradientTop > gradientBottom;
	if (upsideDown) {
		int32 temp = gradientTop;
		gradientTop = gradientBottom;
		gradientBottom = temp;
	}
	
	int32 colorCount = gradientBottom - gradientTop + 1;
	if (colorCount < 0)
		return;

	_MakeGradient(gradient, colorCount, gradientArray,
		gradientTop - (int32)r.top, gradientArraySize);

	uint8* dst = fBuffer.row_ptr(0);
	uint32 bpr = fBuffer.stride();
	int32 left = (int32)r.left;
	int32 top = (int32)r.top;
	int32 right = (int32)r.right;
	int32 bottom = (int32)r.bottom;
	
	_IterateClipBoxes([&](int32 xmin, int32 ymin, int32 xmax, int32 ymax) {
		int32 x1 = max_c(xmin, left);
		int32 x2 = min_c(xmax, right);
		if (x1 <= x2) {
			int32 y1 = max_c(ymin, top);
			int32 y2 = min_c(ymax, bottom);
			uint8* offset = dst + x1 * 4;
			for (; y1 <= y2; y1++) {
				int32 gradientIndex = upsideDown 
					? (gradientArraySize - 1 - (y1 - top))
					: (y1 - top);
				if (gradientIndex >= 0 && gradientIndex < gradientArraySize) {
					gfxset32(offset + y1 * bpr, gradientArray[gradientIndex],
						(x2 - x1 + 1) * 4);
				}
			}
		}
	});
}


void
Painter::FillRectNoClipping(const clipping_rect& r, const rgb_color& c) const
{
	int32 y = (int32)r.top;

	uint8* dst = fBuffer.row_ptr(y) + r.left * 4;
	uint32 bpr = fBuffer.stride();
	int32 bytes = (r.right - r.left + 1) * 4;

	pixel32 color;
	color.data8[0] = c.blue;
	color.data8[1] = c.green;
	color.data8[2] = c.red;
	color.data8[3] = c.alpha;

	for (; y <= r.bottom; y++) {
		gfxset32(dst, color.data32, bytes);
		dst += bpr;
	}
}


BRect
Painter::StrokeRoundRect(const BRect& r, float xRadius, float yRadius) const
{
	CHECK_CLIPPING

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	bool centerOffset = fmodf(fPenSize, 2.0) != 0.0;
	_Align(&lt, centerOffset);
	_Align(&rb, centerOffset);

	agg::rounded_rect rect;
	rect.rect(lt.x, lt.y, rb.x, rb.y);
	rect.radius(xRadius, yRadius);

	return _StrokePath(rect);
}


BRect
Painter::FillRoundRect(const BRect& r, float xRadius, float yRadius) const
{
	CHECK_CLIPPING

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	_Align(&lt, false);
	_Align(&rb, false);

	rb.x += 1.0;
	rb.y += 1.0;

	agg::rounded_rect rect;
	rect.rect(lt.x, lt.y, rb.x, rb.y);
	rect.radius(xRadius, yRadius);

	return _FillPath(rect);
}


BRect
Painter::FillRoundRect(const BRect& r, float xRadius, float yRadius,
	const BGradient& gradient)
{
	CHECK_CLIPPING

	BPoint lt(r.left, r.top);
	BPoint rb(r.right, r.bottom);
	_Align(&lt, false);
	_Align(&rb, false);

	rb.x += 1.0;
	rb.y += 1.0;

	agg::rounded_rect rect;
	rect.rect(lt.x, lt.y, rb.x, rb.y);
	rect.radius(xRadius, yRadius);

	return _FillPath(rect, gradient);
}


void
Painter::AlignEllipseRect(BRect* rect, bool filled) const
{
	if (!fSubpixelPrecise) {
		align_rect_to_pixels(rect);
		rect->right++;
		rect->bottom++;
		if (!filled && fmodf(fPenSize, 2.0) != 0.0) {
			rect->InsetBy(kPixelCenterOffset, kPixelCenterOffset);
		}
	}
}


BRect
Painter::DrawEllipse(BRect r, bool fill) const
{
	CHECK_CLIPPING

	AlignEllipseRect(&r, fill);

	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius, r.top + yRadius);

	int32 divisions = (int32)((xRadius + yRadius + 2 * fPenSize) * M_PI / 2);
	if (divisions < kDefaultEllipseDivisions)
		divisions = kDefaultEllipseDivisions;
	if (divisions > kMaxEllipseDivisions)
		divisions = kMaxEllipseDivisions;

	agg::ellipse path(center.x, center.y, xRadius, yRadius, divisions);

	if (fill)
		return _FillPath(path);
	else
		return _StrokePath(path);
}


BRect
Painter::FillEllipse(BRect r, const BGradient& gradient)
{
	CHECK_CLIPPING

	AlignEllipseRect(&r, true);

	float xRadius = r.Width() / 2.0;
	float yRadius = r.Height() / 2.0;
	BPoint center(r.left + xRadius, r.top + yRadius);

	int32 divisions = (int32)((xRadius + yRadius + 2 * fPenSize) * M_PI / 2);
	if (divisions < kDefaultEllipseDivisions)
		divisions = kDefaultEllipseDivisions;
	if (divisions > kMaxEllipseDivisions)
		divisions = kMaxEllipseDivisions;

	agg::ellipse path(center.x, center.y, xRadius, yRadius, divisions);

	return _FillPath(path, gradient);
}


BRect
Painter::StrokeArc(BPoint center, float xRadius, float yRadius, float angle,
	float span) const
{
	CHECK_CLIPPING

	_Align(&center);

	double angleRad = (angle * M_PI) / 180.0;
	double spanRad = (span * M_PI) / 180.0;
	agg::bezier_arc arc(center.x, center.y, xRadius, yRadius, -angleRad,
		-spanRad);

	agg::conv_curve<agg::bezier_arc> path(arc);
	path.approximation_scale(kDefaultApproximationScale);

	return _StrokePath(path);
}


BRect
Painter::FillArc(BPoint center, float xRadius, float yRadius, float angle,
	float span) const
{
	CHECK_CLIPPING

	_Align(&center);

	double angleRad = (angle * M_PI) / 180.0;
	double spanRad = (span * M_PI) / 180.0;
	agg::bezier_arc arc(center.x, center.y, xRadius, yRadius, -angleRad,
		-spanRad);

	agg::conv_curve<agg::bezier_arc> segmentedArc(arc);

	fPath.remove_all();

	fPath.move_to(center.x, center.y);

	segmentedArc.rewind(0);
	double x;
	double y;
	unsigned cmd = segmentedArc.vertex(&x, &y);
	while (!agg::is_stop(cmd)) {
		fPath.line_to(x, y);
		cmd = segmentedArc.vertex(&x, &y);
	}

	fPath.close_polygon();

	return _FillPath(fPath);
}


BRect
Painter::FillArc(BPoint center, float xRadius, float yRadius, float angle,
	float span, const BGradient& gradient)
{
	CHECK_CLIPPING

	_Align(&center);

	double angleRad = (angle * M_PI) / 180.0;
	double spanRad = (span * M_PI) / 180.0;
	agg::bezier_arc arc(center.x, center.y, xRadius, yRadius, -angleRad,
		-spanRad);

	agg::conv_curve<agg::bezier_arc> segmentedArc(arc);

	fPath.remove_all();

	fPath.move_to(center.x, center.y);

	segmentedArc.rewind(0);
	double x;
	double y;
	unsigned cmd = segmentedArc.vertex(&x, &y);
	while (!agg::is_stop(cmd)) {
		fPath.line_to(x, y);
		cmd = segmentedArc.vertex(&x, &y);
	}

	fPath.close_polygon();

	return _FillPath(fPath, gradient);
}


BRect
Painter::DrawString(const char* utf8String, uint32 length, BPoint baseLine,
	const escapement_delta* delta, FontCacheReference* cacheReference)
{
	CHECK_CLIPPING

	if (!fSubpixelPrecise) {
		baseLine.x = roundf(baseLine.x);
		baseLine.y = roundf(baseLine.y);
	}

	BRect bounds;

	SolidPatternGuard _(this);

	bounds = fTextRenderer.RenderString(utf8String, length,
		baseLine, fClippingRegion->Frame(), false, nullptr, delta,
		cacheReference);

	return _Clipped(bounds);
}


BRect
Painter::DrawString(const char* utf8String, uint32 length,
	const BPoint* offsets, FontCacheReference* cacheReference)
{
	CHECK_CLIPPING

	SolidPatternGuard _(this);

	auto rounded = _CreateRoundedOffsets(offsets, length);
	const BPoint* effectiveOffsets = rounded ? rounded.get() : offsets;

	BRect bounds = fTextRenderer.RenderString(utf8String, length,
		effectiveOffsets, fClippingRegion->Frame(), false, nullptr,
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

	auto rounded = _CreateRoundedOffsets(offsets, length);
	const BPoint* effectiveOffsets = rounded ? rounded.get() : offsets;

	return fTextRenderer.RenderString(utf8String, length,
		effectiveOffsets, dummy, true, penLocation, cacheReference);
}


float
Painter::StringWidth(const char* utf8String, uint32 length,
	const escapement_delta* delta)
{
	return Font().StringWidth(utf8String, length, delta);
}


BRect
Painter::DrawBitmap(const ServerBitmap* bitmap, BRect bitmapRect,
	BRect viewRect, uint32 options) const
{
	CHECK_CLIPPING

	BRect touched = TransformAlignAndClipRect(viewRect);

	if (touched.IsValid()) {
		BitmapPainter bitmapPainter(this, bitmap, options);
		bitmapPainter.Draw(bitmapRect, viewRect);
	}

	return touched;
}


BRect
Painter::FillRegion(const BRegion* region) const
{
	CHECK_CLIPPING

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
	CHECK_CLIPPING

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
	CHECK_CLIPPING

	BRegion region(r);
	region.IntersectWith(fClippingRegion);

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++)
		_InvertRect32(region.RectAt(i));

	return _Clipped(r);
}


void
Painter::SetRendererOffset(int32 offsetX, int32 offsetY)
{
	fBaseRenderer.set_offset(offsetX, offsetY);
}


inline float
Painter::_Align(float coord, bool round, bool centerOffset) const
{
	if (round)
		coord = (int32)coord;

	if (centerOffset)
		coord += kPixelCenterOffset;

	return coord;
}


inline void
Painter::_Align(BPoint* point, bool centerOffset) const
{
	_Align(point, !fSubpixelPrecise, centerOffset);
}


inline void
Painter::_Align(BPoint* point, bool round, bool centerOffset) const
{
	point->x = _Align(point->x, round, centerOffset);
	point->y = _Align(point->y, round, centerOffset);
}


inline BPoint
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
	fPixelFormat.SetDrawingMode(fDrawingMode, fAlphaSrcMode, fAlphaFncMode);
}


void
Painter::_SetRendererColor(const rgb_color& color) const
{
	fRenderer.color(agg::rgba(color.red / 255.0, color.green / 255.0,
		color.blue / 255.0, color.alpha / 255.0));
	fSubpixRenderer.color(agg::rgba(color.red / 255.0, color.green / 255.0,
		color.blue / 255.0, color.alpha / 255.0));
}


inline BRect
Painter::_DrawTriangle(BPoint pt1, BPoint pt2, BPoint pt3, bool fill) const
{
	CHECK_CLIPPING

	_Align(&pt1);
	_Align(&pt2);
	_Align(&pt3);

	fPath.remove_all();

	fPath.move_to(pt1.x, pt1.y);
	fPath.line_to(pt2.x, pt2.y);
	fPath.line_to(pt3.x, pt3.y);

	fPath.close_polygon();

	if (fill)
		return _FillPath(fPath);

	return _StrokePath(fPath);
}


void
Painter::_IterateShapeData(const int32& opCount, const uint32* opList,
	const int32& ptCount, const BPoint* points,
	const BPoint& viewToScreenOffset, float viewScale) const
{
	fPath.remove_all();
	for (int32 i = 0; i < opCount; i++) {
		uint32 op = opList[i] & 0xFF000000;
		if ((op & OP_MOVETO) != 0) {
			fPath.move_to(
				points->x * viewScale + viewToScreenOffset.x,
				points->y * viewScale + viewToScreenOffset.y);
			points++;
		}

		if ((op & OP_LINETO) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			while (count--) {
				fPath.line_to(
					points->x * viewScale + viewToScreenOffset.x,
					points->y * viewScale + viewToScreenOffset.y);
				points++;
			}
		}

		if ((op & OP_BEZIERTO) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			while (count) {
				fPath.curve4(
					points[0].x * viewScale + viewToScreenOffset.x,
					points[0].y * viewScale + viewToScreenOffset.y,
					points[1].x * viewScale + viewToScreenOffset.x,
					points[1].y * viewScale + viewToScreenOffset.y,
					points[2].x * viewScale + viewToScreenOffset.x,
					points[2].y * viewScale + viewToScreenOffset.y);
				points += 3;
				count -= 3;
			}
		}

		if ((op & OP_LARGE_ARC_TO_CW) != 0 || (op & OP_LARGE_ARC_TO_CCW) != 0
			|| (op & OP_SMALL_ARC_TO_CW) != 0
			|| (op & OP_SMALL_ARC_TO_CCW) != 0) {
			int32 count = opList[i] & 0x00FFFFFF;
			while (count > 0) {
				fPath.arc_to(
					points[0].x * viewScale,
					points[0].y * viewScale,
					points[1].x,
					op & (OP_LARGE_ARC_TO_CW | OP_LARGE_ARC_TO_CCW),
					op & (OP_SMALL_ARC_TO_CW | OP_LARGE_ARC_TO_CW),
					points[2].x * viewScale + viewToScreenOffset.x,
					points[2].y * viewScale + viewToScreenOffset.y);
				points += 3;
				count -= 3;
			}
		}

		if ((op & OP_CLOSE) != 0)
			fPath.close_polygon();
	}
}


void
Painter::_InvertRect32(BRect r) const
{
	int32 width = r.IntegerWidth() + 1;
	for (int32 y = (int32)r.top; y <= (int32)r.bottom; y++) {
		uint8* dst = fBuffer.row_ptr(y);
		dst += (int32)r.left * 4;
		for (int32 i = 0; i < width; i++) {
			dst[0] = kMaxAlpha - dst[0];
			dst[1] = kMaxAlpha - dst[1];
			dst[2] = kMaxAlpha - dst[2];
			dst += 4;
		}
	}
}


void
Painter::_BlendRect32(const BRect& r, const rgb_color& c) const
{
	if (!fValidClipping)
		return;

	uint8* dst = fBuffer.row_ptr(0);
	uint32 bpr = fBuffer.stride();

	int32 left = (int32)r.left;
	int32 top = (int32)r.top;
	int32 right = (int32)r.right;
	int32 bottom = (int32)r.bottom;

	_IterateClipBoxes([&](int32 xmin, int32 ymin, int32 xmax, int32 ymax) {
		int32 x1 = max_c(xmin, left);
		int32 x2 = min_c(xmax, right);
		if (x1 <= x2) {
			int32 y1 = max_c(ymin, top);
			int32 y2 = min_c(ymax, bottom);

			uint8* offset = dst + x1 * 4 + y1 * bpr;
			for (; y1 <= y2; y1++) {
				blend_line32(offset, x2 - x1 + 1, c.red, c.green, c.blue,
					c.alpha);
				offset += bpr;
			}
		}
	});
}


template<class VertexSource>
BRect
Painter::_BoundingBox(VertexSource& path) const
{
	double left = 0.0;
	double top = 0.0;
	double right = -1.0;
	double bottom = -1.0;
	uint32 pathID[1];
	pathID[0] = 0;
	agg::bounding_rect(path, pathID, 0, 1, &left, &top, &right, &bottom);
	return BRect(left, top, right, bottom);
}


inline agg::line_cap_e
agg_line_cap_mode_for(cap_mode mode)
{
	switch (mode) {
		case B_BUTT_CAP:
			return agg::butt_cap;
		case B_SQUARE_CAP:
			return agg::square_cap;
		case B_ROUND_CAP:
			return agg::round_cap;
	}
	return agg::butt_cap;
}


inline agg::line_join_e
agg_line_join_mode_for(join_mode mode)
{
	switch (mode) {
		case B_MITER_JOIN:
			return agg::miter_join;
		case B_ROUND_JOIN:
			return agg::round_join;
		case B_BEVEL_JOIN:
		case B_BUTT_JOIN:
		case B_SQUARE_JOIN:
			return agg::bevel_join;
	}
	return agg::miter_join;
}


template<class VertexSource>
BRect
Painter::_StrokePath(VertexSource& path) const
{
	return _StrokePath(path, fLineCapMode);
}


template<class VertexSource>
BRect
Painter::_StrokePath(VertexSource& path, cap_mode capMode) const
{
	agg::conv_stroke<VertexSource> stroke(path);
	stroke.width(fPenSize);

	stroke.line_cap(agg_line_cap_mode_for(capMode));
	stroke.line_join(agg_line_join_mode_for(fLineJoinMode));
	stroke.miter_limit(fMiterLimit);

	if (fIdentityTransform)
		return _RasterizePath(stroke);

	stroke.approximation_scale(fTransform.scale());

	agg::conv_transform<agg::conv_stroke<VertexSource> > transformedStroke(
		stroke, fTransform);
	return _RasterizePath(transformedStroke);
}


template<class VertexSource>
BRect
Painter::_FillPath(VertexSource& path) const
{
	if (fIdentityTransform)
		return _RasterizePath(path);

	agg::conv_transform<VertexSource> transformedPath(path, fTransform);
	return _RasterizePath(transformedPath);
}


template<class VertexSource>
BRect
Painter::_RasterizePath(VertexSource& path) const
{
	if (fMaskedUnpackedScanline != nullptr) {
		fRasterizer.reset();
		fRasterizer.add_path(path);
		agg::render_scanlines(fRasterizer, *fMaskedUnpackedScanline,
			fRenderer);
	} else if (gSubpixelAntialiasing) {
		fSubpixRasterizer.reset();
		fSubpixRasterizer.add_path(path);
		agg::render_scanlines(fSubpixRasterizer,
			fSubpixPackedScanline, fSubpixRenderer);
	} else {
		fRasterizer.reset();
		fRasterizer.add_path(path);
		agg::render_scanlines(fRasterizer, fPackedScanline, fRenderer);
	}

	return _Clipped(_BoundingBox(path));
}


template<class VertexSource>
BRect
Painter::_FillPath(VertexSource& path, const BGradient& gradient)
{
	if (fIdentityTransform)
		return _RasterizePath(path, gradient);

	agg::conv_transform<VertexSource> transformedPath(path, fTransform);
	return _RasterizePath(transformedPath, gradient);
}


template<class VertexSource>
BRect
Painter::_RasterizePath(VertexSource& path, const BGradient& gradient)
{
	GTRACE("Painter::_RasterizePath\n");

	agg::trans_affine gradientTransform;

	switch (gradient.GetType()) {
		case BGradient::TYPE_LINEAR:
		{
			GTRACE(("Painter::_FillPath> type == TYPE_LINEAR\n"));
			const BGradientLinear& linearGradient
				= (const BGradientLinear&) gradient;
			agg::gradient_x gradientFunction;
			_CalcLinearGradientTransform(linearGradient.Start(),
				linearGradient.End(), gradientTransform);
			_RasterizePath(path, gradient, gradientFunction, gradientTransform);
			break;
		}
		case BGradient::TYPE_RADIAL:
		{
			GTRACE(("Painter::_FillPathGradient> type == TYPE_RADIAL\n"));
			const BGradientRadial& radialGradient
				= (const BGradientRadial&) gradient;
			agg::gradient_radial gradientFunction;
			_CalcRadialGradientTransform(radialGradient.Center(),
				gradientTransform);
			_RasterizePath(path, gradient, gradientFunction, gradientTransform,
				radialGradient.Radius());
			break;
		}
		case BGradient::TYPE_RADIAL_FOCUS:
		{
			GTRACE(("Painter::_FillPathGradient> type == TYPE_RADIAL_FOCUS\n"));
			const BGradientRadialFocus& radialGradient
				= (const BGradientRadialFocus&) gradient;
			agg::gradient_radial_focus gradientFunction;
			_CalcRadialGradientTransform(radialGradient.Center(),
				gradientTransform);
			_RasterizePath(path, gradient, gradientFunction, gradientTransform,
				radialGradient.Radius());
			break;
		}
		case BGradient::TYPE_DIAMOND:
		{
			GTRACE(("Painter::_FillPathGradient> type == TYPE_DIAMOND\n"));
			const BGradientDiamond& diamontGradient
				= (const BGradientDiamond&) gradient;
			agg::gradient_diamond gradientFunction;
			_CalcRadialGradientTransform(diamontGradient.Center(),
				gradientTransform);
			_RasterizePath(path, gradient, gradientFunction, gradientTransform);
			break;
		}
		case BGradient::TYPE_CONIC:
		{
			GTRACE(("Painter::_FillPathGradient> type == TYPE_CONIC\n"));
			const BGradientConic& conicGradient
				= (const BGradientConic&) gradient;
			agg::gradient_conic gradientFunction;
			_CalcRadialGradientTransform(conicGradient.Center(),
				gradientTransform);
			_RasterizePath(path, gradient, gradientFunction, gradientTransform);
			break;
		}

		default:
		case BGradient::TYPE_NONE:
			GTRACE(("Painter::_FillPathGradient> type == TYPE_NONE/unkown\n"));
			break;
	}

	return _Clipped(_BoundingBox(path));
}


void
Painter::_CalcLinearGradientTransform(BPoint startPoint, BPoint endPoint,
	agg::trans_affine& matrix, float gradient_d2) const
{
	float dx = endPoint.x - startPoint.x;
	float dy = endPoint.y - startPoint.y;

	matrix.reset();
	matrix *= agg::trans_affine_scaling(sqrt(dx * dx + dy * dy) / gradient_d2);
	matrix *= agg::trans_affine_rotation(atan2(dy, dx));
	matrix *= agg::trans_affine_translation(startPoint.x, startPoint.y);
	_FinalizeGradientTransform(matrix);
}


void
Painter::_CalcRadialGradientTransform(BPoint center,
	agg::trans_affine& matrix, float gradient_d2) const
{
	matrix.reset();
	matrix *= agg::trans_affine_translation(center.x, center.y);
	_FinalizeGradientTransform(matrix);
}


void
Painter::_MakeGradient(const BGradient& gradient, int32 colorCount,
	uint32* colors, int32 arrayOffset, int32 arraySize) const
{
	BGradient::ColorStop* from = gradient.ColorStopAt(0);

	if (!from)
		return;

	float normalizedOffset = from->offset / kGradientOffsetScale;
	int32 index = (int32)floorf(colorCount * normalizedOffset + 0.5)
		+ arrayOffset;
	if (index > arraySize)
		index = arraySize;
		
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

	int32 stopCount = gradient.CountColorStops();
	for (int32 i = 1; i < stopCount; i++) {
		BGradient::ColorStop* to = gradient.ColorStopAtFast(i);

		float normalizedToOffset = to->offset / kGradientOffsetScale;
		int32 offset = (int32)floorf((colorCount - 1) * normalizedToOffset + 0.5);
		if (offset > colorCount - 1)
			offset = colorCount - 1;
		offset += arrayOffset;
		int32 dist = offset - index;
		if (dist >= 0) {
			int32 startIndex = max_c(index, 0);
			int32 stopIndex = min_c(offset, arraySize - 1);
			uint8* c = (uint8*)&colors[startIndex];
			for (int32 i = startIndex; i <= stopIndex; i++) {
				float f = (float)(offset - i) / (float)(dist + 1);
				float t = 1.0 - f;
				c[0] = (uint8)floorf(from->color.blue * f
					+ to->color.blue * t + 0.5);
				c[1] = (uint8)floorf(from->color.green * f
					+ to->color.green * t + 0.5);
				c[2] = (uint8)floorf(from->color.red * f
					+ to->color.red * t + 0.5);
				c[3] = (uint8)floorf(from->color.alpha * f
					+ to->color.alpha * t + 0.5);
				c += 4;
			}
		}
		index = offset + 1;
		from = to;
	}
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


template<class Array>
void
Painter::_MakeGradient(Array& array, const BGradient& gradient) const
{
	for (int i = 0; i < gradient.CountColorStops() - 1; i++) {
		BGradient::ColorStop* from = gradient.ColorStopAtFast(i);
		BGradient::ColorStop* to = gradient.ColorStopAtFast(i + 1);
		agg::rgba8 fromColor(from->color.red, from->color.green,
							 from->color.blue, from->color.alpha);
		agg::rgba8 toColor(to->color.red, to->color.green,
						   to->color.blue, to->color.alpha);
		
		int fromOffset = (int)from->offset;
		int toOffset = (int)to->offset;
		
		float dist = toOffset - fromOffset;
		GTRACE("Painter::_MakeGradient> fromColor(%d, %d, %d, %d) offset = %d (normalized)\n",
			   fromColor.r, fromColor.g, fromColor.b, fromColor.a, fromOffset);
		GTRACE("Painter::_MakeGradient> toColor(%d, %d, %d %d) offset = %d (normalized)\n",
			   toColor.r, toColor.g, toColor.b, toColor.a, toOffset);
		GTRACE("Painter::_MakeGradient> dist = %f\n", dist);
		
		if (dist > 0) {
			for (int j = fromOffset; j <= toOffset; j++) {
				float f = (float)(toOffset - j) / (float)(dist + 1);
				array[j] = toColor.gradient(fromColor, f);
				GTRACE("Painter::_MakeGradient> array[%d](%d, %d, %d, %d)\n",
					   j, array[j].r, array[j].g, array[j].b, array[j].a);
			}
		}
	}
}


template<class VertexSource, typename GradientFunction>
void
Painter::_RasterizePath(VertexSource& path, const BGradient& gradient,
	GradientFunction function, agg::trans_affine& gradientTransform,
	int gradientStop)
{
	GTRACE("Painter::_RasterizePath\n");

	typedef agg::span_interpolator_linear<> interpolator_type;
	typedef agg::pod_auto_array<agg::rgba8, 256> color_array_type;
	typedef agg::span_allocator<agg::rgba8> span_allocator_type;
	typedef agg::span_gradient<agg::rgba8, interpolator_type,
				GradientFunction, color_array_type> span_gradient_type;
	typedef agg::renderer_scanline_aa<renderer_base, span_allocator_type,
				span_gradient_type> renderer_gradient_type;

	SolidPatternGuard _(this);

	interpolator_type spanInterpolator(gradientTransform);
	span_allocator_type spanAllocator;
	color_array_type colorArray;

	_MakeGradient(colorArray, gradient);

	span_gradient_type spanGradient(spanInterpolator, function, colorArray,
		0, gradientStop);

	renderer_gradient_type gradientRenderer(fBaseRenderer, spanAllocator,
		spanGradient);

	fRasterizer.reset();
	fRasterizer.add_path(path);
	if (fMaskedUnpackedScanline == nullptr)
		agg::render_scanlines(fRasterizer, fUnpackedScanline, gradientRenderer);
	else {
		agg::render_scanlines(fRasterizer, *fMaskedUnpackedScanline,
			gradientRenderer);
	}
}
