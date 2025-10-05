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
#include "Blend2DDrawingModeHelpers.h"
#include "Blend2DDrawingModeNative.h"
#include "Blend2DDrawingModeAlpha.h"
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
			fBlendColorHSpan = blend_color_hspan_copy_native;
			fBlendColorVSpan = blend_color_vspan_copy_native;
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
			fBlendColorHSpan = blend_color_hspan_over_native;
			fBlendColorVSpan = blend_color_vspan_over_native;
			break;

		// ====================================================================
		// B_OP_ADD - Additive blending
		// ====================================================================
		case B_OP_ADD:
			fBlendPixel = blend_pixel_add_native;
			fBlendHLine = blend_hline_add_native;
			fBlendVLine = blend_vline_add_native;
			fBlendSolidHSpan = blend_solid_hspan_add_native;
			fBlendSolidHSpanSubpix = blend_solid_hspan_add_subpix;
			fBlendSolidVSpan = blend_solid_vspan_add_native;
			fBlendColorHSpan = blend_color_hspan_add_native;
			fBlendColorVSpan = blend_color_vspan_add_native;
			break;

		// ====================================================================
		// B_OP_SUBTRACT - Subtractive blending
		// ====================================================================
		case B_OP_SUBTRACT:
		fBlendPixel = blend_pixel_subtract_native;
		fBlendHLine = blend_hline_subtract_native;
		fBlendVLine = blend_vline_subtract_native;
			fBlendSolidHSpan = blend_solid_hspan_subtract_native;
			fBlendSolidHSpanSubpix = blend_solid_hspan_subtract_subpix;
			fBlendSolidVSpan = blend_solid_vspan_subtract_native;
			fBlendColorHSpan = blend_color_hspan_subtract_native;
			fBlendColorVSpan = blend_color_vspan_subtract_native;
			break;

		// ====================================================================
		// B_OP_MIN - Darken
		// ====================================================================
		case B_OP_MIN:
		fBlendPixel = blend_pixel_min_native;
		fBlendHLine = blend_hline_min_native;
		fBlendVLine = blend_vline_min_native;
			fBlendSolidHSpan = blend_solid_hspan_min_native;
			fBlendSolidHSpanSubpix = blend_solid_hspan_min_subpix;
			fBlendSolidVSpan = blend_solid_vspan_min_native;
			fBlendColorHSpan = blend_color_hspan_min_native;
			fBlendColorVSpan = blend_color_vspan_min_native;
			break;

		// ====================================================================
		// B_OP_MAX - Lighten
		// ====================================================================
		case B_OP_MAX:
		fBlendPixel = blend_pixel_max_native;
		fBlendHLine = blend_hline_max_native;
		fBlendVLine = blend_vline_max_native;
			fBlendSolidHSpan = blend_solid_hspan_max_native;
			fBlendSolidHSpanSubpix = blend_solid_hspan_max_subpix;
			fBlendSolidVSpan = blend_solid_vspan_max_native;
			fBlendColorHSpan = blend_color_hspan_max_native;
			fBlendColorVSpan = blend_color_vspan_max_native;
			break;

		// ====================================================================
		// B_OP_BLEND - Averaging (custom)
		// ====================================================================
		case B_OP_BLEND:
		fBlendPixel = blend_pixel_blend_custom;
		fBlendHLine = blend_hline_blend_custom;
		fBlendVLine = blend_vline_blend_custom;
			fBlendSolidHSpan = blend_solid_hspan_blend_custom;
			fBlendSolidHSpanSubpix = blend_solid_hspan_blend_subpix;
			fBlendSolidVSpan = blend_solid_vspan_blend_custom;
			fBlendColorHSpan = blend_color_hspan_blend_custom;
			fBlendColorVSpan = blend_color_vspan_blend_custom;
			break;

		// ====================================================================
		// B_OP_INVERT - Invert RGB channels (custom)
		// ====================================================================
		case B_OP_INVERT:
		fBlendPixel = blend_pixel_invert_custom;
		fBlendHLine = blend_hline_invert_custom;
		fBlendVLine = blend_vline_invert_custom;
			fBlendSolidHSpan = blend_solid_hspan_invert_custom;
			fBlendSolidHSpanSubpix = blend_solid_hspan_invert_subpix;
			fBlendSolidVSpan = blend_solid_vspan_invert_custom;
			fBlendColorHSpan = blend_color_hspan_invert_custom;
			fBlendColorVSpan = blend_color_vspan_invert_custom;
			break;

		// ====================================================================
		// B_OP_SELECT - Conditional color swap (custom)
		// ====================================================================
		case B_OP_SELECT:
		fBlendPixel = blend_pixel_select_custom;
		fBlendHLine = blend_hline_select_custom;
		fBlendVLine = blend_vline_select_custom;
			fBlendSolidHSpan = blend_solid_hspan_select_custom;
			fBlendSolidHSpanSubpix = blend_solid_hspan_select_subpix;
			fBlendSolidVSpan = blend_solid_vspan_select_custom;
			fBlendColorHSpan = blend_color_hspan_select_custom;
			fBlendColorVSpan = blend_color_vspan_select_custom;
			break;

		// ====================================================================
		// B_OP_ERASE - Pattern-based erase (custom)
		// ====================================================================
		case B_OP_ERASE:
		fBlendPixel = blend_pixel_erase_custom;
		fBlendHLine = blend_hline_erase_custom;
		fBlendVLine = blend_vline_erase_custom;
			fBlendSolidHSpan = blend_solid_hspan_erase_custom;
			fBlendSolidHSpanSubpix = blend_solid_hspan_erase_subpix;
			fBlendSolidVSpan = blend_solid_vspan_erase_custom;
			fBlendColorHSpan = blend_color_hspan_erase_custom;
			fBlendColorVSpan = blend_color_vspan_erase_custom;
			break;

		// ====================================================================
		// B_OP_ALPHA - Alpha modes (Porter-Duff operators)
		// ====================================================================
		case B_OP_ALPHA:
			// Реализация через Porter-Duff операторы Blend2D
			// Полная поддержка всех 15 режимов для BeOS API совместимости
			switch (alphaFncMode) {
				case B_ALPHA_OVERLAY:
				case B_ALPHA_COMPOSITE:
				case B_ALPHA_COMPOSITE_SOURCE_OVER:
					// B_ALPHA_OVERLAY/SOURCE_OVER: Standard alpha blending (SRC_OVER)
					fBlendPixel = blend_pixel_over_native;
					fBlendHLine = blend_hline_over_native;
					fBlendVLine = blend_vline_over_native;
					fBlendSolidHSpan = blend_solid_hspan_over_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_over_native;
					fBlendColorHSpan = blend_color_hspan_over_native;
					fBlendColorVSpan = blend_color_vspan_over_native;
					break;

				case B_ALPHA_COMPOSITE_SOURCE_IN:
					// SRC_IN: Source where destination exists
					fBlendPixel = blend_pixel_src_in_native;
					fBlendHLine = blend_hline_src_in_native;
					fBlendVLine = blend_vline_src_in_native;
					fBlendSolidHSpan = blend_solid_hspan_src_in_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_src_in_native;
					fBlendColorHSpan = blend_color_hspan_src_in_native;
					fBlendColorVSpan = blend_color_vspan_src_in_native;
					break;

				case B_ALPHA_COMPOSITE_SOURCE_OUT:
					// SRC_OUT: Source where destination doesn't exist
					fBlendPixel = blend_pixel_src_out_native;
					fBlendHLine = blend_hline_src_out_native;
					fBlendVLine = blend_vline_src_out_native;
					fBlendSolidHSpan = blend_solid_hspan_src_out_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_src_out_native;
					fBlendColorHSpan = blend_color_hspan_src_out_native;
					fBlendColorVSpan = blend_color_vspan_src_out_native;
					break;

				case B_ALPHA_COMPOSITE_SOURCE_ATOP:
					// SRC_ATOP: Source over destination, clipped to destination
					fBlendPixel = blend_pixel_src_atop_native;
					fBlendHLine = blend_hline_src_atop_native;
					fBlendVLine = blend_vline_src_atop_native;
					fBlendSolidHSpan = blend_solid_hspan_src_atop_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_src_atop_native;
					fBlendColorHSpan = blend_color_hspan_src_atop_native;
					fBlendColorVSpan = blend_color_vspan_src_atop_native;
					break;

				case B_ALPHA_COMPOSITE_DESTINATION_OVER:
					// DST_OVER: Destination over source
					fBlendPixel = blend_pixel_dst_over_native;
					fBlendHLine = blend_hline_dst_over_native;
					fBlendVLine = blend_vline_dst_over_native;
					fBlendSolidHSpan = blend_solid_hspan_dst_over_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_dst_over_native;
					fBlendColorHSpan = blend_color_hspan_dst_over_native;
					fBlendColorVSpan = blend_color_vspan_dst_over_native;
					break;

				case B_ALPHA_COMPOSITE_DESTINATION_IN:
					// DST_IN: Destination where source exists
					fBlendPixel = blend_pixel_dst_in_native;
					fBlendHLine = blend_hline_dst_in_native;
					fBlendVLine = blend_vline_dst_in_native;
					fBlendSolidHSpan = blend_solid_hspan_dst_in_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_dst_in_native;
					fBlendColorHSpan = blend_color_hspan_dst_in_native;
					fBlendColorVSpan = blend_color_vspan_dst_in_native;
					break;

				case B_ALPHA_COMPOSITE_DESTINATION_OUT:
					// DST_OUT: Destination where source doesn't exist
					fBlendPixel = blend_pixel_dst_out_native;
					fBlendHLine = blend_hline_dst_out_native;
					fBlendVLine = blend_vline_dst_out_native;
					fBlendSolidHSpan = blend_solid_hspan_dst_out_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_dst_out_native;
					fBlendColorHSpan = blend_color_hspan_dst_out_native;
					fBlendColorVSpan = blend_color_vspan_dst_out_native;
					break;

				case B_ALPHA_COMPOSITE_DESTINATION_ATOP:
					// DST_ATOP: Destination over source, clipped to source
					fBlendPixel = blend_pixel_dst_atop_native;
					fBlendHLine = blend_hline_dst_atop_native;
					fBlendVLine = blend_vline_dst_atop_native;
					fBlendSolidHSpan = blend_solid_hspan_dst_atop_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_dst_atop_native;
					fBlendColorHSpan = blend_color_hspan_dst_atop_native;
					fBlendColorVSpan = blend_color_vspan_dst_atop_native;
					break;

				case B_ALPHA_COMPOSITE_XOR:
					// XOR: Exclusive OR of source and destination
					fBlendPixel = blend_pixel_xor_native;
					fBlendHLine = blend_hline_xor_native;
					fBlendVLine = blend_vline_xor_native;
					fBlendSolidHSpan = blend_solid_hspan_xor_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_xor_native;
					fBlendColorHSpan = blend_color_hspan_xor_native;
					fBlendColorVSpan = blend_color_vspan_xor_native;
					break;

				case B_ALPHA_COMPOSITE_CLEAR:
					// CLEAR: Clear destination (make transparent)
					fBlendPixel = blend_pixel_clear_native;
					fBlendHLine = blend_hline_clear_native;
					fBlendVLine = blend_vline_clear_native;
					fBlendSolidHSpan = blend_solid_hspan_clear_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_clear_native;
					fBlendColorHSpan = blend_color_hspan_clear_native;
					fBlendColorVSpan = blend_color_vspan_clear_native;
					break;

				case B_ALPHA_COMPOSITE_DIFFERENCE:
					// DIFFERENCE: |Source - Destination|
					fBlendPixel = blend_pixel_difference_native;
					fBlendHLine = blend_hline_difference_native;
					fBlendVLine = blend_vline_difference_native;
					fBlendSolidHSpan = blend_solid_hspan_difference_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_difference_native;
					fBlendColorHSpan = blend_color_hspan_difference_native;
					fBlendColorVSpan = blend_color_vspan_difference_native;
					break;

				case B_ALPHA_COMPOSITE_LIGHTEN:
					// LIGHTEN: max(Source, Destination) - maps to existing MAX
					fBlendPixel = blend_pixel_max_native;
					fBlendHLine = blend_hline_max_native;
					fBlendVLine = blend_vline_max_native;
					fBlendSolidHSpan = blend_solid_hspan_max_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_max_subpix;
					fBlendSolidVSpan = blend_solid_vspan_max_native;
					fBlendColorHSpan = blend_color_hspan_max_native;
					fBlendColorVSpan = blend_color_vspan_max_native;
					break;

				case B_ALPHA_COMPOSITE_DARKEN:
					// DARKEN: min(Source, Destination) - maps to existing MIN
					fBlendPixel = blend_pixel_min_native;
					fBlendHLine = blend_hline_min_native;
					fBlendVLine = blend_vline_min_native;
					fBlendSolidHSpan = blend_solid_hspan_min_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_min_subpix;
					fBlendSolidVSpan = blend_solid_vspan_min_native;
					fBlendColorHSpan = blend_color_hspan_min_native;
					fBlendColorVSpan = blend_color_vspan_min_native;
					break;

				default:
					// Неизвестный режим - fallback на SRC_OVER
					fprintf(stderr, "PixelFormat::SetDrawingMode() - "
							"B_OP_ALPHA with unknown alphaFnc=%d, using SRC_OVER\n",
							alphaFncMode);
					fBlendPixel = blend_pixel_over_native;
					fBlendHLine = blend_hline_over_native;
					fBlendVLine = blend_vline_over_native;
					fBlendSolidHSpan = blend_solid_hspan_over_native;
					fBlendSolidHSpanSubpix = blend_solid_hspan_over_subpix;
					fBlendSolidVSpan = blend_solid_vspan_over_native;
					fBlendColorHSpan = blend_color_hspan_over_native;
					fBlendColorVSpan = blend_color_vspan_over_native;
					break;
			}
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
