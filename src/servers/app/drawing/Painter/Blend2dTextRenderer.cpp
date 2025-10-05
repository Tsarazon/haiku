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
				_RenderVectorGlyph(glyph, transformedX, transformedY);
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
		// 1. Получаем BLImage из glyph используя существующий метод
		BLImage image = glyph->GetImage();
		
		if (image.empty())
			return;
		
		// 2. Вычисляем позицию (учитываем bounds глифа)
		BLPoint pos(x + glyph->bounds.x0, y + glyph->bounds.y0);
		
		// 3. Устанавливаем режим композиции и цвет
		fRenderer.fContext.save();
		fRenderer.fContext.setCompOp(BL_COMP_OP_SRC_OVER);
		fRenderer.fContext.setFillStyle(fRenderer.fColor);
		
		// 4. Рисуем глиф как маску альфа-канала
		// Для альфа-изображений используем fillMaskI для применения цвета
		if (glyph->data_type == glyph_data_gray8 || 
		    glyph->data_type == glyph_data_lcd ||
		    glyph->data_type == glyph_data_mono) {
			
			// Создаём маску из альфа-канала
			fRenderer.fContext.fillMaskI(
				BLPointI(int(pos.x), int(pos.y)),
				image,
				BLRectI(0, 0, image.width(), image.height())
			);
		}
		
		fRenderer.fContext.restore();
	}

	void _RenderVectorGlyph(const GlyphCache* glyph, double x, double y)
	{
		// 1. Получаем BLPath из glyph используя существующий метод
		BLPath path = glyph->GetPath();
		
		if (path.empty())
			return;
		
		// 2. Применяем трансляцию для позиционирования глифа
		BLMatrix2D matrix = BLMatrix2D::makeTranslation(x, y);
		
		// 3. Если есть трансформация, комбинируем её
		if (!fTransform.IsIdentity()) {
			matrix.postTransform(fTransform.Matrix());
		}
		
		// 4. Рендерим путь
		fRenderer.fContext.save();
		fRenderer.fContext.setMatrix(matrix);
		fRenderer.fContext.setFillStyle(fRenderer.fColor);
		fRenderer.fContext.fillPath(path);
		fRenderer.fContext.restore();
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
