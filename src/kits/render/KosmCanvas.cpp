/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmCanvas.hpp>
#include <KosmPath.hpp>
#include <KosmGradient.hpp>
#include <KosmImage.hpp>
#include <KosmFont.hpp>
#include <KosmSurface.hpp>

#include "RenderBackend.hpp"

#include <new>


struct KosmCanvas::Impl {
	KosmSurface*	surface;
	RenderBackend*	backend;

	Impl(KosmSurface* s)
		:
		surface(s),
		backend(nullptr)
	{
	}

	~Impl()
	{
		// Don't delete backend - it's a singleton
	}
};


// ============================================================================
// Static methods
// ============================================================================

status_t
KosmCanvas::Initialize(uint32 threads)
{
	return RenderBackend::Initialize(threads);
}


void
KosmCanvas::Terminate()
{
	RenderBackend::Terminate();
}


// ============================================================================
// Construction
// ============================================================================

KosmCanvas::KosmCanvas(KosmSurface* surface)
	:
	fImpl(nullptr)
{
	if (surface == nullptr)
		return;

	fImpl = new(std::nothrow) Impl(surface);
	if (fImpl == nullptr)
		return;

	fImpl->backend = RenderBackend::Instance();
	if (fImpl->backend == nullptr) {
		delete fImpl;
		fImpl = nullptr;
		return;
	}

	surface->Lock();

	status_t status = fImpl->backend->SetTarget(
		surface->BaseAddress(),
		surface->BytesPerRow(),
		surface->Width(),
		surface->Height(),
		surface->Format()
	);

	if (status != B_OK) {
		surface->Unlock();
		delete fImpl;
		fImpl = nullptr;
	}
}


KosmCanvas::~KosmCanvas()
{
	if (fImpl != nullptr) {
		if (fImpl->surface != nullptr)
			fImpl->surface->Unlock();
		delete fImpl;
	}
}


// ============================================================================
// Target
// ============================================================================

KosmSurface*
KosmCanvas::Surface() const
{
	return fImpl != nullptr ? fImpl->surface : nullptr;
}


uint32
KosmCanvas::Width() const
{
	return fImpl != nullptr ? fImpl->backend->Width() : 0;
}


uint32
KosmCanvas::Height() const
{
	return fImpl != nullptr ? fImpl->backend->Height() : 0;
}


// ============================================================================
// Clear
// ============================================================================

void
KosmCanvas::Clear()
{
	if (fImpl != nullptr)
		fImpl->backend->Clear();
}


void
KosmCanvas::Clear(const KosmColor& color)
{
	if (fImpl != nullptr)
		fImpl->backend->Clear(color);
}


// ============================================================================
// Fill with solid color
// ============================================================================

void
KosmCanvas::FillRect(const KosmRect& rect, const KosmColor& color)
{
	if (fImpl != nullptr)
		fImpl->backend->FillRect(rect, color);
}


void
KosmCanvas::FillRoundRect(const KosmRect& rect, float radius,
	const KosmColor& color)
{
	if (fImpl != nullptr)
		fImpl->backend->FillRoundRect(rect, radius, radius, color);
}


void
KosmCanvas::FillRoundRect(const KosmRect& rect, float rx, float ry,
	const KosmColor& color)
{
	if (fImpl != nullptr)
		fImpl->backend->FillRoundRect(rect, rx, ry, color);
}


void
KosmCanvas::FillCircle(const KosmPoint& center, float radius,
	const KosmColor& color)
{
	if (fImpl != nullptr)
		fImpl->backend->FillCircle(center, radius, color);
}


void
KosmCanvas::FillEllipse(const KosmPoint& center, float rx, float ry,
	const KosmColor& color)
{
	if (fImpl != nullptr)
		fImpl->backend->FillEllipse(center, rx, ry, color);
}


void
KosmCanvas::FillPath(const KosmPath& path, const KosmColor& color)
{
	if (fImpl != nullptr)
		fImpl->backend->FillPath(path.NativeHandle(), color);
}


// ============================================================================
// Fill with gradient
// ============================================================================

