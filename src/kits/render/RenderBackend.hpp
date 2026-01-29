/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _RENDER_BACKEND_HPP
#define _RENDER_BACKEND_HPP

#include <KosmCanvas.hpp>
#include <KosmColor.hpp>
#include <KosmGeometry.hpp>
#include <KosmGradient.hpp>
#include <SurfaceTypes.hpp>

class RenderBackend {
public:
	virtual						~RenderBackend();

	// Lifecycle
	static	status_t			Initialize(uint32 threads = 0);
	static	void				Terminate();
	static	RenderBackend*		Instance();

	// Target
	virtual status_t			SetTarget(void* buffer, uint32 stride,
									uint32 width, uint32 height,
									pixel_format format) = 0;
	virtual uint32				Width() const = 0;
	virtual uint32				Height() const = 0;

	// Clear
	virtual void				Clear() = 0;
	virtual void				Clear(const KosmColor& color) = 0;

	// ========================================================================
	// Fill with solid color
	// ========================================================================

	virtual void				FillRect(const KosmRect& rect,
									const KosmColor& color) = 0;
	virtual void				FillRoundRect(const KosmRect& rect,
									float rx, float ry,
									const KosmColor& color) = 0;
	virtual void				FillCircle(const KosmPoint& center,
									float radius,
									const KosmColor& color) = 0;
	virtual void				FillEllipse(const KosmPoint& center,
									float rx, float ry,
									const KosmColor& color) = 0;
	virtual void				FillPath(void* pathHandle,
									const KosmColor& color) = 0;

	// ========================================================================
	// Fill with gradient
	// ========================================================================

	virtual void				FillRectGradient(const KosmRect& rect,
									void* gradientHandle) = 0;
	virtual void				FillRoundRectGradient(const KosmRect& rect,
									float rx, float ry,
									void* gradientHandle) = 0;
	virtual void				FillCircleGradient(const KosmPoint& center,
									float radius,
									void* gradientHandle) = 0;
	virtual void				FillEllipseGradient(const KosmPoint& center,
									float rx, float ry,
									void* gradientHandle) = 0;
	virtual void				FillPathGradient(void* pathHandle,
									void* gradientHandle) = 0;

	// ========================================================================
	// Stroke
	// ========================================================================

	virtual void				StrokeRect(const KosmRect& rect,
									const KosmColor& color,
									const KosmStrokeStyle& style) = 0;
	virtual void				StrokeRoundRect(const KosmRect& rect,
									float rx, float ry,
									const KosmColor& color,
									const KosmStrokeStyle& style) = 0;
	virtual void				StrokeCircle(const KosmPoint& center,
									float radius, const KosmColor& color,
									const KosmStrokeStyle& style) = 0;
	virtual void				StrokeEllipse(const KosmPoint& center,
									float rx, float ry,
									const KosmColor& color,
									const KosmStrokeStyle& style) = 0;
	virtual void				StrokeLine(const KosmPoint& from,
									const KosmPoint& to,
									const KosmColor& color,
									const KosmStrokeStyle& style) = 0;
	virtual void				StrokePath(void* pathHandle,
									const KosmColor& color,
									const KosmStrokeStyle& style) = 0;
	virtual void				StrokePathGradient(void* pathHandle,
									void* gradientHandle,
									const KosmStrokeStyle& style) = 0;

	// ========================================================================
	// Image
	// ========================================================================

	virtual void				DrawImage(void* imageHandle,
									const KosmPoint& position) = 0;
	virtual void				DrawImage(void* imageHandle,
									const KosmRect& destRect) = 0;
	virtual void				DrawImage(void* imageHandle,
									const KosmRect& srcRect,
									const KosmRect& destRect) = 0;

	// ========================================================================
	// Text
	// ========================================================================

	virtual void				DrawText(const char* text,
									const KosmPoint& position,
									void* fontHandle,
									const KosmColor& color) = 0;
	virtual void				DrawTextGradient(const char* text,
									const KosmPoint& position,
									void* fontHandle,
									void* gradientHandle) = 0;
	virtual void				DrawTextWithOutline(const char* text,
									const KosmPoint& position,
									void* fontHandle,
									const KosmColor& fillColor,
									const KosmColor& outlineColor,
									float outlineWidth) = 0;
	virtual void				DrawTextInRect(const char* text,
									const KosmRect& rect,
									void* fontHandle,
									const KosmColor& color,
									kosm_text_align align,
									kosm_text_wrap wrap) = 0;

	// ========================================================================
	// State
	// ========================================================================

	virtual void				PushState() = 0;
	virtual void				PopState() = 0;

	// ========================================================================
	// Transform
	// ========================================================================

	virtual void				SetTransform(const KosmMatrix& matrix) = 0;
	virtual KosmMatrix			GetTransform() const = 0;

	// ========================================================================
	// Clip
	// ========================================================================

	virtual void				SetClipRect(const KosmRect& rect) = 0;
	virtual void				SetClipRoundRect(const KosmRect& rect,
									float radius) = 0;
	virtual void				SetClipCircle(const KosmPoint& center,
									float radius) = 0;
	virtual void				SetClipPath(void* pathHandle) = 0;
	virtual void				ResetClip() = 0;

