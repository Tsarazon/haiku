/*
 * Copyright 2005-2009, Stephan Aßmus <superstippi@gmx.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Blend2D text renderer implementation
 */

#include "Blend2dTextRenderer.h"

#include "GlobalSubpixelSettings.h"
#include "GlyphLayoutEngine.h"
#include "IntRect.h"

Blend2dTextRenderer::Blend2dTextRenderer(BLContext& context,
		const Transformable& transform)
	:
	fContext(context),
	fTransform(transform),
	fColor(0, 0, 0, 255),
	fHinted(true),
	fAntialias(true)
{
}

Blend2dTextRenderer::~Blend2dTextRenderer()
{
}

void
Blend2dTextRenderer::SetFont(const ServerFont& font)
{
	fFont = font;
}

void
Blend2dTextRenderer::SetHinting(bool hinting)
{
	fHinted = hinting;
}

void
Blend2dTextRenderer::SetAntialiasing(bool antialiasing)
{
	fAntialias = antialiasing;
}

void
Blend2dTextRenderer::SetColor(const BLRgba32& color)
{
	fColor = color;
}

// ============================================================================
// StringRenderer - внутренний класс для рендеринга
// ============================================================================

class Blend2dTextRenderer::StringRenderer {
public:
	StringRenderer(const IntRect& clippingFrame, bool dryRun,
			const Transformable& transform,
			const BPoint& transformOffset,
			BPoint* nextCharPos,
			Blend2dTextRenderer& renderer)
		:
		fTransform(transform),
		fTransformOffset(transformOffset),
		fClippingFrame(clippingFrame),
		fDryRun(dryRun),
		fBounds(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN),
		fNextCharPos(nextCharPos),
		fRenderer(renderer)
	{
	}

	bool NeedsVector()
	{
		return !fTransform.IsTranslationOnly();
	}

	void Start()
	{
		// Подготовка к рендерингу
	}

	void Finish(double x, double y)
	{
		if (!fDryRun) {
			// Рисуем подчеркивание и зачеркивание если нужно
			if ((fRenderer.fFont.Face() & B_UNDERSCORE_FACE) != 0)
				_DrawHorizontalLine(y + 2);

			if ((fRenderer.fFont.Face() & B_STRIKEOUT_FACE) != 0) {
				font_height fontHeight;
				fRenderer.fFont.GetHeight(fontHeight);
				_DrawHorizontalLine(y - (fontHeight.ascent + fontHeight.descent) / 4);
			}
		}

		if (fNextCharPos) {
			fNextCharPos->x = x;
			fNextCharPos->y = y;
			fTransform.Transform(fNextCharPos);
		}
	}

	void ConsumeEmptyGlyph(int32 index, uint32 charCode, double x, double y)
	{
		// Пустой глиф - ничего не делаем
	}

	bool ConsumeGlyph(int32 index, uint32 charCode, const GlyphCache* glyph,
		FontCacheEntry* entry, double x, double y, double advanceX,
		double advanceY)
	{
		if (glyph == NULL)
			return true;

		const BLBox& r = glyph->bounds;
		IntRect glyphBounds(int32(r.x0 + x), int32(r.y0 + y - 1),
			int32(r.x1 + x + 1), int32(r.y1 + y + 1));

		// Отслеживаем границы
		fBounds = fBounds | glyphBounds;

		if (fDryRun)
			return true;

		// Трансформация позиции глифа
		double transformedX = x + fTransformOffset.x;
		double transformedY = y + fTransformOffset.y;

		if (!NeedsVector()) {
			glyphBounds.OffsetBy(fTransformOffset);
		} else {
			glyphBounds = fTransform.TransformBounds(glyphBounds);
		}

		// Проверка clipping
		if (!fClippingFrame.Intersects(glyphBounds))
			return true;

		// Рендеринг глифа
		switch (glyph->data_type) {
			case glyph_data_mono:
			case glyph_data_gray8:
			case glyph_data_lcd:
				// Растровый глиф
				_RenderBitmapGlyph(glyph, transformedX, transformedY);
				break;

			case glyph_data_outline:
				// Векторный глиф
				_RenderVectorGlyph(entry, transformedX, transformedY);
				break;

			default:
				break;
		}

		return true;
	}

