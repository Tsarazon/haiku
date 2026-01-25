/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_FONT_HPP
#define _KOSM_FONT_HPP

#include <KosmGeometry.hpp>
#include <SupportDefs.h>

enum kosm_text_align {
	KOSM_TEXT_ALIGN_LEFT = 0,
	KOSM_TEXT_ALIGN_CENTER,
	KOSM_TEXT_ALIGN_RIGHT
};

enum kosm_text_baseline {
	KOSM_TEXT_BASELINE_TOP = 0,
	KOSM_TEXT_BASELINE_MIDDLE,
	KOSM_TEXT_BASELINE_BOTTOM,
	KOSM_TEXT_BASELINE_ALPHABETIC
};

enum kosm_text_wrap {
	KOSM_TEXT_WRAP_NONE = 0,
	KOSM_TEXT_WRAP_WORD,
	KOSM_TEXT_WRAP_CHARACTER,
	KOSM_TEXT_WRAP_SMART,
	KOSM_TEXT_WRAP_ELLIPSIS
};


class KosmFont {
public:
							KosmFont();
							KosmFont(const char* family, float size);
							KosmFont(const KosmFont& other);
							~KosmFont();

			KosmFont&		operator=(const KosmFont& other);

	// Font loading (static, cached)
	static	status_t		LoadFont(const char* path);
	static	status_t		LoadFont(const char* name, const void* data,
								size_t size);
	static	status_t		UnloadFont(const char* path);

	// Font selection
			void			SetFamily(const char* family);
			const char*		Family() const;

			void			SetSize(float size);
			float			Size() const;

	// Style
			void			SetBold(bool bold);
			bool			IsBold() const;

			void			SetItalic(bool italic);
			bool			IsItalic() const;

			void			SetItalicShear(float shear);
			float			ItalicShear() const;

	// Spacing
			void			SetLetterSpacing(float spacing);
			float			LetterSpacing() const;

			void			SetLineSpacing(float spacing);
			float			LineSpacing() const;

	// Metrics
			float			Ascent() const;
			float			Descent() const;
			float			Leading() const;
			float			LineHeight() const;

	// Text measurement
			float			MeasureWidth(const char* text) const;
			KosmRect		MeasureBounds(const char* text) const;

	// Internal
			void*			NativeHandle() const;

private:
			struct Data;
			Data*			fData;
};


struct KosmTextStyle {
	KosmFont			font;
	kosm_text_align		align;
	kosm_text_baseline	baseline;
	kosm_text_wrap		wrap;
	float				maxWidth;
	float				maxHeight;

	KosmTextStyle()
		:
		font(),
		align(KOSM_TEXT_ALIGN_LEFT),
		baseline(KOSM_TEXT_BASELINE_ALPHABETIC),
		wrap(KOSM_TEXT_WRAP_NONE),
		maxWidth(0),
		maxHeight(0)
	{
	}

	KosmTextStyle(const KosmFont& f)
		:
		font(f),
		align(KOSM_TEXT_ALIGN_LEFT),
		baseline(KOSM_TEXT_BASELINE_ALPHABETIC),
		wrap(KOSM_TEXT_WRAP_NONE),
		maxWidth(0),
		maxHeight(0)
	{
	}
};

#endif /* _KOSM_FONT_HPP */