void
KosmCanvas::FillRect(const KosmRect& rect, const KosmGradient& gradient)
{
	if (fImpl != nullptr)
		fImpl->backend->FillRectGradient(rect, gradient.NativeHandle());
}


void
KosmCanvas::FillRoundRect(const KosmRect& rect, float radius,
	const KosmGradient& gradient)
{
	if (fImpl != nullptr)
		fImpl->backend->FillRoundRectGradient(rect, radius, radius,
			gradient.NativeHandle());
}


void
KosmCanvas::FillRoundRect(const KosmRect& rect, float rx, float ry,
	const KosmGradient& gradient)
{
	if (fImpl != nullptr)
		fImpl->backend->FillRoundRectGradient(rect, rx, ry,
			gradient.NativeHandle());
}


void
KosmCanvas::FillCircle(const KosmPoint& center, float radius,
	const KosmGradient& gradient)
{
	if (fImpl != nullptr)
		fImpl->backend->FillCircleGradient(center, radius,
			gradient.NativeHandle());
}


void
KosmCanvas::FillEllipse(const KosmPoint& center, float rx, float ry,
	const KosmGradient& gradient)
{
	if (fImpl != nullptr)
		fImpl->backend->FillEllipseGradient(center, rx, ry,
			gradient.NativeHandle());
}


void
KosmCanvas::FillPath(const KosmPath& path, const KosmGradient& gradient)
{
	if (fImpl != nullptr)
		fImpl->backend->FillPathGradient(path.NativeHandle(),
			gradient.NativeHandle());
}


// ============================================================================
// Stroke
// ============================================================================

void
KosmCanvas::StrokeRect(const KosmRect& rect, const KosmColor& color,
	const KosmStrokeStyle& style)
{
	if (fImpl != nullptr)
		fImpl->backend->StrokeRect(rect, color, style);
}


void
KosmCanvas::StrokeRoundRect(const KosmRect& rect, float radius,
	const KosmColor& color, const KosmStrokeStyle& style)
{
	if (fImpl != nullptr)
		fImpl->backend->StrokeRoundRect(rect, radius, radius, color, style);
}


void
KosmCanvas::StrokeRoundRect(const KosmRect& rect, float rx, float ry,
	const KosmColor& color, const KosmStrokeStyle& style)
{
	if (fImpl != nullptr)
		fImpl->backend->StrokeRoundRect(rect, rx, ry, color, style);
}


void
KosmCanvas::StrokeCircle(const KosmPoint& center, float radius,
	const KosmColor& color, const KosmStrokeStyle& style)
{
	if (fImpl != nullptr)
		fImpl->backend->StrokeCircle(center, radius, color, style);
}


void
KosmCanvas::StrokeEllipse(const KosmPoint& center, float rx, float ry,
	const KosmColor& color, const KosmStrokeStyle& style)
{
	if (fImpl != nullptr)
		fImpl->backend->StrokeEllipse(center, rx, ry, color, style);
}


void
KosmCanvas::StrokeLine(const KosmPoint& from, const KosmPoint& to,
	const KosmColor& color, const KosmStrokeStyle& style)
{
	if (fImpl != nullptr)
		fImpl->backend->StrokeLine(from, to, color, style);
}


void
KosmCanvas::StrokePath(const KosmPath& path, const KosmColor& color,
	const KosmStrokeStyle& style)
{
	if (fImpl != nullptr)
		fImpl->backend->StrokePath(path.NativeHandle(), color, style);
}


void
KosmCanvas::StrokePath(const KosmPath& path, const KosmGradient& gradient,
	const KosmStrokeStyle& style)
{
	if (fImpl != nullptr)
		fImpl->backend->StrokePathGradient(path.NativeHandle(),
			gradient.NativeHandle(), style);
}


// ============================================================================
// Image
// ============================================================================

void
KosmCanvas::DrawImage(const KosmImage& image, const KosmPoint& position)
{
	if (fImpl != nullptr)
		fImpl->backend->DrawImage(image.NativeHandle(), position);
}


