/*
 * Copyright 2007, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Maxim Shemanarev <mcseemagg@yahoo.com>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Andrej Spielmann, <andrej.spielmann@seh.ox.ac.uk>
 *		2025 Blend2D Migration
 */

#ifndef FONT_ENGINE_H
#define FONT_ENGINE_H

#include <SupportDefs.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <blend2d.h>

enum glyph_rendering {
	glyph_ren_native_mono,
	glyph_ren_native_gray8,
	glyph_ren_outline,
	glyph_ren_lcd  // заменяет glyph_ren_subpix
};

enum glyph_data_type {
	glyph_data_invalid	= 0,
	glyph_data_mono		= 1,
	glyph_data_gray8	= 2,
	glyph_data_outline	= 3,
	glyph_data_lcd		= 4  // заменяет glyph_data_subpix
};

class FontEngine {
 public:
	// Адаптеры для совместимости с существующим кодом
	typedef BLPath							PathAdapter;
	
								FontEngine();
	virtual						~FontEngine();

			bool				Init(const char* fontFilePath,
									unsigned face_index, double size,
									FT_Encoding char_map,
									glyph_rendering ren_type,
									bool hinting,
									const void* fontFileBuffer = NULL,
									const long fontFileBufferSize = 0);

			int					LastError() const
									{ return fLastError; }
			unsigned			CountFaces() const;
			bool				Hinting() const
									{ return fHinting; }

			uint32				GlyphIndexForGlyphCode(uint32 glyphCode) const;
			bool				PrepareGlyph(uint32 glyphIndex);

			uint32				DataSize() const;
			glyph_data_type		DataType() const
									{ return fDataType; }
			BLBox				Bounds() const
									{ return fBounds; }
			double				AdvanceX() const
									{ return fAdvanceX; }
			double				AdvanceY() const
									{ return fAdvanceY; }
			double				PreciseAdvanceX() const
									{ return fPreciseAdvanceX; }
			double				PreciseAdvanceY() const
									{ return fPreciseAdvanceY; }
			double				InsetLeft() const
									{ return fInsetLeft; }
			double				InsetRight() const
									{ return fInsetRight; }

			void				WriteGlyphTo(uint8* data) const;
			bool				GetKerning(uint32 first, uint32 second,
									double* x, double* y);

			// Методы для получения данных глифов
			const BLPath&		Path() const { return fPath; }
			const BLImage&		Image() const { return fImage; }

 private:
			// disallowed stuff:
								FontEngine(const FontEngine&);
			const FontEngine&	operator=(const FontEngine&);

			int					fLastError;
			bool				fLibraryInitialized;
			FT_Library			fLibrary;
			FT_Face				fFace;

			glyph_rendering		fGlyphRendering;
			bool				fHinting;

			// Данные глифа
			glyph_data_type		fDataType;
			BLBox				fBounds;
			double				fAdvanceX;
			double				fAdvanceY;
			double				fPreciseAdvanceX;
			double				fPreciseAdvanceY;
			double				fInsetLeft;
			double				fInsetRight;

			// blend2d структуры для хранения данных глифа
			BLPath				fPath;			// для векторных глифов
			BLImage				fImage;			// для растровых глифов
};

#endif // FONT_ENGINE_H
