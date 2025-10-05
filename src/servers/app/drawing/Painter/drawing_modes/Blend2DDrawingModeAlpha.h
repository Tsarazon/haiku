/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Porter-Duff alpha compositing modes for B_OP_ALPHA.
 * Implements all 15 alpha_function modes from BeOS API for compatibility.
 */

#ifndef BLEND2D_DRAWING_MODE_ALPHA_H
#define BLEND2D_DRAWING_MODE_ALPHA_H

#include "PixelFormat.h"
#include "PatternHandler.h"
#include <blend2d.h>

// Helper functions are defined in Blend2DDrawingModeNative.h:
// - blend2d_draw_pixel()
// - blend2d_draw_hline()
// - blend2d_draw_vline()
// - blend2d_draw_hspan()

// ============================================================================
// Porter-Duff Operators - macro generator for all alpha compositing modes
// ============================================================================

// Macro to generate all 8 blend functions for a given Porter-Duff operator
#define GENERATE_PORTER_DUFF_FUNCTIONS(name, compop) \
	/* blend_pixel */ \
	static void \
	blend_pixel_##name##_native(int x, int y, \
							 const PixelFormat::color_type& c, \
							 uint8 cover, BLImage* image, BLContext* ctx, \
							 const PatternHandler* pattern) \
	{ \
		rgb_color color = pattern->ColorAt(x, y); \
		blend2d_draw_pixel(x, y, color, cover, ctx, compop); \
	} \
	 \
	/* blend_hline */ \
	static void \
	blend_hline_##name##_native(int x, int y, unsigned len, \
							 const PixelFormat::color_type& c, \
							 uint8 cover, BLImage* image, BLContext* ctx, \
							 const PatternHandler* pattern) \
	{ \
		if (pattern->IsSolid()) { \
			rgb_color color = pattern->HighColor(); \
			blend2d_draw_hline(x, y, len, color, cover, ctx, compop); \
		} else { \
			for (unsigned i = 0; i < len; i++, x++) { \
				rgb_color color = pattern->ColorAt(x, y); \
				blend2d_draw_pixel(x, y, color, cover, ctx, compop); \
			} \
		} \
	} \
	 \
	/* blend_vline */ \
	static void \
	blend_vline_##name##_native(int x, int y, unsigned len, \
							 const PixelFormat::color_type& c, \
							 uint8 cover, BLImage* image, BLContext* ctx, \
							 const PatternHandler* pattern) \
	{ \
		if (pattern->IsSolid()) { \
			rgb_color color = pattern->HighColor(); \
			blend2d_draw_vline(x, y, len, color, cover, ctx, compop); \
		} else { \
			for (unsigned i = 0; i < len; i++, y++) { \
				rgb_color color = pattern->ColorAt(x, y); \
				blend2d_draw_pixel(x, y, color, cover, ctx, compop); \
			} \
		} \
	} \
	 \
	/* blend_solid_hspan */ \
	static void \
	blend_solid_hspan_##name##_native(int x, int y, unsigned len, \
								   const PixelFormat::color_type& c, \
								   const uint8* covers, BLImage* image, \
								   BLContext* ctx, const PatternHandler* pattern) \
	{ \
		if (pattern->IsSolid()) { \
			rgb_color color = pattern->HighColor(); \
			blend2d_draw_hspan(x, y, len, color, covers, ctx, compop); \
		} else { \
			for (unsigned i = 0; i < len; i++, x++, covers++) { \
				if (*covers == 0) \
					continue; \
				rgb_color color = pattern->ColorAt(x, y); \
				blend2d_draw_pixel(x, y, color, *covers, ctx, compop); \
			} \
		} \
	} \
	 \
	/* blend_solid_vspan */ \
	static void \
	blend_solid_vspan_##name##_native(int x, int y, unsigned len, \
								   const PixelFormat::color_type& c, \
								   const uint8* covers, BLImage* image, \
								   BLContext* ctx, const PatternHandler* pattern) \
	{ \
		ctx->setCompOp(compop); \
		for (unsigned i = 0; i < len; i++, y++, covers++) { \
			if (*covers == 0) \
				continue; \
			rgb_color color = pattern->ColorAt(x, y); \
			blend2d_draw_pixel(x, y, color, *covers, ctx, compop); \
		} \
	} \
	 \
	/* blend_color_hspan */ \
	static void \
	blend_color_hspan_##name##_native(int x, int y, unsigned len, \
								   const PixelFormat::color_type* colors, \
								   const uint8* covers, uint8 cover, \
								   BLImage* image, BLContext* ctx, \
								   const PatternHandler* pattern) \
	{ \
		ctx->setCompOp(compop); \
		for (unsigned i = 0; i < len; i++, x++) { \
			uint8 alpha = covers ? covers[i] : cover; \
			if (alpha == 0) \
				continue; \
			const PixelFormat::color_type& c = colors[i]; \
			if (alpha == 255) { \
				ctx->fillRect(BLRect(x, y, 1, 1), \
							 BLRgba32(c.r, c.g, c.b, c.a)); \
			} else { \
				double prevAlpha = ctx->globalAlpha(); \
				ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0)); \
				ctx->fillRect(BLRect(x, y, 1, 1), \
							 BLRgba32(c.r, c.g, c.b, 255)); \
				ctx->setGlobalAlpha(prevAlpha); \
			} \
		} \
	} \
	 \
	/* blend_color_vspan */ \
	static void \
	blend_color_vspan_##name##_native(int x, int y, unsigned len, \
								   const PixelFormat::color_type* colors, \
								   const uint8* covers, uint8 cover, \
								   BLImage* image, BLContext* ctx, \
								   const PatternHandler* pattern) \
	{ \
		ctx->setCompOp(compop); \
		for (unsigned i = 0; i < len; i++, y++) { \
			uint8 alpha = covers ? covers[i] : cover; \
			if (alpha == 0) \
				continue; \
			const PixelFormat::color_type& c = colors[i]; \
			if (alpha == 255) { \
				ctx->fillRect(BLRect(x, y, 1, 1), \
							 BLRgba32(c.r, c.g, c.b, c.a)); \
			} else { \
				double prevAlpha = ctx->globalAlpha(); \
				ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0)); \
				ctx->fillRect(BLRect(x, y, 1, 1), \
							 BLRgba32(c.r, c.g, c.b, 255)); \
				ctx->setGlobalAlpha(prevAlpha); \
			} \
		} \
	}

