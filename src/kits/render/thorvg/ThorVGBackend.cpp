/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ThorVGBackend.hpp"

#include <thorvg.h>
#include <atomic>
#include <cstring>
#include <cmath>
#include <new>


// ============================================================================
// RenderBackend static methods
// ============================================================================

static std::atomic<bool> sInitialized{false};


RenderBackend::~RenderBackend()
{
}


status_t
RenderBackend::Initialize(uint32 threads)
{
	if (sInitialized.exchange(true))
		return B_OK;

	auto result = tvg::Initializer::init(threads);
	if (result != tvg::Result::Success) {
		sInitialized = false;
		return B_ERROR;
	}

	return B_OK;
}


void
RenderBackend::Terminate()
{
	if (!sInitialized.exchange(false))
		return;

	tvg::Initializer::term();
}


RenderBackend*
RenderBackend::Create()
{
	return new(std::nothrow) ThorVGBackend();
}


// ============================================================================
// ThorVGBackend implementation
// ============================================================================

ThorVGBackend::ThorVGBackend()
	:
	fCanvas(nullptr),
	fScene(nullptr),
	fWidth(0),
	fHeight(0),
	fMaskScene(nullptr),
	fInMask(false)
{
	fCanvas = tvg::SwCanvas::gen();
	fScene = tvg::Scene::gen();
	fCanvas->push(fScene);
}


ThorVGBackend::~ThorVGBackend()
{
	delete fCanvas;
}


status_t
ThorVGBackend::SetTarget(void* buffer, uint32 stride, uint32 width,
	uint32 height, pixel_format format)
{
	tvg::ColorSpace cs;

	switch (format) {
		case PIXEL_FORMAT_ARGB8888:
			cs = tvg::ColorSpace::ARGB8888;
			break;
		case PIXEL_FORMAT_BGRA8888:
			cs = tvg::ColorSpace::ABGR8888;
			break;
		default:
			return B_BAD_VALUE;
	}

	fWidth = width;
	fHeight = height;

	auto result = fCanvas->target(
		static_cast<uint32_t*>(buffer),
		stride / sizeof(uint32_t),
		width,
		height,
		cs
	);

	return (result == tvg::Result::Success) ? B_OK : B_ERROR;
}


void
ThorVGBackend::Clear()
{
	fScene->clear();
}


void
ThorVGBackend::Clear(const KosmColor& color)
{
	fScene->clear();

	if (color.a > 0) {
		auto bg = tvg::Shape::gen();
		bg->appendRect(0, 0, fWidth, fHeight);
		bg->fill(color.R8(), color.G8(), color.B8(), color.A8());
		fScene->push(std::move(bg));
	}
}


// ============================================================================
// Fill with solid color
// ============================================================================

void
ThorVGBackend::FillRect(const KosmRect& rect, const KosmColor& color)
{
	auto shape = tvg::Shape::gen();
	shape->appendRect(rect.x, rect.y, rect.width, rect.height);
	_ApplyFill(shape.get(), color);
	_ApplyState(shape.get());

	if (fCurrentState.hasShadow)
		_DrawShadow(shape.get());

	fScene->push(std::move(shape));
}


void
ThorVGBackend::FillRoundRect(const KosmRect& rect, float rx, float ry,
	const KosmColor& color)
{
	auto shape = tvg::Shape::gen();
	shape->appendRect(rect.x, rect.y, rect.width, rect.height, rx, ry);
	_ApplyFill(shape.get(), color);
	_ApplyState(shape.get());

	if (fCurrentState.hasShadow)
		_DrawShadow(shape.get());

	fScene->push(std::move(shape));
}


void
ThorVGBackend::FillCircle(const KosmPoint& center, float radius,
	const KosmColor& color)
{
	auto shape = tvg::Shape::gen();
	shape->appendCircle(center.x, center.y, radius, radius);
	_ApplyFill(shape.get(), color);
	_ApplyState(shape.get());

	if (fCurrentState.hasShadow)
		_DrawShadow(shape.get());

	fScene->push(std::move(shape));
}


void
ThorVGBackend::FillEllipse(const KosmPoint& center, float rx, float ry,
	const KosmColor& color)
{
	auto shape = tvg::Shape::gen();
	shape->appendCircle(center.x, center.y, rx, ry);
	_ApplyFill(shape.get(), color);
	_ApplyState(shape.get());

	if (fCurrentState.hasShadow)
		_DrawShadow(shape.get());

	fScene->push(std::move(shape));
}


void
ThorVGBackend::FillPath(void* pathHandle, const KosmColor& color)
{
	if (pathHandle == nullptr)
		return;

	auto original = static_cast<tvg::Shape*>(pathHandle);
	auto shape = tvg::cast<tvg::Shape>(original->duplicate());

	_ApplyFill(shape.get(), color);
	_ApplyState(shape.get());

	if (fCurrentState.hasShadow)
		_DrawShadow(shape.get());

	fScene->push(std::move(shape));
}


// ============================================================================
// Fill with gradient
// ============================================================================

