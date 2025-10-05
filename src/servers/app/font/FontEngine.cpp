/*
 * Copyright 2007, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Maxim Shemanarev <mcseemagg@yahoo.com>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Anthony Lee <don.anthony.lee@gmail.com>
 *		Andrej Spielmann, <andrej.spielmann@seh.ox.ac.uk>
 *		2025 Blend2D Migration
 */

#include "FontEngine.h"

#include FT_GLYPH_H
#include FT_OUTLINE_H

#include <stdio.h>

static const bool kFlipY = true;

// ============================================================================
// Вспомогательные функции конвертации
// ============================================================================

static inline double
int26p6_to_dbl(int p)
{
	return double(p) / 64.0;
}

static inline int
dbl_to_int26p6(double p)
{
	return int(p * 64.0 + 0.5);
}

// Конвертация FT_Outline в BLPath
static bool
decompose_ft_outline_to_blend2d(const FT_Outline& outline, bool flip_y, 
                                 BLPath& path)
{
	FT_Vector v_last;
	FT_Vector v_control;
	FT_Vector v_start;
	FT_Vector* point;
	FT_Vector* limit;
	char* tags;

	int first = 0;

	for (int n = 0; n < outline.n_contours; n++) {
		int last = outline.contours[n];
		limit = outline.points + last;

		v_start = outline.points[first];
		v_last = outline.points[last];
		v_control = v_start;

		point = outline.points + first;
		tags = outline.tags + first;
		char tag = FT_CURVE_TAG(tags[0]);

		// Контур не может начинаться с кубической контрольной точки
		if (tag == FT_CURVE_TAG_CUBIC)
			return false;

		// Проверяем первую точку
		if (tag == FT_CURVE_TAG_CONIC) {
			if (FT_CURVE_TAG(outline.tags[last]) == FT_CURVE_TAG_ON) {
				v_start = v_last;
				limit--;
			} else {
				v_start.x = (v_start.x + v_last.x) / 2;
				v_start.y = (v_start.y + v_last.y) / 2;
				v_last = v_start;
			}
			point--;
			tags--;
		}

		double x = int26p6_to_dbl(v_start.x);
		double y = int26p6_to_dbl(v_start.y);
		if (flip_y) y = -y;
		path.moveTo(x, y);

		while (point < limit) {
			point++;
			tags++;
			tag = FT_CURVE_TAG(tags[0]);

			switch(tag) {
				case FT_CURVE_TAG_ON: {
					x = int26p6_to_dbl(point->x);
					y = int26p6_to_dbl(point->y);
					if (flip_y) y = -y;
					path.lineTo(x, y);
					continue;
				}

				case FT_CURVE_TAG_CONIC: {
					v_control.x = point->x;
					v_control.y = point->y;

				Do_Conic:
					if (point < limit) {
						FT_Vector vec;
						FT_Vector v_middle;

						point++;
						tags++;
						tag = FT_CURVE_TAG(tags[0]);

						vec.x = point->x;
						vec.y = point->y;

						if (tag == FT_CURVE_TAG_ON) {
							double x1 = int26p6_to_dbl(v_control.x);
							double y1 = int26p6_to_dbl(v_control.y);
							double x2 = int26p6_to_dbl(vec.x);
							double y2 = int26p6_to_dbl(vec.y);
							if (flip_y) { y1 = -y1; y2 = -y2; }
							path.quadTo(x1, y1, x2, y2);
							continue;
						}

						if (tag != FT_CURVE_TAG_CONIC)
							return false;

						v_middle.x = (v_control.x + vec.x) / 2;
						v_middle.y = (v_control.y + vec.y) / 2;

						double x1 = int26p6_to_dbl(v_control.x);
						double y1 = int26p6_to_dbl(v_control.y);
						double x2 = int26p6_to_dbl(v_middle.x);
						double y2 = int26p6_to_dbl(v_middle.y);
						if (flip_y) { y1 = -y1; y2 = -y2; }
						path.quadTo(x1, y1, x2, y2);

						v_control = vec;
						goto Do_Conic;
					}

					double x1 = int26p6_to_dbl(v_control.x);
					double y1 = int26p6_to_dbl(v_control.y);
					double x2 = int26p6_to_dbl(v_start.x);
					double y2 = int26p6_to_dbl(v_start.y);
					if (flip_y) { y1 = -y1; y2 = -y2; }
					path.quadTo(x1, y1, x2, y2);
					goto Close;
				}

				default: { // FT_CURVE_TAG_CUBIC
					FT_Vector vec1, vec2;

					if (point + 1 > limit || FT_CURVE_TAG(tags[1]) != FT_CURVE_TAG_CUBIC)
						return false;

					vec1.x = point[0].x;
					vec1.y = point[0].y;
					vec2.x = point[1].x;
					vec2.y = point[1].y;

					point += 2;
					tags += 2;

					if (point <= limit) {
						FT_Vector vec;
						vec.x = point->x;
						vec.y = point->y;

						double x1 = int26p6_to_dbl(vec1.x);
						double y1 = int26p6_to_dbl(vec1.y);
						double x2 = int26p6_to_dbl(vec2.x);
						double y2 = int26p6_to_dbl(vec2.y);
						double x3 = int26p6_to_dbl(vec.x);
						double y3 = int26p6_to_dbl(vec.y);
						if (flip_y) { y1 = -y1; y2 = -y2; y3 = -y3; }
						path.cubicTo(x1, y1, x2, y2, x3, y3);
						continue;
					}

					double x1 = int26p6_to_dbl(vec1.x);
					double y1 = int26p6_to_dbl(vec1.y);
					double x2 = int26p6_to_dbl(vec2.x);
					double y2 = int26p6_to_dbl(vec2.y);
					double x3 = int26p6_to_dbl(v_start.x);
					double y3 = int26p6_to_dbl(v_start.y);
					if (flip_y) { y1 = -y1; y2 = -y2; y3 = -y3; }
					path.cubicTo(x1, y1, x2, y2, x3, y3);
					goto Close;
				}
			}
		}

		path.close();
	Close:
		first = last + 1;
	}

	return true;
}

