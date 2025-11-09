/*
 * Copyright 2005, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Copyright 2002-2004 Maxim Shemanarev (http://www.antigrain.com)
 *
 * A class implementing the AGG "pixel format" interface which maintains
 * a PatternHandler and pointers to blending functions implementing the
 * different BeOS "drawing_modes".
 *
 */

#include "PixelFormat.h"

#include <stdio.h>

// NEW: Template-based drawing mode infrastructure
#include "DrawingModePolicy.h"
#include "DrawingModeTemplate.h"

// Migrated to templates: Add, Subtract, Blend, Min, Max, Over, Erase, Invert
// Migrated to templates: AlphaCC, AlphaCO, AlphaPC, AlphaPO (with solid optimizations)

// Not yet migrated (still using old implementation):
#include "DrawingModeCopy.h"
#include "DrawingModeCopySolid.h"
#include "DrawingModeSelect.h"

// SUBPIX versions (not yet migrated):
#include "DrawingModeCopySUBPIX.h"
#include "DrawingModeCopySolidSUBPIX.h"
#include "DrawingModeSelectSUBPIX.h"

#include "PatternHandler.h"

// #pragma mark - NEW: Table-driven mode dispatch infrastructure

// NEW: Structure holding function pointers for a drawing mode
struct DrawingModeEntry {
	PixelFormat::blend_pixel_f pixel;
	PixelFormat::blend_line hline;
	PixelFormat::blend_solid_span solid_hspan;
	PixelFormat::blend_solid_span solid_vspan;
	PixelFormat::blend_color_span color_hspan;
	PixelFormat::blend_solid_span solid_hspan_subpix;
};

// NEW: Helper to construct table entries at compile time
template<typename Policy>
static constexpr DrawingModeEntry MakeModeEntry() {
	return {
		DrawingModeImpl<Policy>::blend_pixel,
		DrawingModeImpl<Policy>::blend_hline,
		DrawingModeImpl<Policy>::blend_solid_hspan,
		DrawingModeImpl<Policy>::blend_solid_vspan,
		DrawingModeImpl<Policy>::blend_color_hspan,
		DrawingModeImpl<Policy>::blend_solid_hspan_subpix
	};
}

// NEW: Helper for Alpha modes (which use AlphaModeImpl template)
template<typename Policy>
static constexpr DrawingModeEntry MakeAlphaModeEntry() {
	return {
		AlphaModeImpl<Policy>::blend_pixel,
		AlphaModeImpl<Policy>::blend_hline,
		AlphaModeImpl<Policy>::blend_solid_hspan,
		AlphaModeImpl<Policy>::blend_solid_vspan,
		AlphaModeImpl<Policy>::blend_color_hspan,
		AlphaModeImpl<Policy>::blend_solid_hspan_subpix
	};
}

// NEW: Helper for pattern-filtered modes (Over, Invert) that use DrawingModeImpl for color_hspan
template<typename Policy>
static constexpr DrawingModeEntry MakePatternFilteredEntry() {
	return {
		PatternFilteredModeImpl<Policy>::blend_pixel,
		PatternFilteredModeImpl<Policy>::blend_hline,
		PatternFilteredModeImpl<Policy>::blend_solid_hspan,
		PatternFilteredModeImpl<Policy>::blend_solid_vspan,
		DrawingModeImpl<Policy>::blend_color_hspan,  // No pattern filtering
		PatternFilteredModeImpl<Policy>::blend_solid_hspan_subpix
	};
}

// NEW: Helper for solid pattern optimization (B_OP_OVER)
template<typename Policy>
static constexpr DrawingModeEntry MakeSolidPatternFilteredEntry() {
	return {
		SolidPatternFilteredModeImpl<Policy>::blend_pixel,
		SolidPatternFilteredModeImpl<Policy>::blend_hline,
		SolidPatternFilteredModeImpl<Policy>::blend_solid_hspan,
		SolidPatternFilteredModeImpl<Policy>::blend_solid_vspan,
		DrawingModeImpl<Policy>::blend_color_hspan,  // No pattern filtering
		SolidPatternFilteredModeImpl<Policy>::blend_solid_hspan_subpix
	};
}

// NEW: Helper for solid Alpha modes optimization (AlphaCC, AlphaCO, AlphaPC, AlphaPO)
template<typename Policy>
static constexpr DrawingModeEntry MakeSolidAlphaModeEntry() {
	return {
		SolidAlphaModeImpl<Policy>::blend_pixel,
		SolidAlphaModeImpl<Policy>::blend_hline,
		SolidAlphaModeImpl<Policy>::blend_solid_hspan,
		SolidAlphaModeImpl<Policy>::blend_solid_vspan,
		AlphaModeImpl<Policy>::blend_color_hspan,  // Use AlphaModeImpl for color_hspan
		SolidAlphaModeImpl<Policy>::blend_solid_hspan_subpix
	};
}