void
ThorVGBackend::FillRectGradient(const KosmRect& rect, void* gradientHandle)
{
	auto shape = tvg::Shape::gen();
	shape->appendRect(rect.x, rect.y, rect.width, rect.height);
	_ApplyGradientFill(shape.get(), gradientHandle);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


void
ThorVGBackend::FillRoundRectGradient(const KosmRect& rect, float rx, float ry,
	void* gradientHandle)
{
	auto shape = tvg::Shape::gen();
	shape->appendRect(rect.x, rect.y, rect.width, rect.height, rx, ry);
	_ApplyGradientFill(shape.get(), gradientHandle);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


void
ThorVGBackend::FillCircleGradient(const KosmPoint& center, float radius,
	void* gradientHandle)
{
	auto shape = tvg::Shape::gen();
	shape->appendCircle(center.x, center.y, radius, radius);
	_ApplyGradientFill(shape.get(), gradientHandle);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


void
ThorVGBackend::FillEllipseGradient(const KosmPoint& center, float rx, float ry,
	void* gradientHandle)
{
	auto shape = tvg::Shape::gen();
	shape->appendCircle(center.x, center.y, rx, ry);
	_ApplyGradientFill(shape.get(), gradientHandle);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


void
ThorVGBackend::FillPathGradient(void* pathHandle, void* gradientHandle)
{
	if (pathHandle == nullptr)
		return;

	auto original = static_cast<tvg::Shape*>(pathHandle);
	auto shape = tvg::cast<tvg::Shape>(original->duplicate());

	_ApplyGradientFill(shape.get(), gradientHandle);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


// ============================================================================
// Stroke
// ============================================================================

void
ThorVGBackend::StrokeRect(const KosmRect& rect, const KosmColor& color,
	const KosmStrokeStyle& style)
{
	auto shape = tvg::Shape::gen();
	shape->appendRect(rect.x, rect.y, rect.width, rect.height);
	_ApplyStroke(shape.get(), color, style);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


void
ThorVGBackend::StrokeRoundRect(const KosmRect& rect, float rx, float ry,
	const KosmColor& color, const KosmStrokeStyle& style)
{
	auto shape = tvg::Shape::gen();
	shape->appendRect(rect.x, rect.y, rect.width, rect.height, rx, ry);
	_ApplyStroke(shape.get(), color, style);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


void
ThorVGBackend::StrokeCircle(const KosmPoint& center, float radius,
	const KosmColor& color, const KosmStrokeStyle& style)
{
	auto shape = tvg::Shape::gen();
	shape->appendCircle(center.x, center.y, radius, radius);
	_ApplyStroke(shape.get(), color, style);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


void
ThorVGBackend::StrokeEllipse(const KosmPoint& center, float rx, float ry,
	const KosmColor& color, const KosmStrokeStyle& style)
{
	auto shape = tvg::Shape::gen();
	shape->appendCircle(center.x, center.y, rx, ry);
	_ApplyStroke(shape.get(), color, style);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


void
ThorVGBackend::StrokeLine(const KosmPoint& from, const KosmPoint& to,
	const KosmColor& color, const KosmStrokeStyle& style)
{
	auto shape = tvg::Shape::gen();
	shape->moveTo(from.x, from.y);
	shape->lineTo(to.x, to.y);
	_ApplyStroke(shape.get(), color, style);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


void
ThorVGBackend::StrokePath(void* pathHandle, const KosmColor& color,
	const KosmStrokeStyle& style)
{
	if (pathHandle == nullptr)
		return;

	auto original = static_cast<tvg::Shape*>(pathHandle);
	auto shape = tvg::cast<tvg::Shape>(original->duplicate());

	_ApplyStroke(shape.get(), color, style);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


void
ThorVGBackend::StrokePathGradient(void* pathHandle, void* gradientHandle,
	const KosmStrokeStyle& style)
{
	if (pathHandle == nullptr)
		return;

	auto original = static_cast<tvg::Shape*>(pathHandle);
	auto shape = tvg::cast<tvg::Shape>(original->duplicate());

	_ApplyGradientStroke(shape.get(), gradientHandle, style);
	_ApplyState(shape.get());
	fScene->push(std::move(shape));
}


// ============================================================================
// Image
// ============================================================================

void
ThorVGBackend::DrawImage(void* imageHandle, const KosmPoint& position)
{
	if (imageHandle == nullptr)
		return;

	auto original = static_cast<tvg::Picture*>(imageHandle);
	auto picture = tvg::cast<tvg::Picture>(original->duplicate());

	picture->translate(position.x, position.y);
	picture->opacity(static_cast<uint8_t>(fCurrentState.opacity * 255));
	picture->blend(_ConvertBlendMode(fCurrentState.blendMode));

	if (!fCurrentState.transform.IsIdentity()) {
		tvg::Matrix m = {
			fCurrentState.transform.A(),
			fCurrentState.transform.B(),
			fCurrentState.transform.Tx(),
			fCurrentState.transform.C(),
			fCurrentState.transform.D(),
			fCurrentState.transform.Ty(),
			0, 0, 1
		};
		picture->transform(m);
	}

	if (fCurrentState.hasClipRect) {
		auto clipper = _CreateClipShape();
		picture->clip(std::move(tvg::cast<tvg::Shape>(clipper)));
	}

	fScene->push(std::move(picture));
}


void
ThorVGBackend::DrawImage(void* imageHandle, const KosmRect& destRect)
{
	if (imageHandle == nullptr)
		return;

	auto original = static_cast<tvg::Picture*>(imageHandle);
	auto picture = tvg::cast<tvg::Picture>(original->duplicate());

	picture->translate(destRect.x, destRect.y);
	picture->size(destRect.width, destRect.height);
	picture->opacity(static_cast<uint8_t>(fCurrentState.opacity * 255));
	picture->blend(_ConvertBlendMode(fCurrentState.blendMode));

	fScene->push(std::move(picture));
}


void
ThorVGBackend::DrawImage(void* imageHandle, const KosmRect& srcRect,
	const KosmRect& destRect)
{
	if (imageHandle == nullptr)
		return;

	auto original = static_cast<tvg::Picture*>(imageHandle);
	auto picture = tvg::cast<tvg::Picture>(original->duplicate());

	// Scale from src to dest
	float scaleX = destRect.width / srcRect.width;
	float scaleY = destRect.height / srcRect.height;

	// Offset in source
	float offsetX = -srcRect.x * scaleX + destRect.x;
	float offsetY = -srcRect.y * scaleY + destRect.y;

	tvg::Matrix m = {
		scaleX, 0, offsetX,
		0, scaleY, offsetY,
		0, 0, 1
	};
	picture->transform(m);

	// Clip to dest rect
	auto clipper = tvg::Shape::gen();
	clipper->appendRect(destRect.x, destRect.y, destRect.width, destRect.height);
	picture->clip(std::move(clipper));

	picture->opacity(static_cast<uint8_t>(fCurrentState.opacity * 255));
	picture->blend(_ConvertBlendMode(fCurrentState.blendMode));

	fScene->push(std::move(picture));
}


// ============================================================================
// Text
// ============================================================================

void
ThorVGBackend::DrawText(const char* text, const KosmPoint& position,
	void* fontHandle, const KosmColor& color)
{
	if (text == nullptr || fontHandle == nullptr)
		return;

	auto fontInfo = static_cast<FontInfo*>(fontHandle);

	auto tvgText = tvg::Text::gen();
	tvgText->font(fontInfo->family, fontInfo->size);
	tvgText->text(text);
	tvgText->fill(color.R8(), color.G8(), color.B8());
	tvgText->translate(position.x, position.y);

	if (fontInfo->italicShear > 0)
		tvgText->italic(fontInfo->italicShear);

	tvgText->opacity(static_cast<uint8_t>(fCurrentState.opacity * color.a * 255));
	tvgText->blend(_ConvertBlendMode(fCurrentState.blendMode));

	fScene->push(std::move(tvgText));
}


void
ThorVGBackend::DrawTextGradient(const char* text, const KosmPoint& position,
	void* fontHandle, void* gradientHandle)
{
	if (text == nullptr || fontHandle == nullptr || gradientHandle == nullptr)
		return;

	auto fontInfo = static_cast<FontInfo*>(fontHandle);
	auto gradient = static_cast<tvg::Fill*>(gradientHandle);

	auto tvgText = tvg::Text::gen();
	tvgText->font(fontInfo->family, fontInfo->size);
	tvgText->text(text);
	tvgText->fill(tvg::cast<tvg::Fill>(gradient->duplicate()));
	tvgText->translate(position.x, position.y);

	if (fontInfo->italicShear > 0)
		tvgText->italic(fontInfo->italicShear);

	tvgText->opacity(static_cast<uint8_t>(fCurrentState.opacity * 255));
	tvgText->blend(_ConvertBlendMode(fCurrentState.blendMode));

	fScene->push(std::move(tvgText));
}


void
ThorVGBackend::DrawTextWithOutline(const char* text, const KosmPoint& position,
	void* fontHandle, const KosmColor& fillColor, const KosmColor& outlineColor,
	float outlineWidth)
{
	if (text == nullptr || fontHandle == nullptr)
		return;

	auto fontInfo = static_cast<FontInfo*>(fontHandle);

	auto tvgText = tvg::Text::gen();
	tvgText->font(fontInfo->family, fontInfo->size);
	tvgText->text(text);
	tvgText->fill(fillColor.R8(), fillColor.G8(), fillColor.B8());
	tvgText->translate(position.x, position.y);

	if (fontInfo->italicShear > 0)
		tvgText->italic(fontInfo->italicShear);

	tvgText->opacity(static_cast<uint8_t>(fCurrentState.opacity * 255));

	fScene->push(std::move(tvgText));
}


void
ThorVGBackend::DrawTextInRect(const char* text, const KosmRect& rect,
	void* fontHandle, const KosmColor& color, kosm_text_align align,
	kosm_text_wrap wrap)
{
	if (text == nullptr || fontHandle == nullptr)
		return;

	auto fontInfo = static_cast<FontInfo*>(fontHandle);

	auto tvgText = tvg::Text::gen();
	tvgText->font(fontInfo->family, fontInfo->size);
	tvgText->text(text);
	tvgText->fill(color.R8(), color.G8(), color.B8());

	float alignX = 0;
	switch (align) {
		case KOSM_TEXT_ALIGN_CENTER:
			alignX = 0.5f;
			break;
		case KOSM_TEXT_ALIGN_RIGHT:
			alignX = 1.0f;
			break;
		default:
			alignX = 0;
			break;
	}

	tvgText->translate(rect.x, rect.y);
	tvgText->opacity(static_cast<uint8_t>(fCurrentState.opacity * color.a * 255));

	fScene->push(std::move(tvgText));
}


// ============================================================================
// State
// ============================================================================

void
ThorVGBackend::PushState()
{
	fStateStack.push_back(fCurrentState);
}


void
ThorVGBackend::PopState()
{
	if (!fStateStack.empty()) {
		fCurrentState = fStateStack.back();
		fStateStack.pop_back();
	}
}


void
ThorVGBackend::SetTransform(const KosmMatrix& matrix)
{
	fCurrentState.transform = matrix;
}


KosmMatrix
ThorVGBackend::GetTransform() const
{
	return fCurrentState.transform;
}


void
ThorVGBackend::SetClipRect(const KosmRect& rect)
{
	fCurrentState.clipRect = rect;
	fCurrentState.hasClipRect = true;
}


void
ThorVGBackend::SetClipRoundRect(const KosmRect& rect, float radius)
{
	if (fCurrentState.clipPath != nullptr)
		delete fCurrentState.clipPath;

	fCurrentState.clipPath = tvg::Shape::gen().release();
	fCurrentState.clipPath->appendRect(rect.x, rect.y, rect.width, rect.height,
		radius, radius);
	fCurrentState.hasClipRect = false;
}


void
ThorVGBackend::SetClipCircle(const KosmPoint& center, float radius)
{
	if (fCurrentState.clipPath != nullptr)
		delete fCurrentState.clipPath;

	fCurrentState.clipPath = tvg::Shape::gen().release();
	fCurrentState.clipPath->appendCircle(center.x, center.y, radius, radius);
	fCurrentState.hasClipRect = false;
}


void
ThorVGBackend::SetClipPath(void* pathHandle)
{
	if (pathHandle == nullptr)
		return;

	if (fCurrentState.clipPath != nullptr)
		delete fCurrentState.clipPath;

	auto original = static_cast<tvg::Shape*>(pathHandle);
	fCurrentState.clipPath = static_cast<tvg::Shape*>(original->duplicate().release());
	fCurrentState.hasClipRect = false;
}


void
ThorVGBackend::ResetClip()
{
	fCurrentState.hasClipRect = false;
	if (fCurrentState.clipPath != nullptr) {
		delete fCurrentState.clipPath;
		fCurrentState.clipPath = nullptr;
	}
}


void
ThorVGBackend::SetOpacity(float opacity)
{
	fCurrentState.opacity = opacity;
}


float
ThorVGBackend::GetOpacity() const
{
	return fCurrentState.opacity;
}


void
ThorVGBackend::SetBlendMode(kosm_blend_mode mode)
{
	fCurrentState.blendMode = mode;
}


kosm_blend_mode
ThorVGBackend::GetBlendMode() const
{
	return fCurrentState.blendMode;
}


// ============================================================================
// Shadow & Effects
// ============================================================================

void
ThorVGBackend::SetShadow(const KosmColor& color, float offsetX, float offsetY,
	float blur)
{
	fCurrentState.hasShadow = true;
	fCurrentState.shadowColor = color;
	fCurrentState.shadowOffsetX = offsetX;
	fCurrentState.shadowOffsetY = offsetY;
	fCurrentState.shadowBlur = blur;
}


void
ThorVGBackend::ClearShadow()
{
	fCurrentState.hasShadow = false;
}


void
ThorVGBackend::SetBlur(float sigma)
{
	fCurrentState.blur = sigma;
}


void
ThorVGBackend::ClearBlur()
{
	fCurrentState.blur = 0;
}


// ============================================================================
// Mask
// ============================================================================

void
ThorVGBackend::BeginMask()
{
	fMaskScene = tvg::Scene::gen().release();
	fInMask = true;
}


void
ThorVGBackend::EndMask()
{
	fInMask = false;
}


void
ThorVGBackend::ApplyMask()
{
	if (fMaskScene != nullptr) {
		fMaskScene = nullptr;
	}
}


void
ThorVGBackend::ClearMask()
{
	if (fMaskScene != nullptr) {
		delete fMaskScene;
		fMaskScene = nullptr;
	}
	fInMask = false;
}


// ============================================================================
// Layer
// ============================================================================

void
ThorVGBackend::BeginLayer(const KosmRect& bounds, float opacity)
{
	auto layerScene = tvg::Scene::gen();

	LayerInfo info;
	info.scene = fScene;
	info.opacity = opacity;
	fLayerStack.push_back(info);

	fScene = layerScene.release();
}


void
ThorVGBackend::EndLayer()
{
	if (fLayerStack.empty())
		return;

	LayerInfo info = fLayerStack.back();
	fLayerStack.pop_back();

	tvg::Scene* layerScene = fScene;
	fScene = info.scene;

	layerScene->opacity(static_cast<uint8_t>(info.opacity * 255));

	fScene->push(std::unique_ptr<tvg::Scene>(layerScene));
}


// ============================================================================
// Flush
// ============================================================================

status_t
ThorVGBackend::Flush()
{
	if (fCanvas->draw() != tvg::Result::Success)
		return B_ERROR;

	fCanvas->sync();

	// Clear scene for next frame
	fScene->clear();

	return B_OK;
}


// ============================================================================
// Path
// ============================================================================

void*
ThorVGBackend::CreatePath()
{
	return tvg::Shape::gen().release();
}


void
ThorVGBackend::DestroyPath(void* path)
{
	if (path != nullptr) {
		delete static_cast<tvg::Shape*>(path);
	}
}


void*
ThorVGBackend::DuplicatePath(void* path)
{
	if (path == nullptr)
		return nullptr;
	return static_cast<tvg::Shape*>(path)->duplicate().release();
}


void
ThorVGBackend::PathMoveTo(void* path, float x, float y)
{
	if (path != nullptr)
		static_cast<tvg::Shape*>(path)->moveTo(x, y);
}


void
ThorVGBackend::PathLineTo(void* path, float x, float y)
{
	if (path != nullptr)
		static_cast<tvg::Shape*>(path)->lineTo(x, y);
}


void
ThorVGBackend::PathCubicTo(void* path, float cx1, float cy1,
	float cx2, float cy2, float x, float y)
{
	if (path != nullptr)
		static_cast<tvg::Shape*>(path)->cubicTo(cx1, cy1, cx2, cy2, x, y);
}


void
ThorVGBackend::PathClose(void* path)
{
	if (path != nullptr)
		static_cast<tvg::Shape*>(path)->close();
}


void
ThorVGBackend::PathReset(void* path)
{
	if (path != nullptr)
		static_cast<tvg::Shape*>(path)->reset();
}


void
ThorVGBackend::PathAddRect(void* path, const KosmRect& rect)
{
	if (path != nullptr) {
		static_cast<tvg::Shape*>(path)->appendRect(
			rect.x, rect.y, rect.width, rect.height);
	}
}


void
ThorVGBackend::PathAddRoundRect(void* path, const KosmRect& rect,
	float rx, float ry)
{
	if (path != nullptr) {
		static_cast<tvg::Shape*>(path)->appendRect(
			rect.x, rect.y, rect.width, rect.height, rx, ry);
	}
}


void
ThorVGBackend::PathAddCircle(void* path, const KosmPoint& center, float radius)
{
	if (path != nullptr) {
		static_cast<tvg::Shape*>(path)->appendCircle(
			center.x, center.y, radius, radius);
	}
}


void
ThorVGBackend::PathAddEllipse(void* path, const KosmPoint& center,
	float rx, float ry)
{
	if (path != nullptr) {
		static_cast<tvg::Shape*>(path)->appendCircle(
			center.x, center.y, rx, ry);
	}
}


void
ThorVGBackend::PathAddArc(void* path, const KosmPoint& center, float radius,
	float startAngle, float sweepAngle)
{
	if (path == nullptr)
		return;

	auto shape = static_cast<tvg::Shape*>(path);

	// Convert angles to radians
	float startRad = startAngle * M_PI / 180.0f;
	float endRad = (startAngle + sweepAngle) * M_PI / 180.0f;

	// Start point
	float x0 = center.x + radius * cosf(startRad);
	float y0 = center.y + radius * sinf(startRad);
	shape->moveTo(x0, y0);

	// Approximate arc with cubic bezier segments
	int segments = static_cast<int>(fabsf(sweepAngle) / 90.0f) + 1;
	float angleStep = sweepAngle / segments * M_PI / 180.0f;

	float angle = startRad;
	for (int i = 0; i < segments; i++) {
		float nextAngle = angle + angleStep;

		float x1 = center.x + radius * cosf(angle);
		float y1 = center.y + radius * sinf(angle);
		float x4 = center.x + radius * cosf(nextAngle);
		float y4 = center.y + radius * sinf(nextAngle);

		float ax = x1 - center.x;
		float ay = y1 - center.y;
		float bx = x4 - center.x;
		float by = y4 - center.y;

		float q1 = ax * ax + ay * ay;
		float q2 = q1 + ax * bx + ay * by;
		float k2 = 4.0f / 3.0f * (sqrtf(2.0f * q1 * q2) - q2) /
			(ax * by - ay * bx);

		float x2 = x1 - k2 * ay;
		float y2 = y1 + k2 * ax;
		float x3 = x4 + k2 * by;
		float y3 = y4 - k2 * bx;

		shape->cubicTo(x2, y2, x3, y3, x4, y4);

		angle = nextAngle;
	}
}


void
ThorVGBackend::PathAppend(void* path, void* other)
{
	if (path == nullptr || other == nullptr)
		return;

	auto shape = static_cast<tvg::Shape*>(path);
	auto otherShape = static_cast<tvg::Shape*>(other);

	const tvg::PathCommand* cmds;
	uint32_t cmdsCnt;
	const tvg::Point* pts;
	uint32_t ptsCnt;

	otherShape->pathCommands(&cmds, &cmdsCnt);
	otherShape->pathCoords(&pts, &ptsCnt);
	shape->appendPath(cmds, cmdsCnt, pts, ptsCnt);
}


KosmRect
ThorVGBackend::PathBounds(void* path)
{
	if (path == nullptr)
		return KosmRect();

	auto shape = static_cast<tvg::Shape*>(path);
	float x, y, w, h;

	if (shape->bounds(&x, &y, &w, &h) == tvg::Result::Success)
		return KosmRect(x, y, w, h);

	return KosmRect();
}


// ============================================================================
// Gradient
// ============================================================================

void*
ThorVGBackend::CreateLinearGradient(float x1, float y1, float x2, float y2)
{
	auto gradient = tvg::LinearGradient::gen();
	gradient->linear(x1, y1, x2, y2);
	return gradient.release();
}


void*
ThorVGBackend::CreateRadialGradient(float cx, float cy, float radius,
	float fx, float fy, float focalRadius)
{
	auto gradient = tvg::RadialGradient::gen();
	gradient->radial(cx, cy, radius, fx, fy, focalRadius);
	return gradient.release();
}


void
ThorVGBackend::DestroyGradient(void* gradient)
{
	if (gradient != nullptr)
		delete static_cast<tvg::Fill*>(gradient);
}


void
ThorVGBackend::GradientAddColorStop(void* gradient, float offset,
	const KosmColor& color)
{
	if (gradient == nullptr)
		return;

	auto fill = static_cast<tvg::Fill*>(gradient);

	const tvg::Fill::ColorStop* existingStops;
	uint32_t count = fill->colorStops(&existingStops);

	auto newStops = new tvg::Fill::ColorStop[count + 1];
	for (uint32_t i = 0; i < count; i++)
		newStops[i] = existingStops[i];

	newStops[count].offset = offset;
	newStops[count].r = color.R8();
	newStops[count].g = color.G8();
	newStops[count].b = color.B8();
	newStops[count].a = color.A8();

	fill->colorStops(newStops, count + 1);
	delete[] newStops;
}


void
ThorVGBackend::GradientSetSpread(void* gradient, kosm_gradient_spread spread)
{
	if (gradient == nullptr)
		return;

	auto fill = static_cast<tvg::Fill*>(gradient);
	fill->spread(_ConvertSpread(spread));
}


void
ThorVGBackend::GradientSetTransform(void* gradient, const KosmMatrix& matrix)
{
	if (gradient == nullptr)
		return;

	auto fill = static_cast<tvg::Fill*>(gradient);
	tvg::Matrix m = {
		matrix.A(), matrix.B(), matrix.Tx(),
		matrix.C(), matrix.D(), matrix.Ty(),
		0, 0, 1
	};
	fill->transform(m);
}


// ============================================================================
// Image
// ============================================================================

void*
ThorVGBackend::CreateImage()
{
	return tvg::Picture::gen().release();
}


void
ThorVGBackend::DestroyImage(void* image)
{
	if (image != nullptr)
		delete static_cast<tvg::Picture*>(image);
}


status_t
ThorVGBackend::ImageLoad(void* image, const char* path)
{
	if (image == nullptr || path == nullptr)
		return B_BAD_VALUE;

	auto picture = static_cast<tvg::Picture*>(image);
	auto result = picture->load(path);

	return (result == tvg::Result::Success) ? B_OK : B_ERROR;
}


status_t
ThorVGBackend::ImageLoadData(void* image, const void* data, size_t size,
	const char* mimeType)
{
	if (image == nullptr || data == nullptr || size == 0)
		return B_BAD_VALUE;

	auto picture = static_cast<tvg::Picture*>(image);
	auto result = picture->load(static_cast<const char*>(data),
		static_cast<uint32_t>(size), mimeType, true);

	return (result == tvg::Result::Success) ? B_OK : B_ERROR;
}


status_t
ThorVGBackend::ImageSetPixels(void* image, const uint32* pixels,
	uint32 width, uint32 height, bool premultiplied)
{
	if (image == nullptr || pixels == nullptr)
		return B_BAD_VALUE;

	auto picture = static_cast<tvg::Picture*>(image);
	tvg::ColorSpace cs = premultiplied
		? tvg::ColorSpace::ARGB8888
		: tvg::ColorSpace::ARGB8888S;

	auto result = picture->load(pixels, width, height, cs, true);

	return (result == tvg::Result::Success) ? B_OK : B_ERROR;
}


uint32
ThorVGBackend::ImageWidth(void* image)
{
	if (image == nullptr)
		return 0;

	auto picture = static_cast<tvg::Picture*>(image);
	float w, h;
	picture->size(&w, &h);
	return static_cast<uint32>(w);
}


uint32
ThorVGBackend::ImageHeight(void* image)
{
	if (image == nullptr)
		return 0;

	auto picture = static_cast<tvg::Picture*>(image);
	float w, h;
	picture->size(&w, &h);
	return static_cast<uint32>(h);
}


void
ThorVGBackend::ImageSetSize(void* image, float width, float height)
{
	if (image == nullptr)
		return;

	static_cast<tvg::Picture*>(image)->size(width, height);
}


void
ThorVGBackend::ImageSetTransform(void* image, const KosmMatrix& matrix)
{
	if (image == nullptr)
		return;

	auto picture = static_cast<tvg::Picture*>(image);
	tvg::Matrix m = {
		matrix.A(), matrix.B(), matrix.Tx(),
		matrix.C(), matrix.D(), matrix.Ty(),
		0, 0, 1
	};
	picture->transform(m);
}


void
ThorVGBackend::ImageSetOpacity(void* image, float opacity)
{
	if (image == nullptr)
		return;

	static_cast<tvg::Picture*>(image)->opacity(
		static_cast<uint8_t>(opacity * 255));
}


// ============================================================================
// Font
// ============================================================================

status_t
ThorVGBackend::LoadFont(const char* path)
{
	if (path == nullptr)
		return B_BAD_VALUE;

	auto result = tvg::Text::load(path);
	return (result == tvg::Result::Success) ? B_OK : B_ERROR;
}


status_t
ThorVGBackend::LoadFontData(const char* name, const void* data, size_t size)
{
	if (name == nullptr || data == nullptr || size == 0)
		return B_BAD_VALUE;

	auto result = tvg::Text::load(name, static_cast<const char*>(data),
		static_cast<uint32_t>(size), "ttf", true);

	return (result == tvg::Result::Success) ? B_OK : B_ERROR;
}


status_t
ThorVGBackend::UnloadFont(const char* path)
{
	if (path == nullptr)
		return B_BAD_VALUE;

	auto result = tvg::Text::unload(path);
	return (result == tvg::Result::Success) ? B_OK : B_ERROR;
}


void*
ThorVGBackend::CreateFont(const char* family, float size)
{
	auto fontInfo = new FontInfo;
	fontInfo->family = family ? strdup(family) : nullptr;
	fontInfo->size = size;
	fontInfo->italicShear = 0;
	fontInfo->letterSpacing = 1.0f;
	fontInfo->lineSpacing = 1.0f;
	return fontInfo;
}


void
ThorVGBackend::DestroyFont(void* font)
{
	if (font == nullptr)
		return;

	auto fontInfo = static_cast<FontInfo*>(font);
	free(fontInfo->family);
	delete fontInfo;
}


void
ThorVGBackend::FontSetSize(void* font, float size)
{
	if (font != nullptr)
		static_cast<FontInfo*>(font)->size = size;
}


void
ThorVGBackend::FontSetItalic(void* font, float shear)
{
	if (font != nullptr)
		static_cast<FontInfo*>(font)->italicShear = shear;
}


void
ThorVGBackend::FontSetLetterSpacing(void* font, float spacing)
{
	if (font != nullptr)
		static_cast<FontInfo*>(font)->letterSpacing = spacing;
}


void
ThorVGBackend::FontSetLineSpacing(void* font, float spacing)
{
	if (font != nullptr)
		static_cast<FontInfo*>(font)->lineSpacing = spacing;
}


float
ThorVGBackend::FontMeasureWidth(void* font, const char* text)
{
	if (font == nullptr || text == nullptr)
		return 0;

	auto fontInfo = static_cast<FontInfo*>(font);

	// Rough estimate: average char width * string length
	return strlen(text) * fontInfo->size * 0.5f;
}


KosmRect
ThorVGBackend::FontMeasureBounds(void* font, const char* text)
{
	float width = FontMeasureWidth(font, text);
	float height = font ? static_cast<FontInfo*>(font)->size : 0;
	return KosmRect(0, 0, width, height);
}


// ============================================================================
// Private helpers
// ============================================================================

void
ThorVGBackend::_ApplyFill(tvg::Shape* shape, const KosmColor& color)
{
	uint8_t alpha = static_cast<uint8_t>(color.a * fCurrentState.opacity * 255);
	shape->fill(color.R8(), color.G8(), color.B8(), alpha);
}


void
ThorVGBackend::_ApplyGradientFill(tvg::Shape* shape, void* gradientHandle)
{
	if (gradientHandle == nullptr)
		return;

	auto gradient = static_cast<tvg::Fill*>(gradientHandle);
	shape->fill(tvg::cast<tvg::Fill>(gradient->duplicate()));

	if (fCurrentState.opacity < 1.0f)
		shape->opacity(static_cast<uint8_t>(fCurrentState.opacity * 255));
}


void
ThorVGBackend::_ApplyStroke(tvg::Shape* shape, const KosmColor& color,
	const KosmStrokeStyle& style)
{
	uint8_t alpha = static_cast<uint8_t>(color.a * fCurrentState.opacity * 255);

	shape->strokeWidth(style.width);
	shape->strokeFill(color.R8(), color.G8(), color.B8(), alpha);
	shape->strokeMiterlimit(style.miterLimit);
	shape->strokeCap(_ConvertLineCap(style.cap));
	shape->strokeJoin(_ConvertLineJoin(style.join));

	if (style.dashPattern != nullptr && style.dashCount > 0) {
		shape->strokeDash(style.dashPattern, style.dashCount, style.dashOffset);
	}
}


void
ThorVGBackend::_ApplyGradientStroke(tvg::Shape* shape, void* gradientHandle,
	const KosmStrokeStyle& style)
{
	if (gradientHandle == nullptr)
		return;

	auto gradient = static_cast<tvg::Fill*>(gradientHandle);

	shape->strokeWidth(style.width);
	shape->strokeFill(tvg::cast<tvg::Fill>(gradient->duplicate()));
	shape->strokeMiterlimit(style.miterLimit);
	shape->strokeCap(_ConvertLineCap(style.cap));
	shape->strokeJoin(_ConvertLineJoin(style.join));

	if (style.dashPattern != nullptr && style.dashCount > 0) {
		shape->strokeDash(style.dashPattern, style.dashCount, style.dashOffset);
	}

	if (fCurrentState.opacity < 1.0f)
		shape->opacity(static_cast<uint8_t>(fCurrentState.opacity * 255));
}


void
ThorVGBackend::_ApplyState(tvg::Shape* shape)
{
	// Transform
	if (!fCurrentState.transform.IsIdentity()) {
		tvg::Matrix m = {
			fCurrentState.transform.A(),
			fCurrentState.transform.B(),
			fCurrentState.transform.Tx(),
			fCurrentState.transform.C(),
			fCurrentState.transform.D(),
			fCurrentState.transform.Ty(),
			0, 0, 1
		};
		shape->transform(m);
	}

	// Blend mode
	shape->blend(_ConvertBlendMode(fCurrentState.blendMode));

	// Clip
	if (fCurrentState.hasClipRect) {
		auto clipper = _CreateClipShape();
		shape->clip(std::unique_ptr<tvg::Shape>(clipper));
	} else if (fCurrentState.clipPath != nullptr) {
		auto clipper = static_cast<tvg::Shape*>(
			fCurrentState.clipPath->duplicate().release());
		shape->clip(std::unique_ptr<tvg::Shape>(clipper));
	}
}


tvg::Shape*
ThorVGBackend::_CreateClipShape()
{
	auto clipper = tvg::Shape::gen();
	clipper->appendRect(
		fCurrentState.clipRect.x,
		fCurrentState.clipRect.y,
		fCurrentState.clipRect.width,
		fCurrentState.clipRect.height
	);
	return clipper.release();
}


void
ThorVGBackend::_DrawShadow(tvg::Shape* shape)
{
	auto shadowShape = tvg::cast<tvg::Shape>(shape->duplicate());

	shadowShape->fill(
		fCurrentState.shadowColor.R8(),
		fCurrentState.shadowColor.G8(),
		fCurrentState.shadowColor.B8(),
		fCurrentState.shadowColor.A8()
	);

	shadowShape->translate(fCurrentState.shadowOffsetX,
		fCurrentState.shadowOffsetY);

	fScene->push(std::move(shadowShape));
}


tvg::BlendMethod
ThorVGBackend::_ConvertBlendMode(kosm_blend_mode mode)
{
	switch (mode) {
		case KOSM_BLEND_MULTIPLY:
			return tvg::BlendMethod::Multiply;
		case KOSM_BLEND_SCREEN:
			return tvg::BlendMethod::Screen;
		case KOSM_BLEND_OVERLAY:
			return tvg::BlendMethod::Overlay;
		case KOSM_BLEND_DARKEN:
			return tvg::BlendMethod::Darken;
		case KOSM_BLEND_LIGHTEN:
			return tvg::BlendMethod::Lighten;
		case KOSM_BLEND_COLOR_DODGE:
			return tvg::BlendMethod::ColorDodge;
		case KOSM_BLEND_COLOR_BURN:
			return tvg::BlendMethod::ColorBurn;
		case KOSM_BLEND_HARD_LIGHT:
			return tvg::BlendMethod::HardLight;
		case KOSM_BLEND_SOFT_LIGHT:
			return tvg::BlendMethod::SoftLight;
		case KOSM_BLEND_DIFFERENCE:
			return tvg::BlendMethod::Difference;
		case KOSM_BLEND_EXCLUSION:
			return tvg::BlendMethod::Exclusion;
		case KOSM_BLEND_ADD:
			return tvg::BlendMethod::Add;
		default:
			return tvg::BlendMethod::Normal;
	}
}


tvg::StrokeCap
ThorVGBackend::_ConvertLineCap(kosm_line_cap cap)
{
	switch (cap) {
		case KOSM_LINE_CAP_ROUND:
			return tvg::StrokeCap::Round;
		case KOSM_LINE_CAP_SQUARE:
			return tvg::StrokeCap::Square;
		default:
			return tvg::StrokeCap::Butt;
	}
}


tvg::StrokeJoin
ThorVGBackend::_ConvertLineJoin(kosm_line_join join)
{
	switch (join) {
		case KOSM_LINE_JOIN_ROUND:
			return tvg::StrokeJoin::Round;
		case KOSM_LINE_JOIN_BEVEL:
			return tvg::StrokeJoin::Bevel;
		default:
			return tvg::StrokeJoin::Miter;
	}
}


tvg::FillSpread
ThorVGBackend::_ConvertSpread(kosm_gradient_spread spread)
{
	switch (spread) {
		case KOSM_GRADIENT_SPREAD_REFLECT:
			return tvg::FillSpread::Reflect;
		case KOSM_GRADIENT_SPREAD_REPEAT:
			return tvg::FillSpread::Repeat;
		default:
			return tvg::FillSpread::Pad;
	}
}


tvg::TextWrap
ThorVGBackend::_ConvertTextWrap(kosm_text_wrap wrap)
{
	switch (wrap) {
		case KOSM_TEXT_WRAP_WORD:
			return tvg::TextWrap::Word;
		case KOSM_TEXT_WRAP_CHARACTER:
			return tvg::TextWrap::Character;
		default:
			return tvg::TextWrap::None;
	}
}