// Конвертация FT_Bitmap в BLImage
// КРИТИЧНО: для LCD (субпиксельного) используем перцептивный alpha
static BLImage
convert_ft_bitmap_to_blend2d(const FT_Bitmap& bitmap, bool flip_y, 
                              glyph_data_type dataType)
{
	BLImage image;

	if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
		// Монохромный bitmap - 1 бит на пиксель
		if (image.create(bitmap.width, bitmap.rows, BL_FORMAT_A8) != BL_SUCCESS)
			return image;

		BLImageData imageData;
		if (image.getData(&imageData) != BL_SUCCESS)
			return BLImage();

		uint8_t* dst = static_cast<uint8_t*>(imageData.pixelData);
		intptr_t dstStride = imageData.stride;

		const uint8_t* src = bitmap.buffer;
		int srcPitch = bitmap.pitch;

		if (flip_y) {
			src += srcPitch * (bitmap.rows - 1);
			srcPitch = -srcPitch;
		}

		for (unsigned int y = 0; y < bitmap.rows; y++) {
			uint8_t* dstRow = dst + y * dstStride;
			const uint8_t* srcRow = src + y * srcPitch;

			for (unsigned int x = 0; x < bitmap.width; x++) {
				uint8_t byte = srcRow[x / 8];
				uint8_t bit = (byte >> (7 - (x % 8))) & 1;
				dstRow[x] = bit ? 255 : 0;
			}
		}

	} else if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
		// Grayscale bitmap
		if (image.create(bitmap.width, bitmap.rows, BL_FORMAT_A8) != BL_SUCCESS)
			return image;

		BLImageData imageData;
		if (image.getData(&imageData) != BL_SUCCESS)
			return BLImage();

		uint8_t* dst = static_cast<uint8_t*>(imageData.pixelData);
		intptr_t dstStride = imageData.stride;

		const uint8_t* src = bitmap.buffer;
		int srcPitch = bitmap.pitch;

		if (flip_y) {
			src += srcPitch * (bitmap.rows - 1);
			srcPitch = -srcPitch;
		}

		for (unsigned int y = 0; y < bitmap.rows; y++) {
			memcpy(dst + y * dstStride, src + y * srcPitch, bitmap.width);
		}

	} else if (bitmap.pixel_mode == FT_PIXEL_MODE_LCD) {
		// LCD bitmap - RGB субпиксели
		// КРИТИЧНО: Конвертируем в перцептивно-взвешенный alpha
		// Формула: alpha = 0.299*R + 0.587*G + 0.114*B
		
		unsigned int width = bitmap.width / 3;
		if (image.create(width, bitmap.rows, BL_FORMAT_A8) != BL_SUCCESS)
			return image;

		BLImageData imageData;
		if (image.getData(&imageData) != BL_SUCCESS)
			return BLImage();

		uint8_t* dst = static_cast<uint8_t*>(imageData.pixelData);
		intptr_t dstStride = imageData.stride;

		const uint8_t* src = bitmap.buffer;
		int srcPitch = bitmap.pitch;

		if (flip_y) {
			src += srcPitch * (bitmap.rows - 1);
			srcPitch = -srcPitch;
		}

		for (unsigned int y = 0; y < bitmap.rows; y++) {
			const uint8_t* srcRow = src + y * srcPitch;
			uint8_t* dstRow = dst + y * dstStride;

			for (unsigned int x = 0; x < width; x++) {
				uint8_t r = srcRow[x * 3 + 0];
				uint8_t g = srcRow[x * 3 + 1];
				uint8_t b = srcRow[x * 3 + 2];

				// Перцептивно-взвешенный alpha (целочисленная арифметика)
				// Коэффициенты: R=0.299, G=0.587, B=0.114
				uint32_t alpha = (299 * r + 587 * g + 114 * b) / 1000;
				dstRow[x] = static_cast<uint8_t>(alpha);
			}
		}
	}

	return image;
}