void
KosmCanvas::DrawImage(const KosmImage& image, const KosmRect& destRect)
{
	if (fImpl != nullptr)
		fImpl->backend->DrawImage(image.NativeHandle(), destRect);
}


void
KosmCanvas::DrawImage(const KosmImage& image, const KosmRect& srcRect,
	const KosmRect& destRect)
{
	if (fImpl != nullptr)
		fImpl->backend->DrawImage(image.NativeHandle(), srcRect, destRect);
}


void
KosmCanvas::DrawImageNineSlice(const KosmImage& image, const KosmRect& destRect,
	float insetLeft, float insetTop, float insetRight, float insetBottom)
{
	if (fImpl == nullptr)
		return;

	float srcW = image.Width();
	float srcH = image.Height();

	float dstW = destRect.width;
	float dstH = destRect.height;

	// 9 regions: corners (fixed size), edges (stretched), center (stretched)

	// Top-left corner
	DrawImage(image,
		KosmRect(0, 0, insetLeft, insetTop),
		KosmRect(destRect.x, destRect.y, insetLeft, insetTop));

	// Top-right corner
	DrawImage(image,
		KosmRect(srcW - insetRight, 0, insetRight, insetTop),
		KosmRect(destRect.x + dstW - insetRight, destRect.y, insetRight, insetTop));

	// Bottom-left corner
	DrawImage(image,
		KosmRect(0, srcH - insetBottom, insetLeft, insetBottom),
		KosmRect(destRect.x, destRect.y + dstH - insetBottom, insetLeft, insetBottom));

	// Bottom-right corner
	DrawImage(image,
		KosmRect(srcW - insetRight, srcH - insetBottom, insetRight, insetBottom),
		KosmRect(destRect.x + dstW - insetRight, destRect.y + dstH - insetBottom,
			insetRight, insetBottom));

	// Top edge
	DrawImage(image,
		KosmRect(insetLeft, 0, srcW - insetLeft - insetRight, insetTop),
		KosmRect(destRect.x + insetLeft, destRect.y,
			dstW - insetLeft - insetRight, insetTop));

	// Bottom edge
	DrawImage(image,
		KosmRect(insetLeft, srcH - insetBottom, srcW - insetLeft - insetRight, insetBottom),
		KosmRect(destRect.x + insetLeft, destRect.y + dstH - insetBottom,
			dstW - insetLeft - insetRight, insetBottom));

	// Left edge
	DrawImage(image,
		KosmRect(0, insetTop, insetLeft, srcH - insetTop - insetBottom),
		KosmRect(destRect.x, destRect.y + insetTop,
			insetLeft, dstH - insetTop - insetBottom));

	// Right edge
	DrawImage(image,
		KosmRect(srcW - insetRight, insetTop, insetRight, srcH - insetTop - insetBottom),
		KosmRect(destRect.x + dstW - insetRight, destRect.y + insetTop,
			insetRight, dstH - insetTop - insetBottom));

	// Center
	DrawImage(image,
		KosmRect(insetLeft, insetTop,
			srcW - insetLeft - insetRight, srcH - insetTop - insetBottom),
		KosmRect(destRect.x + insetLeft, destRect.y + insetTop,
			dstW - insetLeft - insetRight, dstH - insetTop - insetBottom));
}


// ============================================================================
// Text
// ============================================================================

void
KosmCanvas::DrawText(const char* text, const KosmPoint& position,
	const KosmFont& font, const KosmColor& color)
{
	if (fImpl != nullptr)
		fImpl->backend->DrawText(text, position, font.NativeHandle(), color);
}


void
KosmCanvas::DrawText(const char* text, const KosmPoint& position,
	const KosmTextStyle& style, const KosmColor& color)
{
	if (fImpl != nullptr)
		fImpl->backend->DrawText(text, position, style.font.NativeHandle(), color);
}


void
KosmCanvas::DrawText(const char* text, const KosmRect& rect,
	const KosmTextStyle& style, const KosmColor& color)
{
	if (fImpl != nullptr) {
		fImpl->backend->DrawTextInRect(text, rect, style.font.NativeHandle(),
			color, style.align, style.wrap);
	}
}


