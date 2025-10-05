/*
 * Copyright 2005, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>
 * Copyright 2025, Haiku Blend2D Migration
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Pixel format implementation - Blend2D backend.
 */

#include "PixelFormat.h"
#include "PatternHandler.h"

#include <stdio.h>

// Include all drawing mode implementations
#include "Blend2DDrawingModeNative.h"
#include "Blend2DDrawingModeCustom.h"
#include "Blend2DDrawingModeSUBPIX.h"

// Empty/stub implementations for unimplemented modes
static void
blend_pixel_empty(int x, int y, const PixelFormat::color_type& c,
				 uint8 cover, BLImage* image, BLContext* ctx,
				 const PatternHandler* pattern)
{
	printf("blend_pixel_empty()\n");
}

static void
blend_hline_empty(int x, int y, unsigned len,
				 const PixelFormat::color_type& c, uint8 cover,
				 BLImage* image, BLContext* ctx,
				 const PatternHandler* pattern)
{
	printf("blend_hline_empty()\n");
}

static void
blend_vline_empty(int x, int y, unsigned len,
				 const PixelFormat::color_type& c, uint8 cover,
				 BLImage* image, BLContext* ctx,
				 const PatternHandler* pattern)
{
	printf("blend_vline_empty()\n");
}

static void
blend_solid_hspan_empty(int x, int y, unsigned len,
					   const PixelFormat::color_type& c,
					   const uint8* covers,
					   BLImage* image, BLContext* ctx,
					   const PatternHandler* pattern)
{
	printf("blend_solid_hspan_empty()\n");
}

static void
blend_solid_hspan_subpix_empty(int x, int y, unsigned len,
							  const PixelFormat::color_type& c,
							  const uint8* covers,
							  BLImage* image, BLContext* ctx,
							  const PatternHandler* pattern)
{
	printf("blend_solid_hspan_subpix_empty()\n");
}

static void
blend_solid_vspan_empty(int x, int y, unsigned len,
					   const PixelFormat::color_type& c,
					   const uint8* covers,
					   BLImage* image, BLContext* ctx,
					   const PatternHandler* pattern)
{
	printf("blend_solid_vspan_empty()\n");
}

static void
blend_color_hspan_empty(int x, int y, unsigned len,
					   const PixelFormat::color_type* colors,
					   const uint8* covers, uint8 cover,
					   BLImage* image, BLContext* ctx,
					   const PatternHandler* pattern)
{
	printf("blend_color_hspan_empty()\n");
}

static void
blend_color_vspan_empty(int x, int y, unsigned len,
					   const PixelFormat::color_type* colors,
					   const uint8* covers, uint8 cover,
					   BLImage* image, BLContext* ctx,
					   const PatternHandler* pattern)
{
	printf("blend_color_vspan_empty()\n");
}

// Constructor
PixelFormat::PixelFormat(BLImage& image, BLContext& ctx,
						 const PatternHandler* handler)
	: fImage(&image),
	  fContext(&ctx),
	  fPatternHandler(handler),
	  fImageDataValid(false),
	  fBlendPixel(blend_pixel_empty),
	  fBlendHLine(blend_hline_empty),
	  fBlendVLine(blend_vline_empty),
	  fBlendSolidHSpan(blend_solid_hspan_empty),
	  fBlendSolidHSpanSubpix(blend_solid_hspan_subpix_empty),
	  fBlendSolidVSpan(blend_solid_vspan_empty),
	  fBlendColorHSpan(blend_color_hspan_empty),
	  fBlendColorVSpan(blend_color_vspan_empty)
{
}

// Destructor
PixelFormat::~PixelFormat()
{
}