// ============================================================================
// Generate all Porter-Duff alpha compositing functions
// ============================================================================
//
// BeOS API Mapping:
//   B_ALPHA_COMPOSITE_SOURCE_IN       → BL_COMP_OP_SRC_IN
//   B_ALPHA_COMPOSITE_SOURCE_OUT      → BL_COMP_OP_SRC_OUT
//   B_ALPHA_COMPOSITE_SOURCE_ATOP     → BL_COMP_OP_SRC_ATOP
//   B_ALPHA_COMPOSITE_DESTINATION_OVER  → BL_COMP_OP_DST_OVER
//   B_ALPHA_COMPOSITE_DESTINATION_IN    → BL_COMP_OP_DST_IN
//   B_ALPHA_COMPOSITE_DESTINATION_OUT   → BL_COMP_OP_DST_OUT
//   B_ALPHA_COMPOSITE_DESTINATION_ATOP  → BL_COMP_OP_DST_ATOP
//   B_ALPHA_COMPOSITE_XOR             → BL_COMP_OP_XOR
//   B_ALPHA_COMPOSITE_CLEAR           → BL_COMP_OP_CLEAR
//   B_ALPHA_COMPOSITE_DIFFERENCE      → BL_COMP_OP_DIFFERENCE
//
// Note: B_ALPHA_OVERLAY, B_ALPHA_COMPOSITE_SOURCE_OVER use existing
//       blend_*_over_native functions from Blend2DDrawingModeNative.h
//
// Note: B_ALPHA_COMPOSITE_LIGHTEN and B_ALPHA_COMPOSITE_DARKEN use existing
//       blend_*_max_native and blend_*_min_native respectively

GENERATE_PORTER_DUFF_FUNCTIONS(src_in, BL_COMP_OP_SRC_IN)
GENERATE_PORTER_DUFF_FUNCTIONS(src_out, BL_COMP_OP_SRC_OUT)
GENERATE_PORTER_DUFF_FUNCTIONS(src_atop, BL_COMP_OP_SRC_ATOP)
GENERATE_PORTER_DUFF_FUNCTIONS(dst_over, BL_COMP_OP_DST_OVER)
GENERATE_PORTER_DUFF_FUNCTIONS(dst_in, BL_COMP_OP_DST_IN)
GENERATE_PORTER_DUFF_FUNCTIONS(dst_out, BL_COMP_OP_DST_OUT)
GENERATE_PORTER_DUFF_FUNCTIONS(dst_atop, BL_COMP_OP_DST_ATOP)
GENERATE_PORTER_DUFF_FUNCTIONS(xor, BL_COMP_OP_XOR)
GENERATE_PORTER_DUFF_FUNCTIONS(clear, BL_COMP_OP_CLEAR)
GENERATE_PORTER_DUFF_FUNCTIONS(difference, BL_COMP_OP_DIFFERENCE)

#undef GENERATE_PORTER_DUFF_FUNCTIONS

#endif // BLEND2D_DRAWING_MODE_ALPHA_H