	// ========================================================================
	// Opacity & Blend
	// ========================================================================

	virtual void				SetOpacity(float opacity) = 0;
	virtual float				GetOpacity() const = 0;
	virtual void				SetBlendMode(kosm_blend_mode mode) = 0;
	virtual kosm_blend_mode		GetBlendMode() const = 0;

	// ========================================================================
	// Shadow & Effects
	// ========================================================================

	virtual void				SetShadow(const KosmColor& color,
									float offsetX, float offsetY,
									float blur) = 0;
	virtual void				ClearShadow() = 0;
	virtual void				SetBlur(float sigma) = 0;
	virtual void				ClearBlur() = 0;

	// ========================================================================
	// Mask
	// ========================================================================

	virtual void				BeginMask() = 0;
	virtual void				EndMask() = 0;
	virtual void				ApplyMask() = 0;
	virtual void				ClearMask() = 0;

	// ========================================================================
	// Layer
	// ========================================================================

	virtual void				BeginLayer(const KosmRect& bounds,
									float opacity) = 0;
	virtual void				EndLayer() = 0;

	// ========================================================================
	// Flush
	// ========================================================================

	virtual status_t			Flush() = 0;

	// ========================================================================
	// Path
	// ========================================================================

	virtual void*				CreatePath() = 0;
	virtual void				DestroyPath(void* path) = 0;
	virtual void*				DuplicatePath(void* path) = 0;
	virtual void				PathMoveTo(void* path, float x, float y) = 0;
	virtual void				PathLineTo(void* path, float x, float y) = 0;
	virtual void				PathCubicTo(void* path,
									float cx1, float cy1,
									float cx2, float cy2,
									float x, float y) = 0;
	virtual void				PathClose(void* path) = 0;
	virtual void				PathReset(void* path) = 0;
	virtual void				PathAddRect(void* path,
									const KosmRect& rect) = 0;
	virtual void				PathAddRoundRect(void* path,
									const KosmRect& rect,
									float rx, float ry) = 0;
	virtual void				PathAddCircle(void* path,
									const KosmPoint& center,
									float radius) = 0;
	virtual void				PathAddEllipse(void* path,
									const KosmPoint& center,
									float rx, float ry) = 0;
	virtual void				PathAddArc(void* path,
									const KosmPoint& center,
									float radius,
									float startAngle,
									float sweepAngle) = 0;
	virtual void				PathAppend(void* path, void* other) = 0;
	virtual KosmRect			PathBounds(void* path) = 0;
	virtual void				PathSetFillRule(void* path,
									kosm_fill_rule rule) = 0;

	// ========================================================================
	// Gradient
	// ========================================================================

	virtual void*				CreateLinearGradient(float x1, float y1,
									float x2, float y2) = 0;
	virtual void*				CreateRadialGradient(float cx, float cy,
									float radius,
									float fx, float fy,
									float focalRadius) = 0;
	virtual void				DestroyGradient(void* gradient) = 0;
	virtual void				GradientAddColorStop(void* gradient,
									float offset,
									const KosmColor& color) = 0;
	virtual void				GradientSetColorStops(void* gradient,
									const KosmColorStop* stops,
									uint32 count) = 0;
	virtual void				GradientSetSpread(void* gradient,
									kosm_gradient_spread spread) = 0;
	virtual void				GradientSetTransform(void* gradient,
									const KosmMatrix& matrix) = 0;

	// ========================================================================
	// Image
	// ========================================================================

	virtual void*				CreateImage() = 0;
	virtual void				DestroyImage(void* image) = 0;
	virtual status_t			ImageLoad(void* image, const char* path) = 0;
	virtual status_t			ImageLoadData(void* image,
									const void* data, size_t size,
									const char* mimeType) = 0;
	virtual status_t			ImageSetPixels(void* image,
									const uint32* pixels,
									uint32 width, uint32 height,
									bool premultiplied) = 0;
	virtual uint32				ImageWidth(void* image) = 0;
	virtual uint32				ImageHeight(void* image) = 0;
	virtual void				ImageSetSize(void* image,
									float width, float height) = 0;
	virtual void				ImageSetTransform(void* image,
									const KosmMatrix& matrix) = 0;
	virtual void				ImageSetOpacity(void* image, float opacity) = 0;

	// ========================================================================
	// Font
	// ========================================================================

	virtual status_t			LoadFont(const char* path) = 0;
	virtual status_t			LoadFontData(const char* name,
									const void* data, size_t size) = 0;
	virtual status_t			UnloadFont(const char* path) = 0;

	virtual void*				CreateFont(const char* family, float size) = 0;
	virtual void				DestroyFont(void* font) = 0;
	virtual void				FontSetSize(void* font, float size) = 0;
	virtual void				FontSetItalic(void* font, float shear) = 0;
	virtual void				FontSetLetterSpacing(void* font,
									float spacing) = 0;
	virtual void				FontSetLineSpacing(void* font,
									float spacing) = 0;
	virtual float				FontMeasureWidth(void* font,
									const char* text) = 0;
	virtual KosmRect			FontMeasureBounds(void* font,
									const char* text) = 0;

protected:
								RenderBackend() {}
};

#endif /* _RENDER_BACKEND_HPP */
