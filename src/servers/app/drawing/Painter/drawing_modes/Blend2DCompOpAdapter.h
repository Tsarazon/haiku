/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Blend2D Porter-Duff composition operator adapter.
 * Replaces AggCompOpAdapter.h with Blend2D equivalents.
 */

#ifndef BLEND2D_COMP_OP_ADAPTER_H
#define BLEND2D_COMP_OP_ADAPTER_H

#include "PixelFormat.h"
#include "PatternHandler.h"
#include <blend2d.h>

// ============================================================================
// Porter-Duff Composition Operators Template
// ============================================================================

// Template adapter for Porter-Duff operators using Blend2D's BLCompOp
template<BLCompOp CompOp>
class Blend2DCompOpAdapter {
 public:
	typedef PixelFormat::color_type color_type;

	// Blend single pixel
	static void blend_pixel(int x, int y, const color_type& c, uint8 cover,
						   BLImage* image, BLContext* ctx,
						   const PatternHandler* pattern)
	{
		rgb_color color = pattern->ColorAt(x, y);
		
		ctx->setCompOp(CompOp);
		
		if (cover == 255) {
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(color.red, color.green, color.blue,
						 		 color.alpha));
		} else {
			double prevAlpha = ctx->globalAlpha();
			ctx->setGlobalAlpha((cover / 255.0) * (color.alpha / 255.0));
			ctx->fillRect(BLRect(x, y, 1, 1),
						 BLRgba32(color.red, color.green, color.blue, 255));
			ctx->setGlobalAlpha(prevAlpha);
		}
	}

	// Blend horizontal line
	static void blend_hline(int x, int y, unsigned len, const color_type& c,
						   uint8 cover, BLImage* image, BLContext* ctx,
						   const PatternHandler* pattern)
	{
		ctx->setCompOp(CompOp);
		
		if (pattern->IsSolid()) {
			rgb_color color = pattern->HighColor();
			
			if (cover == 255) {
				ctx->fillRect(BLRect(x, y, len, 1),
							 BLRgba32(color.red, color.green, color.blue,
							 		 color.alpha));
			} else {
				double prevAlpha = ctx->globalAlpha();
				ctx->setGlobalAlpha((cover / 255.0) * (color.alpha / 255.0));
				ctx->fillRect(BLRect(x, y, len, 1),
							 BLRgba32(color.red, color.green, color.blue, 255));
				ctx->setGlobalAlpha(prevAlpha);
			}
		} else {
			// Pattern - draw pixel by pixel
			for (unsigned i = 0; i < len; i++, x++) {
				blend_pixel(x, y, c, cover, image, ctx, pattern);
			}
		}
	}

	// Blend horizontal span with coverage array
	static void blend_solid_hspan(int x, int y, unsigned len,
								 const color_type& c, const uint8* covers,
								 BLImage* image, BLContext* ctx,
								 const PatternHandler* pattern)
	{
		ctx->setCompOp(CompOp);
		
		for (unsigned i = 0; i < len; i++, x++, covers++) {
			if (*covers == 0)
				continue;
			
			rgb_color color = pattern->ColorAt(x, y);
			
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

	// Blend horizontal span with subpixel coverage
	static void blend_solid_hspan_subpix(int x, int y, unsigned len,
									    const color_type& c,
									    const uint8* covers,
									    BLImage* image, BLContext* ctx,
									    const PatternHandler* pattern)
	{
		ctx->setCompOp(CompOp);
		
		// Subpixel array has 3 values per pixel
		unsigned pixelCount = len / 3;
		
		for (unsigned i = 0; i < pixelCount; i++, x++) {
			// Simple grayscale averaging
			uint8 avgCover = (covers[0] + covers[1] + covers[2]) / 3;
			covers += 3;
			
			if (avgCover == 0)
				continue;
			
			rgb_color color = pattern->ColorAt(x, y);
			
			if (avgCover == 255) {
				ctx->fillRect(BLRect(x, y, 1, 1),
							 BLRgba32(color.red, color.green, color.blue,
							 		 color.alpha));
			} else {
				double prevAlpha = ctx->globalAlpha();
				ctx->setGlobalAlpha((avgCover / 255.0) * (color.alpha / 255.0));
				ctx->fillRect(BLRect(x, y, 1, 1),
							 BLRgba32(color.red, color.green, color.blue, 255));
				ctx->setGlobalAlpha(prevAlpha);
			}
		}
	}

	// Blend vertical span with coverage array
	static void blend_solid_vspan(int x, int y, unsigned len,
								 const color_type& c, const uint8* covers,
								 BLImage* image, BLContext* ctx,
								 const PatternHandler* pattern)
	{
		ctx->setCompOp(CompOp);
		
		for (unsigned i = 0; i < len; i++, y++, covers++) {
			if (*covers == 0)
				continue;
			
			rgb_color color = pattern->ColorAt(x, y);
			
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

	// Blend color horizontal span
	static void blend_color_hspan(int x, int y, unsigned len,
								 const color_type* colors,
								 const uint8* covers, uint8 cover,
								 BLImage* image, BLContext* ctx,
								 const PatternHandler* pattern)
	{
		ctx->setCompOp(CompOp);
		
		// TODO: Implement proper color span blending
		// For now, just use solid hspan with pattern colors
		for (unsigned i = 0; i < len; i++, x++) {
			uint8 alpha = covers ? covers[i] : cover;
			if (alpha == 0)
				continue;
			
			rgb_color color = pattern->ColorAt(x, y);
			
			if (alpha == 255) {
				ctx->fillRect(BLRect(x, y, 1, 1),
							 BLRgba32(color.red, color.green, color.blue,
							 		 color.alpha));
			} else {
				double prevAlpha = ctx->globalAlpha();
				ctx->setGlobalAlpha((alpha / 255.0) * (color.alpha / 255.0));
				ctx->fillRect(BLRect(x, y, 1, 1),
							 BLRgba32(color.red, color.green, color.blue, 255));
				ctx->setGlobalAlpha(prevAlpha);
			}
		}
	}
};

// ============================================================================
// Type aliases for specific Porter-Duff operators
// ============================================================================

typedef Blend2DCompOpAdapter<BL_COMP_OP_SRC_IN>     blend2d_src_in;
typedef Blend2DCompOpAdapter<BL_COMP_OP_SRC_OUT>    blend2d_src_out;
typedef Blend2DCompOpAdapter<BL_COMP_OP_SRC_ATOP>   blend2d_src_atop;
typedef Blend2DCompOpAdapter<BL_COMP_OP_DST_OVER>   blend2d_dst_over;
typedef Blend2DCompOpAdapter<BL_COMP_OP_DST_IN>     blend2d_dst_in;
typedef Blend2DCompOpAdapter<BL_COMP_OP_DST_OUT>    blend2d_dst_out;
typedef Blend2DCompOpAdapter<BL_COMP_OP_DST_ATOP>   blend2d_dst_atop;
typedef Blend2DCompOpAdapter<BL_COMP_OP_XOR>        blend2d_xor;
typedef Blend2DCompOpAdapter<BL_COMP_OP_CLEAR>      blend2d_clear;
typedef Blend2DCompOpAdapter<BL_COMP_OP_DIFFERENCE> blend2d_difference;
typedef Blend2DCompOpAdapter<BL_COMP_OP_LIGHTEN>    blend2d_lighten;
typedef Blend2DCompOpAdapter<BL_COMP_OP_DARKEN>     blend2d_darken;

#endif // BLEND2D_COMP_OP_ADAPTER_H