// ============================================================================
// Реализация FontEngine
// ============================================================================

FontEngine::FontEngine()
	:
	fLastError(0),
	fLibraryInitialized(false),
	fLibrary(nullptr),
	fFace(nullptr),
	fGlyphRendering(glyph_ren_native_gray8),
	fHinting(true),
	fDataType(glyph_data_invalid),
	fAdvanceX(0.0),
	fAdvanceY(0.0),
	fInsetLeft(0.0),
	fInsetRight(0.0)
{
	fLastError = FT_Init_FreeType(&fLibrary);
	if (fLastError == 0)
		fLibraryInitialized = true;
}

FontEngine::~FontEngine()
{
	if (fFace)
		FT_Done_Face(fFace);

	if (fLibraryInitialized)
		FT_Done_FreeType(fLibrary);
}

unsigned
FontEngine::CountFaces() const
{
	if (fFace)
		return fFace->num_faces;
	return 0;
}

uint32
FontEngine::GlyphIndexForGlyphCode(uint32 glyphCode) const
{
	return FT_Get_Char_Index(fFace, glyphCode);
}

bool
FontEngine::PrepareGlyph(uint32 glyphIndex)
{
	FT_Int32 loadFlags = fHinting ? FT_LOAD_DEFAULT : FT_LOAD_NO_HINTING;
	if (fGlyphRendering == glyph_ren_lcd)
		loadFlags |= FT_LOAD_TARGET_LCD;
	else
		loadFlags |= FT_LOAD_TARGET_NORMAL;

	// Загружаем без хинтинга и масштаба для точных метрик
	fLastError = FT_Load_Glyph(fFace, glyphIndex,
		loadFlags | FT_LOAD_NO_HINTING | FT_LOAD_NO_SCALE);

	FT_UShort units_per_EM = fFace->units_per_EM;
	if (!FT_IS_SCALABLE(fFace))
		units_per_EM = 1;

	fPreciseAdvanceX = (double)fFace->glyph->advance.x / units_per_EM;
	fPreciseAdvanceY = (double)fFace->glyph->advance.y / units_per_EM;

	// Загружаем с хинтингом
	fLastError = FT_Load_Glyph(fFace, glyphIndex, loadFlags);
	if (fLastError != 0)
		return false;

	fAdvanceX = int26p6_to_dbl(fFace->glyph->advance.x);
	fAdvanceY = int26p6_to_dbl(fFace->glyph->advance.y);

	fInsetLeft = int26p6_to_dbl(fFace->glyph->metrics.horiBearingX);
	fInsetRight = int26p6_to_dbl(fFace->glyph->metrics.horiBearingX
		+ fFace->glyph->metrics.width - fFace->glyph->metrics.horiAdvance);

	switch(fGlyphRendering) {
		case glyph_ren_native_mono:
			fLastError = FT_Render_Glyph(fFace->glyph, FT_RENDER_MODE_MONO);
			if (fLastError == 0) {
				fImage = convert_ft_bitmap_to_blend2d(fFace->glyph->bitmap,
					kFlipY, glyph_data_mono);
				
				double left = fFace->glyph->bitmap_left;
				double top = kFlipY ? -fFace->glyph->bitmap_top : fFace->glyph->bitmap_top;
				fBounds = BLBox(left, top, 
					left + fFace->glyph->bitmap.width,
					top + fFace->glyph->bitmap.rows);
				
				fDataType = glyph_data_mono;
				return true;
			}
			break;

		case glyph_ren_native_gray8:
			fLastError = FT_Render_Glyph(fFace->glyph, FT_RENDER_MODE_NORMAL);
			if (fLastError == 0) {
				fImage = convert_ft_bitmap_to_blend2d(fFace->glyph->bitmap,
					kFlipY, glyph_data_gray8);
				
				double left = fFace->glyph->bitmap_left;
				double top = kFlipY ? -fFace->glyph->bitmap_top : fFace->glyph->bitmap_top;
				fBounds = BLBox(left, top,
					left + fFace->glyph->bitmap.width,
					top + fFace->glyph->bitmap.rows);
				
				fDataType = glyph_data_gray8;
				return true;
			}
			break;

		case glyph_ren_lcd:
			fLastError = FT_Render_Glyph(fFace->glyph, FT_RENDER_MODE_LCD);
			if (fLastError == 0) {
				// Конвертация LCD → перцептивный alpha
				fImage = convert_ft_bitmap_to_blend2d(fFace->glyph->bitmap,
					kFlipY, glyph_data_lcd);
				
				double left = fFace->glyph->bitmap_left;
				double top = kFlipY ? -fFace->glyph->bitmap_top : fFace->glyph->bitmap_top;
				fBounds = BLBox(left, top,
					left + (fFace->glyph->bitmap.width / 3),
					top + fFace->glyph->bitmap.rows);
				
				fDataType = glyph_data_lcd;
				return true;
			}
			break;

		case glyph_ren_outline:
			fPath.reset();
			if (decompose_ft_outline_to_blend2d(fFace->glyph->outline, kFlipY, fPath)) {
				BLBox box;
				fPath.getBoundingBox(&box);
				fBounds = box;
				fDataType = glyph_data_outline;
				return true;
			}
			break;
	}

	return false;
}

