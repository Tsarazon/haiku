/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_CANVAS_HPP
#define _KOSM_CANVAS_HPP

#include <KosmColor.hpp>
#include <KosmGeometry.hpp>
#include <SupportDefs.h>

class KosmSurface;
class KosmPath;
class KosmGradient;
class KosmImage;
class KosmFont;
struct KosmTextStyle;
class RenderBackend;


// ============================================================================
// Stroke style
// ============================================================================

enum kosm_line_cap {
	KOSM_LINE_CAP_BUTT = 0,
	KOSM_LINE_CAP_ROUND,
	KOSM_LINE_CAP_SQUARE
};

enum kosm_line_join {
	KOSM_LINE_JOIN_MITER = 0,
	KOSM_LINE_JOIN_ROUND,
	KOSM_LINE_JOIN_BEVEL
};

struct KosmStrokeStyle {
	float			width;
	kosm_line_cap	cap;
	kosm_line_join	join;
	float			miterLimit;
	float*			dashPattern;
	uint32			dashCount;
	float			dashOffset;

	KosmStrokeStyle()
		:
		width(1.0f),
		cap(KOSM_LINE_CAP_BUTT),
		join(KOSM_LINE_JOIN_MITER),
		miterLimit(4.0f),
		dashPattern(nullptr),
		dashCount(0),
		dashOffset(0)
	{
	}

	KosmStrokeStyle(float w)
		:
		width(w),
		cap(KOSM_LINE_CAP_BUTT),
		join(KOSM_LINE_JOIN_MITER),
		miterLimit(4.0f),
		dashPattern(nullptr),
		dashCount(0),
		dashOffset(0)
	{
	}
};


// ============================================================================
// Fill rule
// ============================================================================

enum kosm_fill_rule {
	KOSM_FILL_RULE_NON_ZERO = 0,
	KOSM_FILL_RULE_EVEN_ODD
};


// ============================================================================
// Mask method
// ============================================================================

enum kosm_mask_method {
	KOSM_MASK_ALPHA = 0,		// Alpha masking (default)
	KOSM_MASK_INV_ALPHA,		// Inverted alpha
	KOSM_MASK_LUMA,				// Luminance-based
	KOSM_MASK_INV_LUMA,			// Inverted luminance
	KOSM_MASK_ADD,				// Additive
	KOSM_MASK_SUBTRACT,			// Subtractive
	KOSM_MASK_INTERSECT,		// Intersection
	KOSM_MASK_DIFFERENCE,		// Difference
	KOSM_MASK_LIGHTEN,			// Lighten
	KOSM_MASK_DARKEN			// Darken
};


// ============================================================================
// Blend mode
// ============================================================================

enum kosm_blend_mode {
	KOSM_BLEND_NORMAL = 0,
	KOSM_BLEND_MULTIPLY,
	KOSM_BLEND_SCREEN,
	KOSM_BLEND_OVERLAY,
	KOSM_BLEND_DARKEN,
	KOSM_BLEND_LIGHTEN,
	KOSM_BLEND_COLOR_DODGE,
	KOSM_BLEND_COLOR_BURN,
	KOSM_BLEND_HARD_LIGHT,
	KOSM_BLEND_SOFT_LIGHT,
	KOSM_BLEND_DIFFERENCE,
	KOSM_BLEND_EXCLUSION,
	KOSM_BLEND_HUE,
	KOSM_BLEND_SATURATION,
	KOSM_BLEND_COLOR,
	KOSM_BLEND_LUMINOSITY,
	KOSM_BLEND_ADD
};


// ============================================================================
// Shadow
// ============================================================================

struct KosmShadow {
	KosmColor		color;
	float			offsetX;
	float			offsetY;
	float			blur;

	KosmShadow()
		:
		color(KosmColor(0, 0, 0, 0.5f)),
		offsetX(0),
		offsetY(2),
		blur(4)
	{
	}

	KosmShadow(const KosmColor& c, float ox, float oy, float b)
		:
		color(c),
		offsetX(ox),
		offsetY(oy),
		blur(b)
	{
	}

	bool IsEnabled() const {
		return color.a > 0 && blur > 0;
	}
};


// ============================================================================
// Canvas
// ============================================================================

class KosmCanvas {
public:
	// Lifecycle - call once per process
	static	status_t		Initialize(uint32 threads = 0);
	static	void			Terminate();

	// Construction
							KosmCanvas(KosmSurface* surface);
							~KosmCanvas();

	// Target
			KosmSurface*	Surface() const;
			uint32			Width() const;
			uint32			Height() const;

	// ========================================================================
	// Clear
	// ========================================================================

			void			Clear();
			void			Clear(const KosmColor& color);

	// ========================================================================
	// Fill primitives with solid color
	// ========================================================================

			void			FillRect(const KosmRect& rect,
								const KosmColor& color);
			void			FillRoundRect(const KosmRect& rect,
								float radius, const KosmColor& color);
			void			FillRoundRect(const KosmRect& rect,
								float rx, float ry, const KosmColor& color);
			void			FillCircle(const KosmPoint& center,
								float radius, const KosmColor& color);
			void			FillEllipse(const KosmPoint& center,
								float rx, float ry, const KosmColor& color);
			void			FillPath(const KosmPath& path,
								const KosmColor& color);

	// ========================================================================
	// Fill primitives with gradient
	// ========================================================================

			void			FillRect(const KosmRect& rect,
								const KosmGradient& gradient);
			void			FillRoundRect(const KosmRect& rect,
								float radius, const KosmGradient& gradient);
			void			FillRoundRect(const KosmRect& rect,
								float rx, float ry,
								const KosmGradient& gradient);
			void			FillCircle(const KosmPoint& center,
								float radius, const KosmGradient& gradient);
			void			FillEllipse(const KosmPoint& center,
								float rx, float ry,
								const KosmGradient& gradient);
			void			FillPath(const KosmPath& path,
								const KosmGradient& gradient);

