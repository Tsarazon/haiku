/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Native Blend2D drawing modes - modes that map directly to BLCompOp.
 * Covers: B_OP_COPY, B_OP_OVER, B_OP_ADD, B_OP_SUBTRACT, B_OP_MIN, B_OP_MAX
 * Also covers B_OP_ALPHA variants that use Porter-Duff operators.
 */

#ifndef BLEND2D_DRAWING_MODE_NATIVE_H
#define BLEND2D_DRAWING_MODE_NATIVE_H

#include "Blend2DPixelFormat.h"
#include "PatternHandler.h"
#include <blend2d.h>

// Helper function to draw a single pixel using BLContext
static inline void
blend2d_draw_pixel(int x, int y, const rgb_color& color, uint8 cover,
				  BLContext* ctx, BLCompOp compOp)
{
	ctx->setCompOp(compOp);
	
	if (cover == 255) {
		// Full coverage - direct draw
		ctx->fillRect(BLRect(x, y, 1, 1),
					 BLRgba32(color.red, color.green, color.blue, color.alpha));
	} else {
		// Partial coverage - use globalAlpha
		double prevAlpha = ctx->globalAlpha();
		ctx->setGlobalAlpha((cover / 255.0) * (color.alpha / 255.0));
		ctx->fillRect(BLRect(x, y, 1, 1),
					 BLRgba32(color.red, color.green, color.blue, 255));
		ctx->setGlobalAlpha(prevAlpha);
	}
}

// Helper function to draw horizontal line
static inline void
blend2d_draw_hline(int x, int y, unsigned len, const rgb_color& color,
				  uint8 cover, BLContext* ctx, BLCompOp compOp)
{
	ctx->setCompOp(compOp);
	
	if (cover == 255) {
		ctx->fillRect(BLRect(x, y, len, 1),
					 BLRgba32(color.red, color.green, color.blue, color.alpha));
	} else {
		double prevAlpha = ctx->globalAlpha();
		ctx->setGlobalAlpha((cover / 255.0) * (color.alpha / 255.0));
		ctx->fillRect(BLRect(x, y, len, 1),
					 BLRgba32(color.red, color.green, color.blue, 255));
		ctx->setGlobalAlpha(prevAlpha);
	}
}

// Helper function to draw vertical line
static inline void
blend2d_draw_vline(int x, int y, unsigned len, const rgb_color& color,
				  uint8 cover, BLContext* ctx, BLCompOp compOp)
{
	ctx->setCompOp(compOp);
	
	if (cover == 255) {
		ctx->fillRect(BLRect(x, y, 1, len),
					 BLRgba32(color.red, color.green, color.blue, color.alpha));
	} else {
		double prevAlpha = ctx->globalAlpha();
		ctx->setGlobalAlpha((cover / 255.0) * (color.alpha / 255.0));
		ctx->fillRect(BLRect(x, y, 1, len),
					 BLRgba32(color.red, color.green, color.blue, 255));
		ctx->setGlobalAlpha(prevAlpha);
	}
}

