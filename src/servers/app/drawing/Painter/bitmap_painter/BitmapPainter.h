/*
 * Copyright 2005-2007, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2015, Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef BITMAP_PAINTER_H
#define BITMAP_PAINTER_H

#include <AutoDeleter.h>
#include <blend2d.h>

#include "Painter.h"


class Painter::BitmapPainter {
public:
								BitmapPainter(const Painter* painter,
									const ServerBitmap* bitmap,
									uint32 options);

			void				Draw(const BRect& sourceRect,
									const BRect& destinationRect);

private:
			bool				_DetermineTransform(
									BRect sourceRect,
									const BRect& destinationRect);

			BLFormat			_ConvertToBLFormat(color_space cs);
			void				_ConvertColorSpace(BLImage& outImage);
			void				_SetupCompOp(BLContext& ctx);

			template<typename sourcePixel>
			void				_TransparentMagicToAlpha(sourcePixel *buffer,
									uint32 width, uint32 height,
									uint32 sourceBytesPerRow,
									sourcePixel transparentMagic,
									BBitmap *output);

private:
			const Painter*		fPainter;
			status_t			fStatus;
			BLImage				fBLImage;
			BRect				fBitmapBounds;
			color_space			fColorSpace;
			uint32				fOptions;

			BRect				fDestinationRect;
			double				fScaleX;
			double				fScaleY;
			BPoint				fOffset;
};


#endif // BITMAP_PAINTER_H