// NEW: Mode dispatch table for simple modes (will be populated incrementally)
// Index corresponds to drawing_mode enum values
// For now, we only populate entries for modes we're ready to migrate
static const DrawingModeEntry MODE_TABLE[] = {
	// B_OP_COPY = 0 - not yet migrated (uses old implementation)
	{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
	// B_OP_OVER = 1 - MIGRATED (pattern-filtered)
	MakePatternFilteredEntry<OverPolicy>(),
	// B_OP_ERASE = 2 - MIGRATED (pattern-filtered, special color_hspan)
	{
		PatternFilteredModeImpl<ErasePolicy>::blend_pixel,
		PatternFilteredModeImpl<ErasePolicy>::blend_hline,
		PatternFilteredModeImpl<ErasePolicy>::blend_solid_hspan,
		PatternFilteredModeImpl<ErasePolicy>::blend_solid_vspan,
		blend_color_hspan_erase,  // Special: uses LowColor instead of colors parameter
		PatternFilteredModeImpl<ErasePolicy>::blend_solid_hspan_subpix
	},
	// B_OP_INVERT = 3 - MIGRATED (pattern-filtered)
	MakePatternFilteredEntry<InvertPolicy>(),
	// B_OP_ADD = 4 - READY for migration
	MakeModeEntry<AddPolicy>(),
	// B_OP_SUBTRACT = 5 - READY for migration
	MakeModeEntry<SubtractPolicy>(),
	// B_OP_BLEND = 6 - READY for migration
	MakeModeEntry<BlendPolicy>(),
	// B_OP_MIN = 7 - READY for migration  
	MakeModeEntry<MinPolicy>(),
	// B_OP_MAX = 8 - READY for migration
	MakeModeEntry<MaxPolicy>(),
	// B_OP_SELECT = 9 - not yet migrated
	{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
	// B_OP_ALPHA = 10 - handled separately (see alpha mode table below)
	{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr },
};

// NEW: Alpha mode dispatch table (4 variants: CC, CO, PC, PO)
static const DrawingModeEntry ALPHA_MODE_TABLE[] = {
	MakeAlphaModeEntry<AlphaCCPolicy>(),  // Constant-Composite
	MakeAlphaModeEntry<AlphaCOPolicy>(),  // Constant-Overlay
	MakeAlphaModeEntry<AlphaPCPolicy>(),  // Pixel-Composite
	MakeAlphaModeEntry<AlphaPOPolicy>(),  // Pixel-Overlay
};

// #pragma mark - OLD: Original blend functions (will be removed in Phase 5)

// blend_pixel_empty
void
blend_pixel_empty(int x, int y, const color_type& c, uint8 cover,
				  agg_buffer* buffer, const PatternHandler* pattern)
{
	printf("blend_pixel_empty()\n");
}

// blend_hline_empty
void
blend_hline_empty(int x, int y, unsigned len,
				  const color_type& c, uint8 cover,
				  agg_buffer* buffer, const PatternHandler* pattern)
{
	printf("blend_hline_empty()\n");
}

// blend_vline_empty
void
blend_vline_empty(int x, int y, unsigned len,
				  const color_type& c, uint8 cover,
				  agg_buffer* buffer, const PatternHandler* pattern)
{
	printf("blend_vline_empty()\n");
}

// blend_solid_hspan_empty
void
blend_solid_hspan_empty(int x, int y, unsigned len,
						const color_type& c, const uint8* covers,
						agg_buffer* buffer, const PatternHandler* pattern)
{
	printf("blend_solid_hspan_empty()\n");
}

// blend_solid_hspan_subpix_empty
void
blend_solid_hspan_empty_subpix(int x, int y, unsigned len,
						const color_type& c, const uint8* covers,
						agg_buffer* buffer, const PatternHandler* pattern)
{
	printf("blend_solid_hspan_empty_subpix()\n");
}

// blend_solid_vspan_empty
void
blend_solid_vspan_empty(int x, int y,
						unsigned len, const color_type& c,
						const uint8* covers,
						agg_buffer* buffer, const PatternHandler* pattern)
{
	printf("blend_solid_vspan_empty()\n");
}

// blend_color_hspan_empty
void
blend_color_hspan_empty(int x, int y, unsigned len,
						const color_type* colors,
						const uint8* covers, uint8 cover,
						agg_buffer* buffer, const PatternHandler* pattern)
{
	printf("blend_color_hspan_empty()\n");
}

// blend_color_vspan_empty
void
blend_color_vspan_empty(int x, int y, unsigned len,
						const color_type* colors,
						const uint8* covers, uint8 cover,
						agg_buffer* buffer, const PatternHandler* pattern)
{
	printf("blend_color_vspan_empty()\n");
}

// #pragma mark -

// constructor
PixelFormat::PixelFormat(agg::rendering_buffer& rb,
						 const PatternHandler* handler)
	: fBuffer(&rb),
	  fPatternHandler(handler),

	  fBlendPixel(blend_pixel_empty),
	  fBlendHLine(blend_hline_empty),
	  fBlendVLine(blend_vline_empty),
	  fBlendSolidHSpan(blend_solid_hspan_empty),
	  fBlendSolidHSpanSubpix(blend_solid_hspan_empty_subpix),
	  fBlendSolidVSpan(blend_solid_vspan_empty),
	  fBlendColorHSpan(blend_color_hspan_empty),
	  fBlendColorVSpan(blend_color_vspan_empty)
{
}

// destructor
PixelFormat::~PixelFormat()
{
}

// SetDrawingMode
void
PixelFormat::SetDrawingMode(drawing_mode mode, source_alpha alphaSrcMode,
							alpha_function alphaFncMode)
{
	switch (mode) {
		// These drawing modes discard source pixels
		// which have the current low color.
		case B_OP_OVER: {
			// NEW: Use template-based implementation with solid pattern optimization
			if (fPatternHandler->IsSolid()) {
				// Optimized version for solid patterns
				static constexpr DrawingModeEntry solidEntry = MakeSolidPatternFilteredEntry<OverPolicy>();
				fBlendPixel = solidEntry.pixel;
				fBlendHLine = solidEntry.hline;
				fBlendSolidHSpan = solidEntry.solid_hspan;
				fBlendSolidVSpan = solidEntry.solid_vspan;
				fBlendColorHSpan = solidEntry.color_hspan;
				fBlendSolidHSpanSubpix = solidEntry.solid_hspan_subpix;
			} else {
				// Pattern version
				const DrawingModeEntry& entry = MODE_TABLE[B_OP_OVER];
				fBlendPixel = entry.pixel;
				fBlendHLine = entry.hline;
				fBlendSolidHSpan = entry.solid_hspan;
				fBlendSolidVSpan = entry.solid_vspan;
				fBlendColorHSpan = entry.color_hspan;
				fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
			}
			break;
		}
		case B_OP_ERASE: {
			// NEW: Use template-based implementation
			const DrawingModeEntry& entry = MODE_TABLE[B_OP_ERASE];
			fBlendPixel = entry.pixel;
			fBlendHLine = entry.hline;
			fBlendSolidHSpan = entry.solid_hspan;
			fBlendSolidVSpan = entry.solid_vspan;
			fBlendColorHSpan = entry.color_hspan;
			fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
			break;
		}
		case B_OP_INVERT: {
			// NEW: Use template-based implementation
			const DrawingModeEntry& entry = MODE_TABLE[B_OP_INVERT];
			fBlendPixel = entry.pixel;
			fBlendHLine = entry.hline;
			fBlendSolidHSpan = entry.solid_hspan;
			fBlendSolidVSpan = entry.solid_vspan;
			fBlendColorHSpan = entry.color_hspan;
			fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
			break;
		}
		case B_OP_SELECT:
			fBlendPixel = blend_pixel_select;
			fBlendHLine = blend_hline_select;
			fBlendSolidHSpanSubpix = blend_solid_hspan_select_subpix;
			fBlendSolidHSpan = blend_solid_hspan_select;
			fBlendSolidVSpan = blend_solid_vspan_select;
			fBlendColorHSpan = blend_color_hspan_select;
			break;

		// In these drawing modes, the current high
		// and low color are treated equally.
		case B_OP_COPY:
			if (fPatternHandler->IsSolid()) {
				fBlendPixel = blend_pixel_copy_solid;
				fBlendHLine = blend_hline_copy_solid;
				fBlendSolidHSpanSubpix = blend_solid_hspan_copy_solid_subpix;
				fBlendSolidHSpan = blend_solid_hspan_copy_solid;
				fBlendSolidVSpan = blend_solid_vspan_copy_solid;
				fBlendColorHSpan = blend_color_hspan_copy_solid;
			} else {
				fBlendPixel = blend_pixel_copy;
				fBlendHLine = blend_hline_copy;
				fBlendSolidHSpanSubpix = blend_solid_hspan_copy_subpix;
				fBlendSolidHSpan = blend_solid_hspan_copy;
				fBlendSolidVSpan = blend_solid_vspan_copy;
				fBlendColorHSpan = blend_color_hspan_copy;
			}
			break;
		case B_OP_ADD: {
		// NEW: Use template-based implementation
		const DrawingModeEntry& entry = MODE_TABLE[B_OP_ADD];
		fBlendPixel = entry.pixel;
		fBlendHLine = entry.hline;
		fBlendSolidHSpan = entry.solid_hspan;
		fBlendSolidVSpan = entry.solid_vspan;
		fBlendColorHSpan = entry.color_hspan;
		fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
		break;
	}
		case B_OP_SUBTRACT: {
		// NEW: Use template-based implementation
		const DrawingModeEntry& entry = MODE_TABLE[B_OP_SUBTRACT];
		fBlendPixel = entry.pixel;
		fBlendHLine = entry.hline;
		fBlendSolidHSpan = entry.solid_hspan;
		fBlendSolidVSpan = entry.solid_vspan;
		fBlendColorHSpan = entry.color_hspan;
		fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
		break;
	}
		case B_OP_BLEND: {
		// NEW: Use template-based implementation
		const DrawingModeEntry& entry = MODE_TABLE[B_OP_BLEND];
		fBlendPixel = entry.pixel;
		fBlendHLine = entry.hline;
		fBlendSolidHSpan = entry.solid_hspan;
		fBlendSolidVSpan = entry.solid_vspan;
		fBlendColorHSpan = entry.color_hspan;
		fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
		break;
	}
		case B_OP_MIN: {
		// NEW: Use template-based implementation
		const DrawingModeEntry& entry = MODE_TABLE[B_OP_MIN];
		fBlendPixel = entry.pixel;
		fBlendHLine = entry.hline;
		fBlendSolidHSpan = entry.solid_hspan;
		fBlendSolidVSpan = entry.solid_vspan;
		fBlendColorHSpan = entry.color_hspan;
		fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
		break;
	}
		case B_OP_MAX: {
		// NEW: Use template-based implementation
		const DrawingModeEntry& entry = MODE_TABLE[B_OP_MAX];
		fBlendPixel = entry.pixel;
		fBlendHLine = entry.hline;
		fBlendSolidHSpan = entry.solid_hspan;
		fBlendSolidVSpan = entry.solid_vspan;
		fBlendColorHSpan = entry.color_hspan;
		fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
		break;
	}

		// This drawing mode is the only one considering
		// alpha at all. In B_CONSTANT_ALPHA, the alpha
		// value from the current high color is used for
		// all computations. In B_PIXEL_ALPHA, the alpha
		// is considered at every source pixel.
		// To simplify the implementation, four separate
		// DrawingMode classes are used to cover the
		// four possible combinations of alpha enabled drawing.
		case B_OP_ALPHA:
			if (alphaSrcMode == B_CONSTANT_ALPHA) {
				// NEW: Use template-based Alpha mode implementation with solid optimization
				if (alphaFncMode == B_ALPHA_OVERLAY) {
					// AlphaCO - Constant Overlay
					if (fPatternHandler->IsSolid()) {
						// Optimized version for solid patterns
						static constexpr DrawingModeEntry solidEntry = MakeSolidAlphaModeEntry<AlphaCOPolicy>();
						fBlendPixel = solidEntry.pixel;
						fBlendHLine = solidEntry.hline;
						fBlendSolidHSpan = solidEntry.solid_hspan;
						fBlendSolidVSpan = solidEntry.solid_vspan;
						fBlendColorHSpan = solidEntry.color_hspan;
						fBlendSolidHSpanSubpix = solidEntry.solid_hspan_subpix;
					} else {
						// Pattern version
						const DrawingModeEntry& entry = ALPHA_MODE_TABLE[1];
						fBlendPixel = entry.pixel;
						fBlendHLine = entry.hline;
						fBlendSolidHSpan = entry.solid_hspan;
						fBlendSolidVSpan = entry.solid_vspan;
						fBlendColorHSpan = entry.color_hspan;
						fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
					}
				} else if (alphaFncMode == B_ALPHA_COMPOSITE) {
					// AlphaCC - Constant Composite
					if (fPatternHandler->IsSolid()) {
						// Optimized version for solid patterns
						static constexpr DrawingModeEntry solidEntry = MakeSolidAlphaModeEntry<AlphaCCPolicy>();
						fBlendPixel = solidEntry.pixel;
						fBlendHLine = solidEntry.hline;
						fBlendSolidHSpan = solidEntry.solid_hspan;
						fBlendSolidVSpan = solidEntry.solid_vspan;
						fBlendColorHSpan = solidEntry.color_hspan;
						fBlendSolidHSpanSubpix = solidEntry.solid_hspan_subpix;
					} else {
						// Pattern version
						const DrawingModeEntry& entry = ALPHA_MODE_TABLE[0];
						fBlendPixel = entry.pixel;
						fBlendHLine = entry.hline;
						fBlendSolidHSpan = entry.solid_hspan;
						fBlendSolidVSpan = entry.solid_vspan;
						fBlendColorHSpan = entry.color_hspan;
						fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
					}
				}
			} else if (alphaSrcMode == B_PIXEL_ALPHA){
				// NEW: Use template-based Alpha mode implementation with solid optimization
				if (alphaFncMode == B_ALPHA_OVERLAY) {
					// AlphaPO - Pixel Overlay
					if (fPatternHandler->IsSolid()) {
						// Optimized version for solid patterns
						static constexpr DrawingModeEntry solidEntry = MakeSolidAlphaModeEntry<AlphaPOPolicy>();
						fBlendPixel = solidEntry.pixel;
						fBlendHLine = solidEntry.hline;
						fBlendSolidHSpan = solidEntry.solid_hspan;
						fBlendSolidVSpan = solidEntry.solid_vspan;
						fBlendColorHSpan = solidEntry.color_hspan;
						fBlendSolidHSpanSubpix = solidEntry.solid_hspan_subpix;
					} else {
						// Pattern version
						const DrawingModeEntry& entry = ALPHA_MODE_TABLE[3];
						fBlendPixel = entry.pixel;
						fBlendHLine = entry.hline;
						fBlendSolidHSpan = entry.solid_hspan;
						fBlendSolidVSpan = entry.solid_vspan;
						fBlendColorHSpan = entry.color_hspan;
						fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
					}
				} else if (alphaFncMode == B_ALPHA_COMPOSITE) {
					// AlphaPC - Pixel Composite
					if (fPatternHandler->IsSolid()) {
						// Optimized version for solid patterns
						static constexpr DrawingModeEntry solidEntry = MakeSolidAlphaModeEntry<AlphaPCPolicy>();
						fBlendPixel = solidEntry.pixel;
						fBlendHLine = solidEntry.hline;
						fBlendSolidHSpan = solidEntry.solid_hspan;
						fBlendSolidVSpan = solidEntry.solid_vspan;
						fBlendColorHSpan = solidEntry.color_hspan;
						fBlendSolidHSpanSubpix = solidEntry.solid_hspan_subpix;
					} else {
						// Pattern version
						const DrawingModeEntry& entry = ALPHA_MODE_TABLE[2];
						fBlendPixel = entry.pixel;
						fBlendHLine = entry.hline;
						fBlendSolidHSpan = entry.solid_hspan;
						fBlendSolidVSpan = entry.solid_vspan;
						fBlendColorHSpan = entry.color_hspan;
						fBlendSolidHSpanSubpix = entry.solid_hspan_subpix;
					}
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_SOURCE_IN) {
					SetAggCompOpAdapter<alpha_src_in>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_SOURCE_OUT) {
					SetAggCompOpAdapter<alpha_src_out>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_SOURCE_ATOP) {
					SetAggCompOpAdapter<alpha_src_atop>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_DESTINATION_OVER) {
					SetAggCompOpAdapter<alpha_dst_over>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_DESTINATION_IN) {
					SetAggCompOpAdapter<alpha_dst_in>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_DESTINATION_OUT) {
					SetAggCompOpAdapter<alpha_dst_out>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_DESTINATION_ATOP) {
					SetAggCompOpAdapter<alpha_dst_atop>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_XOR) {
					SetAggCompOpAdapter<alpha_xor>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_CLEAR) {
					SetAggCompOpAdapter<alpha_clear>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_DIFFERENCE) {
					SetAggCompOpAdapter<alpha_difference>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_LIGHTEN) {
					SetAggCompOpAdapter<alpha_lighten>();
				} else if (alphaFncMode == B_ALPHA_COMPOSITE_DARKEN) {
					SetAggCompOpAdapter<alpha_darken>();
				}
			}
			break;

		default:
fprintf(stderr, "PixelFormat::SetDrawingMode() - drawing_mode not implemented\n");
//			return fDrawingModeBGRA32Copy;
			break;
	}
}