// Helper function to draw horizontal span with coverage array
static inline void
blend2d_draw_hspan(int x, int y, unsigned len,
				  const rgb_color& color, const uint8* covers,
				  BLContext* ctx, BLCompOp compOp)
{
	ctx->setCompOp(compOp);
	
	// Draw pixel by pixel with individual coverage
	for (unsigned i = 0; i < len; i++, x++, covers++) {
		if (*covers == 0)
			continue;
			
		if (*covers == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(color.red, color.green, color.blue,
						 		 color.alpha));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((*covers / 255.0) * (color.alpha / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(color.red, color.green, color.blue, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

// ============================================================================
// B_OP_COPY - Direct copy (BL_COMP_OP_SRC_COPY)
// ============================================================================

static void
blend_pixel_copy_native(int x, int y,
					   const Blend2DPixelFormat::color_type& c,
					   uint8 cover, BLImage* image, BLContext* ctx,
					   const PatternHandler* pattern)
{
	rgb_color color = pattern->ColorAt(x, y);
	blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_SRC_COPY);
}

static void
blend_hline_copy_native(int x, int y, unsigned len,
					   const Blend2DPixelFormat::color_type& c,
					   uint8 cover, BLImage* image, BLContext* ctx,
					   const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hline(x, y, len, color, cover, ctx, BL_COMP_OP_SRC_COPY);
	} else {
		// Pattern - draw pixel by pixel
		for (unsigned i = 0; i < len; i++, x++) {
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_SRC_COPY);
		}
	}
}

static void
blend_vline_copy_native(int x, int y, unsigned len,
					   const Blend2DPixelFormat::color_type& c,
					   uint8 cover, BLImage* image, BLContext* ctx,
					   const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_vline(x, y, len, color, cover, ctx, BL_COMP_OP_SRC_COPY);
	} else {
		// Pattern - draw pixel by pixel
		for (unsigned i = 0; i < len; i++, y++) {
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_SRC_COPY);
		}
	}
}

static void
blend_solid_hspan_copy_native(int x, int y, unsigned len,
							 const Blend2DPixelFormat::color_type& c,
							 const uint8* covers, BLImage* image,
							 BLContext* ctx, const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hspan(x, y, len, color, covers, ctx, BL_COMP_OP_SRC_COPY);
	} else {
		// Pattern - draw pixel by pixel
		for (unsigned i = 0; i < len; i++, x++, covers++) {
			if (*covers == 0)
				continue;
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_SRC_COPY);
		}
	}
}

static void
blend_solid_vspan_copy_native(int x, int y, unsigned len,
							 const Blend2DPixelFormat::color_type& c,
							 const uint8* covers, BLImage* image,
							 BLContext* ctx, const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_SRC_COPY);
	
	for (unsigned i = 0; i < len; i++, y++, covers++) {
		if (*covers == 0)
			continue;
		rgb_color color = pattern->ColorAt(x, y);
		blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_SRC_COPY);
	}
}

// ============================================================================
// B_OP_OVER - Source over (BL_COMP_OP_SRC_OVER) - default blending
// ============================================================================

static void
blend_pixel_over_native(int x, int y,
					   const Blend2DPixelFormat::color_type& c,
					   uint8 cover, BLImage* image, BLContext* ctx,
					   const PatternHandler* pattern)
{
	rgb_color color = pattern->ColorAt(x, y);
	blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_SRC_OVER);
}

static void
blend_hline_over_native(int x, int y, unsigned len,
					   const Blend2DPixelFormat::color_type& c,
					   uint8 cover, BLImage* image, BLContext* ctx,
					   const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hline(x, y, len, color, cover, ctx, BL_COMP_OP_SRC_OVER);
	} else {
		for (unsigned i = 0; i < len; i++, x++) {
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_SRC_OVER);
		}
	}
}

static void
blend_vline_over_native(int x, int y, unsigned len,
					   const Blend2DPixelFormat::color_type& c,
					   uint8 cover, BLImage* image, BLContext* ctx,
					   const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_vline(x, y, len, color, cover, ctx, BL_COMP_OP_SRC_OVER);
	} else {
		for (unsigned i = 0; i < len; i++, y++) {
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_SRC_OVER);
		}
	}
}

static void
blend_solid_hspan_over_native(int x, int y, unsigned len,
							 const Blend2DPixelFormat::color_type& c,
							 const uint8* covers, BLImage* image,
							 BLContext* ctx, const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hspan(x, y, len, color, covers, ctx, BL_COMP_OP_SRC_OVER);
	} else {
		for (unsigned i = 0; i < len; i++, x++, covers++) {
			if (*covers == 0)
				continue;
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_SRC_OVER);
		}
	}
}

static void
blend_solid_vspan_over_native(int x, int y, unsigned len,
							 const Blend2DPixelFormat::color_type& c,
							 const uint8* covers, BLImage* image,
							 BLContext* ctx, const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_SRC_OVER);
	
	for (unsigned i = 0; i < len; i++, y++, covers++) {
		if (*covers == 0)
			continue;
		rgb_color color = pattern->ColorAt(x, y);
		blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_SRC_OVER);
	}
}

