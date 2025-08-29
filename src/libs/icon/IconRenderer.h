/*
 * Copyright 2006-2007, 2023, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Zardshard
 */
#ifndef ICON_RENDERER_H
#define ICON_RENDERER_H


#include <blend2d.h>

#include "IconBuild.h"
#include "Transformable.h"


class BBitmap;
class BRect;


_BEGIN_ICON_NAMESPACE


class Icon;

// Blend2D replacements for AGG types
typedef BLImage                             RenderingBuffer;
typedef BLFormat                            PixelFormat;
typedef BLFormat                            PixelFormatPre;
typedef BLContext                           BaseRenderer;
typedef BLContext                           BaseRendererPre;

typedef BLContext                           Scanline;
typedef BLContext                           BinaryScanline;
typedef BLContext                           SpanAllocator;
typedef BLContext                           CompoundRasterizer;

class IconRenderer {
 public:
								IconRenderer(BBitmap* bitmap);
	virtual						~IconRenderer();

			void				SetIcon(const Icon* icon);

#ifdef ICON_O_MATIC
			void				Render(bool showReferences);
			void				Render(const BRect& area, bool showReferences);
#else
			void				Render();
			void				Render(const BRect& area);
#endif // ICON_O_MATIC

			void				SetScale(double scale);
			void				SetBackground(const BBitmap* background);
									// background is not copied,
									// ownership stays with the caller
									// colorspace and size need to
									// be the same as bitmap passed
									// to constructor
			void				SetBackground(const BLRgba32& color);
									// used when no background bitmap
									// is set

			void				Demultiply();

 private:
		class StyleHandler;

#ifdef ICON_O_MATIC
			void				_Render(const BRect& area, bool showReferences);
#else
			void				_Render(const BRect& area);
#endif // ICON_O_MATIC
			void				_CommitRenderPass(StyleHandler& styleHandler,
									bool reset = true);

			BBitmap*			fBitmap;
			const BBitmap*		fBackground;
			BLRgba32			fBackgroundColor;
			const Icon*			fIcon;

			// Blend2D rendering objects
			BLImage				fRenderingImage;
			BLContext			fContext;
			BLFormat			fPixelFormat;

			Transformable		fGlobalTransform;
};


_END_ICON_NAMESPACE


#endif // ICON_RENDERER_H