// SetDrawingMode
void
PixelFormat::SetDrawingMode(drawing_mode mode, source_alpha alphaSrcMode,
							alpha_function alphaFncMode)
{
	switch (mode) {
		// ====================================================================
		// B_OP_COPY - Direct copy
		// ====================================================================
		case B_OP_COPY:
			fBlendPixel = blend_pixel_copy_native;
			fBlendHLine = blend_hline_copy_native;
			fBlendVLine = blend_vline_copy_native;
			fBlendSolidHSpan = blend_solid_hspan_copy_native;
			fBlendSolidHSpanSubpix = blend_solid_hspan_copy_subpix;
			fBlendSolidVSpan = blend_solid_vspan_copy_native;
			fBlendColorHSpan = blend_color_hspan_empty; // TODO
			fBlendColorVSpan = blend_color_vspan_empty; // TODO
			break;

		// ====================================================================
		// B_OP_OVER - Source over (default blending)
		// ====================================================================
		case B_OP_OVER:
			fBlendPixel = blend_pixel_over_native;
			fBlendHLine = blend_hline_over_native;
			fBlendVLine = blend_vline_over_native;
			fBlendSolidHSpan = blend_solid_hspan_over_native;
			fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
			fBlendSolidVSpan = blend_solid_vspan_over_native;
			fBlendColorHSpan = blend_color_hspan_empty; // TODO
			fBlendColorVSpan = blend_color_vspan_empty; // TODO
			break;

		// ====================================================================
		// B_OP_ADD - Additive blending
		// ====================================================================
		case B_OP_ADD:
			fBlendPixel = blend_pixel_add_native;
			fBlendHLine = blend_hline_add_native;
			fBlendVLine = blend_vline_empty; // TODO
			fBlendSolidHSpan = blend_solid_hspan_add_native;
			fBlendSolidHSpanSubpix = blend_solid_hspan_add_subpix;
			fBlendSolidVSpan = blend_solid_vspan_add_native;
			fBlendColorHSpan = blend_color_hspan_empty; // TODO
			fBlendColorVSpan = blend_color_vspan_empty; // TODO
			break;

		// ====================================================================
		// B_OP_SUBTRACT - Subtractive blending
		// ====================================================================
		case B_OP_SUBTRACT:
			fBlendPixel = blend_pixel_subtract_native;
			fBlendHLine = blend_hline_subtract_native;
			fBlendVLine = blend_vline_empty; // TODO
			fBlendSolidHSpan = blend_solid_hspan_subtract_native;
			fBlendSolidHSpanSubpix = blend_solid_hspan_subtract_subpix;
			fBlendSolidVSpan = blend_solid_vspan_subtract_native;
			fBlendColorHSpan = blend_color_hspan_empty; // TODO
			fBlendColorVSpan = blend_color_vspan_empty; // TODO
			break;

		// ====================================================================
		// B_OP_MIN - Darken
		// ====================================================================
		case B_OP_MIN:
			fBlendPixel = blend_pixel_min_native;
			fBlendHLine = blend_hline_min_native;
			fBlendVLine = blend_vline_empty; // TODO
			fBlendSolidHSpan = blend_solid_hspan_min_native;
			fBlendSolidHSpanSubpix = blend_solid_hspan_min_subpix;
			fBlendSolidVSpan = blend_solid_vspan_min_native;
			fBlendColorHSpan = blend_color_hspan_empty; // TODO
			fBlendColorVSpan = blend_color_vspan_empty; // TODO
			break;

		// ====================================================================
		// B_OP_MAX - Lighten
		// ====================================================================
		case B_OP_MAX:
			fBlendPixel = blend_pixel_max_native;
			fBlendHLine = blend_hline_max_native;
			fBlendVLine = blend_vline_empty; // TODO
			fBlendSolidHSpan = blend_solid_hspan_max_native;
			fBlendSolidHSpanSubpix = blend_solid_hspan_max_subpix;
			fBlendSolidVSpan = blend_solid_vspan_max_native;
			fBlendColorHSpan = blend_color_hspan_empty; // TODO
			fBlendColorVSpan = blend_color_vspan_empty; // TODO
			break;

		// ====================================================================
		// B_OP_BLEND - Averaging (custom)
		// ====================================================================
		case B_OP_BLEND:
			fBlendPixel = blend_pixel_blend_custom;
			fBlendHLine = blend_hline_blend_custom;
			fBlendVLine = blend_vline_empty; // TODO
			fBlendSolidHSpan = blend_solid_hspan_blend_custom;
			fBlendSolidHSpanSubpix = blend_solid_hspan_blend_subpix;
			fBlendSolidVSpan = blend_solid_vspan_blend_custom;
			fBlendColorHSpan = blend_color_hspan_empty; // TODO
			fBlendColorVSpan = blend_color_vspan_empty; // TODO
			break;

		// ====================================================================
		// B_OP_INVERT - Invert RGB channels (custom)
		// ====================================================================
		case B_OP_INVERT:
			fBlendPixel = blend_pixel_invert_custom;
			fBlendHLine = blend_hline_invert_custom;
			fBlendVLine = blend_vline_empty; // TODO
			fBlendSolidHSpan = blend_solid_hspan_invert_custom;
			fBlendSolidHSpanSubpix = blend_solid_hspan_invert_subpix;
			fBlendSolidVSpan = blend_solid_vspan_invert_custom;
			fBlendColorHSpan = blend_color_hspan_empty; // TODO
			fBlendColorVSpan = blend_color_vspan_empty; // TODO
			break;

		// ====================================================================
		// B_OP_SELECT - Conditional color swap (custom)
		// ====================================================================
		case B_OP_SELECT:
			fBlendPixel = blend_pixel_select_custom;
			fBlendHLine = blend_hline_select_custom;
			fBlendVLine = blend_vline_empty; // TODO
			fBlendSolidHSpan = blend_solid_hspan_select_custom;
			fBlendSolidHSpanSubpix = blend_solid_hspan_select_subpix;
			fBlendSolidVSpan = blend_solid_vspan_select_custom;
			fBlendColorHSpan = blend_color_hspan_empty; // TODO
			fBlendColorVSpan = blend_color_vspan_empty; // TODO
			break;

		// ====================================================================
		// B_OP_ERASE - Pattern-based erase (custom)
		// ====================================================================
		case B_OP_ERASE:
			fBlendPixel = blend_pixel_erase_custom;
			fBlendHLine = blend_hline_erase_custom;
			fBlendVLine = blend_vline_empty; // TODO
			fBlendSolidHSpan = blend_solid_hspan_erase_custom;
			fBlendSolidHSpanSubpix = blend_solid_hspan_erase_subpix;
			fBlendSolidVSpan = blend_solid_vspan_erase_custom;
			fBlendColorHSpan = blend_color_hspan_empty; // TODO
			fBlendColorVSpan = blend_color_vspan_empty; // TODO
			break;

		// ====================================================================
		// B_OP_ALPHA - Alpha modes (Porter-Duff operators)
		// ====================================================================
		case B_OP_ALPHA:
			// TODO: Implement alpha variants
			// For now, default to empty stubs
			fprintf(stderr, "PixelFormat::SetDrawingMode() - "
					"B_OP_ALPHA not yet implemented (alphaSrc=%d, alphaFnc=%d)\n",
					alphaSrcMode, alphaFncMode);
			fBlendPixel = blend_pixel_empty;
			fBlendHLine = blend_hline_empty;
			fBlendVLine = blend_vline_empty;
			fBlendSolidHSpan = blend_solid_hspan_empty;
			fBlendSolidHSpanSubpix = blend_solid_hspan_subpix_empty;
			fBlendSolidVSpan = blend_solid_vspan_empty;
			fBlendColorHSpan = blend_color_hspan_empty;
			fBlendColorVSpan = blend_color_vspan_empty;
			break;

		default:
			fprintf(stderr, "PixelFormat::SetDrawingMode() - "
					"drawing_mode %d not implemented\n", mode);
			fBlendPixel = blend_pixel_empty;
			fBlendHLine = blend_hline_empty;
			fBlendVLine = blend_vline_empty;
			fBlendSolidHSpan = blend_solid_hspan_empty;
			fBlendSolidHSpanSubpix = blend_solid_hspan_subpix_empty;
			fBlendSolidVSpan = blend_solid_vspan_empty;
			fBlendColorHSpan = blend_color_hspan_empty;
			fBlendColorVSpan = blend_color_vspan_empty;
			break;
	}
}

// _EnsureImageData
void
PixelFormat::_EnsureImageData() const
{
	if (!fImageDataValid) {
		// Get image data (read-only access for now)
		BLResult result = fImage->getData(&fImageData);
		if (result != BL_SUCCESS) {
			fprintf(stderr, "PixelFormat::_EnsureImageData() - "
					"Failed to get image data: %d\n", result);
		}
		fImageDataValid = true;
	}
}

// _InvalidateImageData
void
PixelFormat::_InvalidateImageData()
{
	fImageDataValid = false;
}