// ============================================================================
// B_OP_ADD - Additive blending (BL_COMP_OP_PLUS)
// ============================================================================

static void
blend_pixel_add_native(int x, int y,
					  const Blend2DPixelFormat::color_type& c,
					  uint8 cover, BLImage* image, BLContext* ctx,
					  const PatternHandler* pattern)
{
	rgb_color color = pattern->ColorAt(x, y);
	blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_PLUS);
}

static void
blend_hline_add_native(int x, int y, unsigned len,
					  const Blend2DPixelFormat::color_type& c,
					  uint8 cover, BLImage* image, BLContext* ctx,
					  const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hline(x, y, len, color, cover, ctx, BL_COMP_OP_PLUS);
	} else {
		for (unsigned i = 0; i < len; i++, x++) {
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_PLUS);
		}
	}
}

static void
blend_solid_hspan_add_native(int x, int y, unsigned len,
							const Blend2DPixelFormat::color_type& c,
							const uint8* covers, BLImage* image,
							BLContext* ctx, const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hspan(x, y, len, color, covers, ctx, BL_COMP_OP_PLUS);
	} else {
		for (unsigned i = 0; i < len; i++, x++, covers++) {
			if (*covers == 0)
				continue;
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_PLUS);
		}
	}
}

static void
blend_solid_vspan_add_native(int x, int y, unsigned len,
							const Blend2DPixelFormat::color_type& c,
							const uint8* covers, BLImage* image,
							BLContext* ctx, const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_PLUS);
	
	for (unsigned i = 0; i < len; i++, y++, covers++) {
		if (*covers == 0)
			continue;
		rgb_color color = pattern->ColorAt(x, y);
		blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_PLUS);
	}
}

// ============================================================================
// B_OP_SUBTRACT - Subtractive blending (BL_COMP_OP_MINUS)
// ============================================================================

static void
blend_pixel_subtract_native(int x, int y,
						   const Blend2DPixelFormat::color_type& c,
						   uint8 cover, BLImage* image, BLContext* ctx,
						   const PatternHandler* pattern)
{
	rgb_color color = pattern->ColorAt(x, y);
	blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_MINUS);
}

static void
blend_hline_subtract_native(int x, int y, unsigned len,
						   const Blend2DPixelFormat::color_type& c,
						   uint8 cover, BLImage* image, BLContext* ctx,
						   const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hline(x, y, len, color, cover, ctx, BL_COMP_OP_MINUS);
	} else {
		for (unsigned i = 0; i < len; i++, x++) {
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_MINUS);
		}
	}
}

static void
blend_solid_hspan_subtract_native(int x, int y, unsigned len,
								 const Blend2DPixelFormat::color_type& c,
								 const uint8* covers, BLImage* image,
								 BLContext* ctx, const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hspan(x, y, len, color, covers, ctx, BL_COMP_OP_MINUS);
	} else {
		for (unsigned i = 0; i < len; i++, x++, covers++) {
			if (*covers == 0)
				continue;
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_MINUS);
		}
	}
}

static void
blend_solid_vspan_subtract_native(int x, int y, unsigned len,
								 const Blend2DPixelFormat::color_type& c,
								 const uint8* covers, BLImage* image,
								 BLContext* ctx, const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_MINUS);
	
	for (unsigned i = 0; i < len; i++, y++, covers++) {
		if (*covers == 0)
			continue;
		rgb_color color = pattern->ColorAt(x, y);
		blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_MINUS);
	}
}

// ============================================================================
// B_OP_MIN - Darken (BL_COMP_OP_DARKEN)
// ============================================================================

static void
blend_pixel_min_native(int x, int y,
					  const Blend2DPixelFormat::color_type& c,
					  uint8 cover, BLImage* image, BLContext* ctx,
					  const PatternHandler* pattern)
{
	rgb_color color = pattern->ColorAt(x, y);
	blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_DARKEN);
}

