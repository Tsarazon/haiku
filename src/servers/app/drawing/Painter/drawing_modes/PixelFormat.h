/*
 * Copyright 2005, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>
 * Copyright 2025, Haiku Blend2D Migration
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Pixel format interface - Blend2D implementation.
 * Maintains the same API for compatibility.
 */

#ifndef PIXEL_FORMAT_H
#define PIXEL_FORMAT_H

#include <blend2d.h>
#include <GraphicsDefs.h>
#include "Blend2DDrawingMode.h"

class PatternHandler;

class PixelFormat {
 public:
	// Color type - RGBA8 (compatible with BLRgba32)
	struct color_type {
		uint8 r, g, b, a;
		
		color_type() : r(0), g(0), b(0), a(0) {}
		color_type(uint8 r_, uint8 g_, uint8 b_, uint8 a_ = 255)
			: r(r_), g(g_), b(b_), a(a_) {}
	};

	enum base_scale_e {
		base_shift = 8,
		base_scale = 255,
		base_mask  = 255,
		pix_width  = 4,  // BGRA32
	};

	// Function pointer types for drawing mode dispatching
	typedef void (*blend_pixel_f)(int x, int y, const color_type& c,
								  uint8 cover,
								  BLImage* image,
								  BLContext* ctx,
								  const PatternHandler* pattern);

	typedef void (*blend_line)(int x, int y, unsigned len,
							   const color_type& c, uint8 cover,
							   BLImage* image,
							   BLContext* ctx,
							   const PatternHandler* pattern);

	typedef void (*blend_solid_span)(int x, int y, unsigned len,
									 const color_type& c,
									 const uint8* covers,
									 BLImage* image,
									 BLContext* ctx,
									 const PatternHandler* pattern);

	typedef void (*blend_color_span)(int x, int y, unsigned len,
									 const color_type* colors,
									 const uint8* covers,
									 uint8 cover,
									 BLImage* image,
									 BLContext* ctx,
									 const PatternHandler* pattern);

	// Constructor/Destructor
	PixelFormat(BLImage& image, BLContext& ctx,
			   const PatternHandler* handler);
	~PixelFormat();

	// Set drawing mode (dispatches to appropriate Blend2D implementation)
	void SetDrawingMode(drawing_mode mode,
					   source_alpha alphaSrcMode,
					   alpha_function alphaFncMode);

	// Interface compatibility with AGG PixelFormat
	inline unsigned width() const;
	inline unsigned height() const;
	inline int stride() const;

	inline uint8* row_ptr(int y);
	inline const uint8* row_ptr(int y) const;

	inline uint8* pix_ptr(int x, int y);
	inline const uint8* pix_ptr(int x, int y) const;

	inline static void make_pix(uint8* p, const color_type& c);

	// Blending operations (dispatch to function pointers)
	inline void blend_pixel(int x, int y, const color_type& c, uint8 cover);
	inline void blend_hline(int x, int y, unsigned len,
						   const color_type& c, uint8 cover);
	inline void blend_vline(int x, int y, unsigned len,
						   const color_type& c, uint8 cover);
	inline void blend_solid_hspan(int x, int y, unsigned len,
								 const color_type& c, const uint8* covers);
	inline void blend_solid_hspan_subpix(int x, int y, unsigned len,
									    const color_type& c,
										const uint8* covers);
	inline void blend_solid_vspan(int x, int y, unsigned len,
								 const color_type& c, const uint8* covers);
	inline void blend_color_hspan(int x, int y, unsigned len,
								 const color_type* colors,
								 const uint8* covers, uint8 cover);
	inline void blend_color_vspan(int x, int y, unsigned len,
								 const color_type* colors,
								 const uint8* covers, uint8 cover);

 private:
	BLImage* fImage;
	BLContext* fContext;
	const PatternHandler* fPatternHandler;

	// Cached image data for direct pixel access
	mutable BLImageData fImageData;
	mutable bool fImageDataValid;

	// Function pointers for current drawing mode
	blend_pixel_f fBlendPixel;
	blend_line fBlendHLine;
	blend_line fBlendVLine;
	blend_solid_span fBlendSolidHSpan;
	blend_solid_span fBlendSolidHSpanSubpix;
	blend_solid_span fBlendSolidVSpan;
	blend_color_span fBlendColorHSpan;
	blend_color_span fBlendColorVSpan;

	// Helper to ensure image data is accessible
	void _EnsureImageData() const;
	void _InvalidateImageData();
};

// Inline implementations

inline unsigned
PixelFormat::width() const
{
	return fImage->width();
}

inline unsigned
PixelFormat::height() const
{
	return fImage->height();
}

inline int
PixelFormat::stride() const
{
	_EnsureImageData();
	return fImageData.stride;
}

inline uint8*
PixelFormat::row_ptr(int y)
{
	_EnsureImageData();
	return (uint8*)fImageData.pixelData + y * fImageData.stride;
}

inline const uint8*
PixelFormat::row_ptr(int y) const
{
	_EnsureImageData();
	return (const uint8*)fImageData.pixelData + y * fImageData.stride;
}

inline uint8*
PixelFormat::pix_ptr(int x, int y)
{
	return row_ptr(y) + x * pix_width;
}

inline const uint8*
PixelFormat::pix_ptr(int x, int y) const
{
	return row_ptr(y) + x * pix_width;
}

inline void
PixelFormat::make_pix(uint8* p, const color_type& c)
{
	// BGRA32 format
	p[0] = c.b;
	p[1] = c.g;
	p[2] = c.r;
	p[3] = c.a;
}

inline void
PixelFormat::blend_pixel(int x, int y, const color_type& c, uint8 cover)
{
	fBlendPixel(x, y, c, cover, fImage, fContext, fPatternHandler);
}

inline void
PixelFormat::blend_hline(int x, int y, unsigned len,
						const color_type& c, uint8 cover)
{
	fBlendHLine(x, y, len, c, cover, fImage, fContext, fPatternHandler);
}

inline void
PixelFormat::blend_vline(int x, int y, unsigned len,
						const color_type& c, uint8 cover)
{
	fBlendVLine(x, y, len, c, cover, fImage, fContext, fPatternHandler);
}

inline void
PixelFormat::blend_solid_hspan(int x, int y, unsigned len,
							   const color_type& c, const uint8* covers)
{
	fBlendSolidHSpan(x, y, len, c, covers, fImage, fContext, fPatternHandler);
}

inline void
PixelFormat::blend_solid_hspan_subpix(int x, int y, unsigned len,
									 const color_type& c,
									 const uint8* covers)
{
	fBlendSolidHSpanSubpix(x, y, len, c, covers, fImage, fContext,
						  fPatternHandler);
}

inline void
PixelFormat::blend_solid_vspan(int x, int y, unsigned len,
							   const color_type& c, const uint8* covers)
{
	fBlendSolidVSpan(x, y, len, c, covers, fImage, fContext, fPatternHandler);
}

inline void
PixelFormat::blend_color_hspan(int x, int y, unsigned len,
							   const color_type* colors,
							   const uint8* covers, uint8 cover)
{
	fBlendColorHSpan(x, y, len, colors, covers, cover, fImage, fContext,
					fPatternHandler);
}

inline void
PixelFormat::blend_color_vspan(int x, int y, unsigned len,
							   const color_type* colors,
							   const uint8* covers, uint8 cover)
{
	fBlendColorVSpan(x, y, len, colors, covers, cover, fImage, fContext,
					fPatternHandler);
}

#endif // PIXEL_FORMAT_H