	IntRect Bounds() const
	{
		return fBounds;
	}

private:
	void _RenderBitmapGlyph(const GlyphCache* glyph, double x, double y)
	{
		// TODO: Восстановить BLImage из glyph->data
		// и отрендерить через fRenderer.fContext.blitImage()
		
		// Для Blend2D нужно создать BLImage из glyph->data
		// Формат зависит от glyph->data_type
		
		// Пример для gray8/lcd:
		// BLImage image;
		// if (image.create(width, height, BL_FORMAT_A8) == BL_SUCCESS) {
		//     BLImageData imageData;
		//     image.makeMutable(&imageData);
		//     memcpy(imageData.pixelData, glyph->data, dataSize);
		//     
		//     fRenderer.fContext.save();
		//     fRenderer.fContext.setFillStyle(fRenderer.fColor);
		//     fRenderer.fContext.blitImage(BLPoint(x + glyph->bounds.x0, 
		//                                           y + glyph->bounds.y0), image);
		//     fRenderer.fContext.restore();
		// }
	}

	void _RenderVectorGlyph(FontCacheEntry* entry, double x, double y)
	{
		// TODO: Восстановить BLPath из данных глифа
		// и отрендерить через fRenderer.fContext.fillPath()
		
		// Для Blend2D нужно получить BLPath из entry
		// const BLPath& path = entry->GetPath();
		// 
		// fRenderer.fContext.save();
		// fRenderer.fContext.translate(x, y);
		// fRenderer.fContext.setFillStyle(fRenderer.fColor);
		// fRenderer.fContext.fillPath(path);
		// fRenderer.fContext.restore();
	}

	void _DrawHorizontalLine(float y)
	{
		IntRect bounds = fBounds;
		BPoint left(bounds.left, y);
		BPoint right(bounds.right, y);
		fTransform.Transform(&left);
		fTransform.Transform(&right);

		BLPath path;
		path.moveTo(left.x, left.y);
		path.lineTo(right.x, right.y);

		fRenderer.fContext.setStrokeWidth(fRenderer.fFont.Size() / 12.0f);
		fRenderer.fContext.setStrokeStyle(fRenderer.fColor);
		fRenderer.fContext.strokePath(path);
	}

private:
	const Transformable&	fTransform;
	const BPoint&			fTransformOffset;
	const IntRect&			fClippingFrame;
	bool					fDryRun;
	IntRect					fBounds;
	BPoint*					fNextCharPos;
	Blend2dTextRenderer&	fRenderer;
};

// ============================================================================
// Методы рендеринга
// ============================================================================

BRect
Blend2dTextRenderer::RenderString(const char* string, uint32 length,
	const BPoint& baseLine, const BRect& clippingFrame, bool dryRun,
	BPoint* nextCharPos, const escapement_delta* delta,
	FontCacheReference* cacheReference)
{
	Transformable transform(fTransform);
	transform.TranslateBy(baseLine);

	BPoint transformOffset(0.0, 0.0);
	transform.Transform(&transformOffset);
	IntRect clippingIntFrame(clippingFrame);

	StringRenderer renderer(clippingIntFrame, dryRun, transform,
		transformOffset, nextCharPos, *this);

	GlyphLayoutEngine::LayoutGlyphs(renderer, fFont, string, length, INT32_MAX,
		delta, fFont.Spacing(), NULL, cacheReference);

	return transform.TransformBounds(renderer.Bounds());
}

BRect
Blend2dTextRenderer::RenderString(const char* string, uint32 length,
	const BPoint* offsets, const BRect& clippingFrame, bool dryRun,
	BPoint* nextCharPos, FontCacheReference* cacheReference)
{
	Transformable transform(fTransform);

	BPoint transformOffset(0.0, 0.0);
	transform.Transform(&transformOffset);
	IntRect clippingIntFrame(clippingFrame);

	StringRenderer renderer(clippingIntFrame, dryRun, transform,
		transformOffset, nextCharPos, *this);

	GlyphLayoutEngine::LayoutGlyphs(renderer, fFont, string, length, INT32_MAX,
		NULL, fFont.Spacing(), offsets, cacheReference);

	return transform.TransformBounds(renderer.Bounds());
}