static void
blend_hline_min_native(int x, int y, unsigned len,
					  const Blend2DPixelFormat::color_type& c,
					  uint8 cover, BLImage* image, BLContext* ctx,
					  const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hline(x, y, len, color, cover, ctx, BL_COMP_OP_DARKEN);
	} else {
		for (unsigned i = 0; i < len; i++, x++) {
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_DARKEN);
		}
	}
}

static void
blend_solid_hspan_min_native(int x, int y, unsigned len,
							const Blend2DPixelFormat::color_type& c,
							const uint8* covers, BLImage* image,
							BLContext* ctx, const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hspan(x, y, len, color, covers, ctx, BL_COMP_OP_DARKEN);
	} else {
		for (unsigned i = 0; i < len; i++, x++, covers++) {
			if (*covers == 0)
				continue;
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_DARKEN);
		}
	}
}

static void
blend_solid_vspan_min_native(int x, int y, unsigned len,
							const Blend2DPixelFormat::color_type& c,
							const uint8* covers, BLImage* image,
							BLContext* ctx, const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_DARKEN);
	
	for (unsigned i = 0; i < len; i++, y++, covers++) {
		if (*covers == 0)
			continue;
		rgb_color color = pattern->ColorAt(x, y);
		blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_DARKEN);
	}
}

// ============================================================================
// B_OP_MAX - Lighten (BL_COMP_OP_LIGHTEN)
// ============================================================================

static void
blend_pixel_max_native(int x, int y,
					  const Blend2DPixelFormat::color_type& c,
					  uint8 cover, BLImage* image, BLContext* ctx,
					  const PatternHandler* pattern)
{
	rgb_color color = pattern->ColorAt(x, y);
	blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_LIGHTEN);
}

static void
blend_hline_max_native(int x, int y, unsigned len,
					  const Blend2DPixelFormat::color_type& c,
					  uint8 cover, BLImage* image, BLContext* ctx,
					  const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hline(x, y, len, color, cover, ctx, BL_COMP_OP_LIGHTEN);
	} else {
		for (unsigned i = 0; i < len; i++, x++) {
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, cover, ctx, BL_COMP_OP_LIGHTEN);
		}
	}
}

static void
blend_solid_hspan_max_native(int x, int y, unsigned len,
							const Blend2DPixelFormat::color_type& c,
							const uint8* covers, BLImage* image,
							BLContext* ctx, const PatternHandler* pattern)
{
	if (pattern->IsSolid()) {
		rgb_color color = pattern->HighColor();
		blend2d_draw_hspan(x, y, len, color, covers, ctx, BL_COMP_OP_LIGHTEN);
	} else {
		for (unsigned i = 0; i < len; i++, x++, covers++) {
			if (*covers == 0)
				continue;
			rgb_color color = pattern->ColorAt(x, y);
			blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_LIGHTEN);
		}
	}
}

static void
blend_solid_vspan_max_native(int x, int y, unsigned len,
							const Blend2DPixelFormat::color_type& c,
							const uint8* covers, BLImage* image,
							BLContext* ctx, const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_LIGHTEN);
	
	for (unsigned i = 0; i < len; i++, y++, covers++) {
		if (*covers == 0)
			continue;
		rgb_color color = pattern->ColorAt(x, y);
		blend2d_draw_pixel(x, y, color, *covers, ctx, BL_COMP_OP_LIGHTEN);
	}
}

// ============================================================================
// blend_color_hspan functions - horizontal span with color array
// ============================================================================