void
KosmCanvas::DrawText(const char* text, const KosmPoint& position,
	const KosmFont& font, const KosmGradient& gradient)
{
	if (fImpl != nullptr) {
		fImpl->backend->DrawTextGradient(text, position,
			font.NativeHandle(), gradient.NativeHandle());
	}
}


void
KosmCanvas::DrawTextWithOutline(const char* text, const KosmPoint& position,
	const KosmFont& font, const KosmColor& fillColor,
	const KosmColor& outlineColor, float outlineWidth)
{
	if (fImpl != nullptr) {
		fImpl->backend->DrawTextWithOutline(text, position,
			font.NativeHandle(), fillColor, outlineColor, outlineWidth);
	}
}


// ============================================================================
// State
// ============================================================================

void
KosmCanvas::Save()
{
	if (fImpl != nullptr)
		fImpl->backend->PushState();
}


void
KosmCanvas::Restore()
{
	if (fImpl != nullptr)
		fImpl->backend->PopState();
}


// ============================================================================
// Transform
// ============================================================================

void
KosmCanvas::Translate(float tx, float ty)
{
	if (fImpl == nullptr)
		return;

	KosmMatrix current = fImpl->backend->GetTransform();
	fImpl->backend->SetTransform(current.Translated(tx, ty));
}


void
KosmCanvas::Translate(const KosmPoint& offset)
{
	Translate(offset.x, offset.y);
}


void
KosmCanvas::Scale(float sx, float sy)
{
	if (fImpl == nullptr)
		return;

	KosmMatrix current = fImpl->backend->GetTransform();
	fImpl->backend->SetTransform(current.Scaled(sx, sy));
}


void
KosmCanvas::Scale(float s)
{
	Scale(s, s);
}


void
KosmCanvas::Scale(float sx, float sy, const KosmPoint& center)
{
	if (fImpl == nullptr)
		return;

	KosmMatrix current = fImpl->backend->GetTransform();
	KosmMatrix scale = KosmMatrix::Scale(sx, sy, center);
	fImpl->backend->SetTransform(current.Multiply(scale));
}


void
KosmCanvas::Rotate(float radians)
{
	if (fImpl == nullptr)
		return;

	KosmMatrix current = fImpl->backend->GetTransform();
	fImpl->backend->SetTransform(current.Rotated(radians));
}


void
KosmCanvas::Rotate(float radians, const KosmPoint& center)
{
	if (fImpl == nullptr)
		return;

	KosmMatrix current = fImpl->backend->GetTransform();
	KosmMatrix rotation = KosmMatrix::Rotate(radians, center);
	fImpl->backend->SetTransform(current.Multiply(rotation));
}


void
KosmCanvas::Skew(float sx, float sy)
{
	if (fImpl == nullptr)
		return;

	KosmMatrix current = fImpl->backend->GetTransform();
	KosmMatrix skew = KosmMatrix::Skew(sx, sy);
	fImpl->backend->SetTransform(current.Multiply(skew));
}


void
KosmCanvas::Transform(const KosmMatrix& matrix)
{
	if (fImpl == nullptr)
		return;

	KosmMatrix current = fImpl->backend->GetTransform();
	fImpl->backend->SetTransform(current.Multiply(matrix));
}


void
KosmCanvas::SetTransform(const KosmMatrix& matrix)
{
	if (fImpl != nullptr)
		fImpl->backend->SetTransform(matrix);
}


void
KosmCanvas::ResetTransform()
{
	if (fImpl != nullptr)
		fImpl->backend->SetTransform(KosmMatrix::Identity());
}


KosmMatrix
KosmCanvas::CurrentTransform() const
{
	if (fImpl != nullptr)
		return fImpl->backend->GetTransform();
	return KosmMatrix::Identity();
}


// ============================================================================
// Clipping
// ============================================================================

void
KosmCanvas::ClipRect(const KosmRect& rect)
{
	if (fImpl != nullptr)
		fImpl->backend->SetClipRect(rect);
}