uint32
FontEngine::DataSize() const
{
	switch (fDataType) {
		case glyph_data_outline: {
			// Оценка размера для сериализованного пути
			size_t vertexCount = fPath.size();
			return vertexCount * sizeof(BLPoint) + 1024; // +буфер
		}
		case glyph_data_mono:
		case glyph_data_gray8:
		case glyph_data_lcd: {
			BLImageData imageData;
			if (fImage.getData(&imageData) == BL_SUCCESS)
				return imageData.stride * fImage.height();
			return 0;
		}
		default:
			return 0;
	}
}

void
FontEngine::WriteGlyphTo(uint8* data) const
{
	if (data == nullptr)
		return;

	switch (fDataType) {
		case glyph_data_outline: {
			// Сериализация BLPath - выполняется в FontCacheEntry
			break;
		}
		case glyph_data_mono:
		case glyph_data_gray8:
		case glyph_data_lcd: {
			BLImageData imageData;
			if (fImage.getData(&imageData) == BL_SUCCESS) {
				size_t dataSize = imageData.stride * fImage.height();
				memcpy(data, imageData.pixelData, dataSize);
			}
			break;
		}
		default:
			break;
	}
}

bool
FontEngine::GetKerning(uint32 first, uint32 second, double* x, double* y)
{
	if (fFace && first && second && FT_HAS_KERNING(fFace)) {
		FT_Vector delta;
		FT_Get_Kerning(fFace, first, second, FT_KERNING_DEFAULT, &delta);

		double dx = int26p6_to_dbl(delta.x);
		double dy = int26p6_to_dbl(delta.y);

		*x += dx;
		*y += dy;

		return true;
	}
	return false;
}

bool
FontEngine::Init(const char* fontFilePath, unsigned faceIndex, double size,
	FT_Encoding charMap, glyph_rendering ren_type, bool hinting,
	const void* fontFileBuffer, const long fontFileBufferSize)
{
	if (!fLibraryInitialized)
		return false;

	fHinting = hinting;
	fLastError = 0;

	if (fFace)
		FT_Done_Face(fFace);

	if (fontFileBuffer && fontFileBufferSize) {
		fLastError = FT_New_Memory_Face(fLibrary,
			(const FT_Byte*)fontFileBuffer, fontFileBufferSize,
			faceIndex, &fFace);
	} else {
		fLastError = FT_New_Face(fLibrary, fontFilePath, faceIndex, &fFace);
	}

	if (fLastError != 0)
		return false;

	switch(ren_type) {
		case glyph_ren_native_mono:
			fGlyphRendering = glyph_ren_native_mono;
			break;

		case glyph_ren_native_gray8:
			fGlyphRendering = glyph_ren_native_gray8;
			break;

		case glyph_ren_lcd:
			fGlyphRendering = glyph_ren_lcd;
			break;

		case glyph_ren_outline:
			if (FT_IS_SCALABLE(fFace))
				fGlyphRendering = glyph_ren_outline;
			else
				fGlyphRendering = glyph_ren_native_gray8;
			break;
	}

	FT_Set_Pixel_Sizes(fFace,
		unsigned(size * 64.0) >> 6,
		unsigned(size * 64.0) >> 6);

	if (charMap != FT_ENCODING_NONE) {
		fLastError = FT_Select_Charmap(fFace, charMap);
	} else {
		if (FT_Select_Charmap(fFace, FT_ENCODING_UNICODE) != 0)
			fLastError = FT_Select_Charmap(fFace, FT_ENCODING_NONE);
	}

	return fLastError == 0;
}