	// ========================================================================
	// Stroke primitives
	// ========================================================================

			void			StrokeRect(const KosmRect& rect,
								const KosmColor& color,
								const KosmStrokeStyle& style = KosmStrokeStyle());
			void			StrokeRoundRect(const KosmRect& rect,
								float radius, const KosmColor& color,
								const KosmStrokeStyle& style = KosmStrokeStyle());
			void			StrokeRoundRect(const KosmRect& rect,
								float rx, float ry, const KosmColor& color,
								const KosmStrokeStyle& style = KosmStrokeStyle());
			void			StrokeCircle(const KosmPoint& center,
								float radius, const KosmColor& color,
								const KosmStrokeStyle& style = KosmStrokeStyle());
			void			StrokeEllipse(const KosmPoint& center,
								float rx, float ry, const KosmColor& color,
								const KosmStrokeStyle& style = KosmStrokeStyle());
			void			StrokeLine(const KosmPoint& from,
								const KosmPoint& to,
								const KosmColor& color,
								const KosmStrokeStyle& style = KosmStrokeStyle());
			void			StrokePath(const KosmPath& path,
								const KosmColor& color,
								const KosmStrokeStyle& style = KosmStrokeStyle());

	// Stroke with gradient
			void			StrokePath(const KosmPath& path,
								const KosmGradient& gradient,
								const KosmStrokeStyle& style = KosmStrokeStyle());

	// ========================================================================
	// Images
	// ========================================================================

			void			DrawImage(const KosmImage& image,
								const KosmPoint& position);
			void			DrawImage(const KosmImage& image,
								const KosmRect& destRect);
			void			DrawImage(const KosmImage& image,
								const KosmRect& srcRect,
								const KosmRect& destRect);

	// Nine-patch / nine-slice
			void			DrawImageNineSlice(const KosmImage& image,
								const KosmRect& destRect,
								float insetLeft, float insetTop,
								float insetRight, float insetBottom);

	// ========================================================================
	// Text
	// ========================================================================

			void			DrawText(const char* text,
								const KosmPoint& position,
								const KosmFont& font,
								const KosmColor& color);

			void			DrawText(const char* text,
								const KosmPoint& position,
								const KosmTextStyle& style,
								const KosmColor& color);

			void			DrawText(const char* text,
								const KosmRect& rect,
								const KosmTextStyle& style,
								const KosmColor& color);

	// Text with gradient
			void			DrawText(const char* text,
								const KosmPoint& position,
								const KosmFont& font,
								const KosmGradient& gradient);

	// Text with outline
			void			DrawTextWithOutline(const char* text,
								const KosmPoint& position,
								const KosmFont& font,
								const KosmColor& fillColor,
								const KosmColor& outlineColor,
								float outlineWidth);

	// ========================================================================
	// State management
	// ========================================================================

			void			Save();
			void			Restore();

	// ========================================================================
	// Transform
	// ========================================================================

			void			Translate(float tx, float ty);
			void			Translate(const KosmPoint& offset);
			void			Scale(float sx, float sy);
			void			Scale(float s);
			void			Scale(float sx, float sy, const KosmPoint& center);
			void			Rotate(float radians);
			void			Rotate(float radians, const KosmPoint& center);
			void			Skew(float sx, float sy);
			void			Transform(const KosmMatrix& matrix);
			void			SetTransform(const KosmMatrix& matrix);
			void			ResetTransform();
			KosmMatrix		CurrentTransform() const;

	// ========================================================================
	// Clipping
	// ========================================================================

			void			ClipRect(const KosmRect& rect);
			void			ClipRoundRect(const KosmRect& rect, float radius);
			void			ClipCircle(const KosmPoint& center, float radius);
			void			ClipPath(const KosmPath& path);
			void			ResetClip();

	// ========================================================================
	// Opacity
	// ========================================================================

			void			SetOpacity(float opacity);
			float			Opacity() const;

	// ========================================================================
	// Blend mode
	// ========================================================================

			void			SetBlendMode(kosm_blend_mode mode);
			kosm_blend_mode	BlendMode() const;

	// ========================================================================
	// Shadow
	// ========================================================================

			void			SetShadow(const KosmShadow& shadow);
			void			SetShadow(const KosmColor& color,
								float offsetX, float offsetY, float blur);
			void			ClearShadow();
			KosmShadow		CurrentShadow() const;

	// ========================================================================
	// Effects (applied to subsequent draws until cleared)
	// ========================================================================

			void			SetBlur(float sigma);
			void			ClearBlur();

	// ========================================================================
	// Masking (Core Graphics style)
	// ========================================================================

			void			BeginMask(kosm_mask_method method = KOSM_MASK_ALPHA);
			void			EndMask();
			void			ClearMask();

	// Alternative: clip to existing mask
			void			ClipToMask(const KosmPath& maskPath,
								kosm_mask_method method = KOSM_MASK_ALPHA);
			void			ClipToMask(const KosmImage& maskImage,
								kosm_mask_method method = KOSM_MASK_ALPHA);

	// ========================================================================
	// Layer (for group opacity / effects)
	// ========================================================================

			void			BeginLayer(float opacity = 1.0f);
			void			BeginLayer(const KosmRect& bounds,
								float opacity = 1.0f);
			void			EndLayer();

	// ========================================================================
	// Flush
	// ========================================================================

			status_t		Flush();

private:
							KosmCanvas(const KosmCanvas&);
			KosmCanvas&		operator=(const KosmCanvas&);

			struct Impl;
			Impl*			fImpl;
};

#endif /* _KOSM_CANVAS_HPP */