static void
blend_color_hspan_copy_native(int x, int y, unsigned len,
							   const Blend2DPixelFormat::color_type* colors,
							   const uint8* covers, uint8 cover,
							   BLImage* image, BLContext* ctx,
							   const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_SRC_COPY);

	for (unsigned i = 0; i < len; i++, x++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

static void
blend_color_hspan_over_native(int x, int y, unsigned len,
							   const Blend2DPixelFormat::color_type* colors,
							   const uint8* covers, uint8 cover,
							   BLImage* image, BLContext* ctx,
							   const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_SRC_OVER);

	for (unsigned i = 0; i < len; i++, x++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

static void
blend_color_hspan_add_native(int x, int y, unsigned len,
							  const Blend2DPixelFormat::color_type* colors,
							  const uint8* covers, uint8 cover,
							  BLImage* image, BLContext* ctx,
							  const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_PLUS);

	for (unsigned i = 0; i < len; i++, x++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

static void
blend_color_hspan_subtract_native(int x, int y, unsigned len,
								   const Blend2DPixelFormat::color_type* colors,
								   const uint8* covers, uint8 cover,
								   BLImage* image, BLContext* ctx,
								   const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_MINUS);

	for (unsigned i = 0; i < len; i++, x++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

static void
blend_color_hspan_min_native(int x, int y, unsigned len,
							  const Blend2DPixelFormat::color_type* colors,
							  const uint8* covers, uint8 cover,
							  BLImage* image, BLContext* ctx,
							  const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_DARKEN);

	for (unsigned i = 0; i < len; i++, x++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

static void
blend_color_hspan_max_native(int x, int y, unsigned len,
							  const Blend2DPixelFormat::color_type* colors,
							  const uint8* covers, uint8 cover,
							  BLImage* image, BLContext* ctx,
							  const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_LIGHTEN);

	for (unsigned i = 0; i < len; i++, x++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

// ============================================================================
// blend_color_vspan functions - vertical span with color array
// ============================================================================

static void
blend_color_vspan_copy_native(int x, int y, unsigned len,
							   const Blend2DPixelFormat::color_type* colors,
							   const uint8* covers, uint8 cover,
							   BLImage* image, BLContext* ctx,
							   const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_SRC_COPY);

	for (unsigned i = 0; i < len; i++, y++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

static void
blend_color_vspan_over_native(int x, int y, unsigned len,
							   const Blend2DPixelFormat::color_type* colors,
							   const uint8* covers, uint8 cover,
							   BLImage* image, BLContext* ctx,
							   const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_SRC_OVER);

	for (unsigned i = 0; i < len; i++, y++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

static void
blend_color_vspan_add_native(int x, int y, unsigned len,
							  const Blend2DPixelFormat::color_type* colors,
							  const uint8* covers, uint8 cover,
							  BLImage* image, BLContext* ctx,
							  const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_PLUS);

	for (unsigned i = 0; i < len; i++, y++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

static void
blend_color_vspan_subtract_native(int x, int y, unsigned len,
								   const Blend2DPixelFormat::color_type* colors,
								   const uint8* covers, uint8 cover,
								   BLImage* image, BLContext* ctx,
								   const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_MINUS);

	for (unsigned i = 0; i < len; i++, y++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

static void
blend_color_vspan_min_native(int x, int y, unsigned len,
							  const Blend2DPixelFormat::color_type* colors,
							  const uint8* covers, uint8 cover,
							  BLImage* image, BLContext* ctx,
							  const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_DARKEN);

	for (unsigned i = 0; i < len; i++, y++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

static void
blend_color_vspan_max_native(int x, int y, unsigned len,
							  const Blend2DPixelFormat::color_type* colors,
							  const uint8* covers, uint8 cover,
							  BLImage* image, BLContext* ctx,
							  const PatternHandler* pattern)
{
	ctx->setCompOp(BL_COMP_OP_LIGHTEN);

	for (unsigned i = 0; i < len; i++, y++) {
		uint8 alpha = covers ? covers[i] : cover;
		if (alpha == 0)
			continue;

		const Blend2DPixelFormat::color_type& c = colors[i];

		if (alpha == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, c.a));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((alpha / 255.0) * (c.a / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(c.r, c.g, c.b, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}
}

#endif // BLEND2D_DRAWING_MODE_NATIVE_H
