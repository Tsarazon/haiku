/*
 * Copyright 2005-2009, Stephan Aßmus <superstippi@gmx.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Blend2D text renderer - replaces AGGTextRenderer
 */
#ifndef BLEND2D_TEXT_RENDERER_H
#define BLEND2D_TEXT_RENDERER_H

#include <blend2d.h>

#include "FontCacheEntry.h"
#include "ServerFont.h"
#include "Transformable.h"

class FontCacheReference;

class Blend2dTextRenderer {
public:
								Blend2dTextRenderer(
									BLContext& context,
									const Transformable& transform);
	virtual						~Blend2dTextRenderer();

			void				SetFont(const ServerFont& font);
	inline	const ServerFont&	Font() const
									{ return fFont; }

			void				SetHinting(bool hinting);
			bool				Hinting() const
									{ return fHinted; }

			void				SetAntialiasing(bool antialiasing);
			bool				Antialiasing() const
									{ return fAntialias; }

			void				SetColor(const BLRgba32& color);

			BRect				RenderString(const char* utf8String,
									uint32 length, const BPoint& baseLine,
									const BRect& clippingFrame, bool dryRun,
									BPoint* nextCharPos,
									const escapement_delta* delta,
									FontCacheReference* cacheReference);

			BRect				RenderString(const char* utf8String,
									uint32 length, const BPoint* offsets,
									const BRect& clippingFrame, bool dryRun,
									BPoint* nextCharPos,
									FontCacheReference* cacheReference);

private:
	class StringRenderer;
	friend class StringRenderer;

	BLContext&					fContext;
	Transformable				fTransform;
	ServerFont					fFont;
	BLRgba32					fColor;
	bool						fHinted;
	bool						fAntialias;
};

#endif // BLEND2D_TEXT_RENDERER_H