void
KosmCanvas::ClipRoundRect(const KosmRect& rect, float radius)
{
	if (fImpl != nullptr)
		fImpl->backend->SetClipRoundRect(rect, radius);
}


void
KosmCanvas::ClipCircle(const KosmPoint& center, float radius)
{
	if (fImpl != nullptr)
		fImpl->backend->SetClipCircle(center, radius);
}


void
KosmCanvas::ClipPath(const KosmPath& path)
{
	if (fImpl != nullptr)
		fImpl->backend->SetClipPath(path.NativeHandle());
}


void
KosmCanvas::ResetClip()
{
	if (fImpl != nullptr)
		fImpl->backend->ResetClip();
}


// ============================================================================
// Opacity & Blend
// ============================================================================

void
KosmCanvas::SetOpacity(float opacity)
{
	if (fImpl != nullptr)
		fImpl->backend->SetOpacity(opacity);
}


float
KosmCanvas::Opacity() const
{
	if (fImpl != nullptr)
		return fImpl->backend->GetOpacity();
	return 1.0f;
}


void
KosmCanvas::SetBlendMode(kosm_blend_mode mode)
{
	if (fImpl != nullptr)
		fImpl->backend->SetBlendMode(mode);
}


kosm_blend_mode
KosmCanvas::BlendMode() const
{
	if (fImpl != nullptr)
		return fImpl->backend->GetBlendMode();
	return KOSM_BLEND_NORMAL;
}


// ============================================================================
// Shadow
// ============================================================================

void
KosmCanvas::SetShadow(const KosmShadow& shadow)
{
	if (fImpl != nullptr) {
		fImpl->backend->SetShadow(shadow.color, shadow.offsetX,
			shadow.offsetY, shadow.blur);
	}
}


void
KosmCanvas::SetShadow(const KosmColor& color, float offsetX, float offsetY,
	float blur)
{
	if (fImpl != nullptr)
		fImpl->backend->SetShadow(color, offsetX, offsetY, blur);
}


void
KosmCanvas::ClearShadow()
{
	if (fImpl != nullptr)
		fImpl->backend->ClearShadow();
}


KosmShadow
KosmCanvas::CurrentShadow() const
{
	return KosmShadow();
}


// ============================================================================
// Effects
// ============================================================================

void
KosmCanvas::SetBlur(float sigma)
{
	if (fImpl != nullptr)
		fImpl->backend->SetBlur(sigma);
}


void
KosmCanvas::ClearBlur()
{
	if (fImpl != nullptr)
		fImpl->backend->ClearBlur();
}


// ============================================================================
// Mask
// ============================================================================

void
KosmCanvas::BeginMask()
{
	if (fImpl != nullptr)
		fImpl->backend->BeginMask();
}


void
KosmCanvas::EndMask()
{
	if (fImpl != nullptr)
		fImpl->backend->EndMask();
}


void
KosmCanvas::ApplyMask()
{
	if (fImpl != nullptr)
		fImpl->backend->ApplyMask();
}


void
KosmCanvas::ClearMask()
{
	if (fImpl != nullptr)
		fImpl->backend->ClearMask();
}


// ============================================================================
// Layer
// ============================================================================

void
KosmCanvas::BeginLayer(float opacity)
{
	if (fImpl != nullptr) {
		KosmRect bounds(0, 0, fImpl->backend->Width(), fImpl->backend->Height());
		fImpl->backend->BeginLayer(bounds, opacity);
	}
}


void
KosmCanvas::BeginLayer(const KosmRect& bounds, float opacity)
{
	if (fImpl != nullptr)
		fImpl->backend->BeginLayer(bounds, opacity);
}


void
KosmCanvas::EndLayer()
{
	if (fImpl != nullptr)
		fImpl->backend->EndLayer();
}


// ============================================================================
// Flush
// ============================================================================

status_t
KosmCanvas::Flush()
{
	if (fImpl == nullptr)
		return B_NO_INIT;

	return fImpl->backend->Flush();
}
