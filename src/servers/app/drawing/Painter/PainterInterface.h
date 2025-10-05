/*
 * Copyright 2005-2007, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>
 * Copyright 2025, Haiku Blend2D Migration
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef PAINTER_INTERFACE_H
#define PAINTER_INTERFACE_H

#include "defines.h"
#include <blend2d.h>

class PatternHandler;

// ============================================================================
// PainterInterface - замена PainterAggInterface для Blend2D
// ============================================================================

struct PainterInterface {
	PainterInterface(PatternHandler& patternHandler)
	:
	fBLContext(),
	fPixelFormat(fBLImage, fBLContext, &patternHandler),
	fPath(),
	fBLImage(),
	fPatternHandler(&patternHandler),
	fImageValid(false),
	fContextValid(false)
	{
	}

	~PainterInterface()
	{
		if (fContextValid) {
			fBLContext.end();
			fContextValid = false;
		}
	}

	// ========================================================================
	// Инициализация (вызывается из Painter::AttachToBuffer)
	// ========================================================================

	bool Init(uint32 width, uint32 height)
	{
		// Создаем изображение
		BLResult result = fBLImage.create(width, height, BL_FORMAT_PRGB32);
		if (result != BL_SUCCESS)
			return false;

		fImageValid = true;

		// Инициализируем контекст
		result = fBLContext.begin(fBLImage);
		if (result != BL_SUCCESS) {
			fBLImage.reset();
			fImageValid = false;
			return false;
		}

		fContextValid = true;

		// Настройки по умолчанию
		fBLContext.setHint(BL_CONTEXT_HINT_RENDERING_QUALITY,
						   BL_RENDERING_QUALITY_ANTIALIAS);
		fBLContext.setCompOp(BL_COMP_OP_SRC_OVER);

		return true;
	}

	// Прикрепление к существующему буферу (для RenderingBuffer)
	bool AttachToBuffer(uint8* bits, uint32 width, uint32 height,
						int32 bytesPerRow)
	{
		if (fContextValid) {
			fBLContext.end();
			fContextValid = false;
		}

		// Создаем BLImage из внешнего буфера
		BLResult result = fBLImage.createFromData(
			width, height,
			BL_FORMAT_PRGB32,
			bits,
			bytesPerRow
		);

		if (result != BL_SUCCESS)
			return false;

		fImageValid = true;

		// Инициализируем контекст
		result = fBLContext.begin(fBLImage);
		if (result != BL_SUCCESS) {
			fBLImage.reset();
			fImageValid = false;
			return false;
		}

		fContextValid = true;
		return true;
	}

	// ========================================================================
	// BLEND2D ОБЪЕКТЫ
	// ========================================================================

	// Основные объекты Blend2D (замена AGG объектов)
	BLContext			fBLContext;		// Главный объект рендеринга
	// (заменяет fRasterizer, fRenderer,
	//  fScanline, fBaseRenderer)

	// PixelFormat (уже адаптирован для Blend2D в PixelFormat.h/cpp)
	pixfmt				fPixelFormat;

	// Геометрия
	BLPath				fPath;			// Путь для рисования
	// (заменяет agg::path_storage)

	// ПРИМЕЧАНИЕ: fCurve (agg::conv_curve) не нужен в Blend2D,
	// так как кривые встроены в BLPath

	// ========================================================================
	// ВНУТРЕННИЕ ОБЪЕКТЫ
	// ========================================================================

	BLImage				fBLImage;		// Целевое изображение
	// (заменяет agg::rendering_buffer)

	PatternHandler*		fPatternHandler;// Обработчик паттернов

	// ========================================================================
	// ЗАГЛУШКИ ДЛЯ СОВМЕСТИМОСТИ
	// (эти объекты не нужны в Blend2D, но код может к ним обращаться)
	// ========================================================================

	// В AGG были отдельные объекты для subpixel рендеринга:
	// - fSubpixRasterizer
	// - fSubpixRenderer
	// - fSubpixPackedScanline
	// - fSubpixUnpackedScanline
	//
	// В Blend2D всё это делается через BLContext!
	// Subpixel antialiasing управляется через:
	// - BL_CONTEXT_HINT_RENDERING_QUALITY
	// - Наша реализация в Blend2DDrawingModeSUBPIX.h использует
	//   простое усреднение: alpha_avg = (R + G + B) / 3

	// В AGG были объекты для alpha masking:
	// - fMaskedUnpackedScanline
	// - fClippedAlphaMask
	//
	// В Blend2D alpha masking делается через:
	// - BLContext::setCompOp() с масками
	// Эти указатели оставлены для совместимости с текущим кодом
	void*				fMaskedUnpackedScanline;
	void*				fClippedAlphaMask;

private:
	bool				fImageValid;
	bool				fContextValid;
};

#endif // PAINTER_INTERFACE_H
