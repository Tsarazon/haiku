/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender_Composition.cpp - Composition and blending operations using AGG
 */

#include "AGGRender.h"

#include <agg_basics.h>
#include <agg_pixfmt_rgba.h>
#include <agg_color_rgba.h>

#include <SupportDefs.h>
#include <string.h>

// Static member initialization
AGGRender::comp_op_e AGGRender::sCurrentOperation = AGGRender::COMP_OP_SRC_OVER;


status_t
AGGRender::InitializeComposition()
{
	sCurrentOperation = COMP_OP_SRC_OVER;
	return B_OK;
}


void
AGGRender::SetCompositionOperation(comp_op_e op)
{
	sCurrentOperation = op;
}


AGGRender::comp_op_e
AGGRender::GetCompositionOperation() const
{
	return sCurrentOperation;
}


void
AGGRender::BlendPixel(uint8* dest, const uint8* src, comp_op_e op)
{
	if (dest == NULL || src == NULL)
		return;

	switch (op) {
		case COMP_OP_CLEAR:
			_BlendPixelClear(dest);
			break;
		case COMP_OP_SRC_OVER:
			_BlendPixelSrcOver(dest, src);
			break;
		case COMP_OP_DARKEN:
			_BlendPixelDarken(dest, src);
			break;
		case COMP_OP_LIGHTEN:
			_BlendPixelLighten(dest, src);
			break;
		case COMP_OP_DIFFERENCE:
			_BlendPixelDifference(dest, src);
			break;
		case COMP_OP_XOR:
			_BlendPixelXor(dest, src);
			break;
		default:
			// For other operations, use AGG's composition operators
			agg::rgba8 destColor(dest[0], dest[1], dest[2], dest[3]);
			agg::rgba8 srcColor(src[0], src[1], src[2], src[3]);
			
			// Apply AGG composition operation using proper blend_pix
			switch(op) {
				case COMP_OP_SRC_OVER:
					agg::comp_op_rgba_src_over<agg::rgba8, agg::order_rgba>::blend_pix(
						dest, srcColor.r, srcColor.g, srcColor.b, srcColor.a, 255);
					break;
				default:
					// Fallback to src_over for unsupported operations
					agg::comp_op_rgba_src_over<agg::rgba8, agg::order_rgba>::blend_pix(
						dest, srcColor.r, srcColor.g, srcColor.b, srcColor.a, 255);
					break;
			}
			break;
	}
}


void
AGGRender::BlendPixelAlpha(uint8* dest, const uint8* src, uint8 alpha, comp_op_e op)
{
	if (dest == NULL || src == NULL || alpha == 0)
		return;

	if (alpha == 255) {
		BlendPixel(dest, src, op);
		return;
	}

	// Apply alpha to source pixel
	uint8 srcWithAlpha[4];
	srcWithAlpha[0] = _MultiplyAlpha(src[0], alpha);
	srcWithAlpha[1] = _MultiplyAlpha(src[1], alpha);
	srcWithAlpha[2] = _MultiplyAlpha(src[2], alpha);
	srcWithAlpha[3] = _MultiplyAlpha(src[3], alpha);

	BlendPixel(dest, srcWithAlpha, op);
}


void
AGGRender::CompositeBuffer(uint8* dest, const uint8* src,
									   int width, int height,
									   int destStride, int srcStride,
									   comp_op_e op)
{
	if (dest == NULL || src == NULL || width <= 0 || height <= 0)
		return;

	for (int y = 0; y < height; y++) {
		uint8* destRow = dest + y * destStride;
		const uint8* srcRow = src + y * srcStride;

		for (int x = 0; x < width; x++) {
			BlendPixel(destRow + x * 4, srcRow + x * 4, op);
		}
	}
}


void
AGGRender::CompositeBufferMasked(uint8* dest, const uint8* src, const uint8* mask,
											 int width, int height,
											 int destStride, int srcStride, int maskStride,
											 comp_op_e op)
{
	if (dest == NULL || src == NULL || mask == NULL || width <= 0 || height <= 0)
		return;

	for (int y = 0; y < height; y++) {
		uint8* destRow = dest + y * destStride;
		const uint8* srcRow = src + y * srcStride;
		const uint8* maskRow = mask + y * maskStride;

		for (int x = 0; x < width; x++) {
			uint8 alpha = maskRow[x];
			if (alpha > 0) {
				BlendPixelAlpha(destRow + x * 4, srcRow + x * 4, alpha, op);
			}
		}
	}
}


// Porter-Duff composition operations
void
AGGRender::Clear(uint8* dest, int width, int height, int stride)
{
	if (dest == NULL || width <= 0 || height <= 0)
		return;

	for (int y = 0; y < height; y++) {
		uint8* row = dest + y * stride;
		memset(row, 0, width * 4);
	}
}


void
AGGRender::Copy(uint8* dest, const uint8* src, int width, int height,
							int destStride, int srcStride)
{
	if (dest == NULL || src == NULL || width <= 0 || height <= 0)
		return;

	for (int y = 0; y < height; y++) {
		uint8* destRow = dest + y * destStride;
		const uint8* srcRow = src + y * srcStride;
		memcpy(destRow, srcRow, width * 4);
	}
}


void
AGGRender::SourceOver(uint8* dest, const uint8* src, int width, int height,
								  int destStride, int srcStride)
{
	CompositeBuffer(dest, src, width, height, destStride, srcStride, COMP_OP_SRC_OVER);
}


void
AGGRender::SourceAtop(uint8* dest, const uint8* src, int width, int height,
								  int destStride, int srcStride)
{
	CompositeBuffer(dest, src, width, height, destStride, srcStride, COMP_OP_SRC_ATOP);
}


void
AGGRender::DestinationOver(uint8* dest, const uint8* src, int width, int height,
									   int destStride, int srcStride)
{
	CompositeBuffer(dest, src, width, height, destStride, srcStride, COMP_OP_DST_OVER);
}


void
AGGRender::DestinationIn(uint8* dest, const uint8* src, int width, int height,
									 int destStride, int srcStride)
{
	CompositeBuffer(dest, src, width, height, destStride, srcStride, COMP_OP_DST_IN);
}


void
AGGRender::DestinationOut(uint8* dest, const uint8* src, int width, int height,
									  int destStride, int srcStride)
{
	CompositeBuffer(dest, src, width, height, destStride, srcStride, COMP_OP_DST_OUT);
}


void
AGGRender::DestinationAtop(uint8* dest, const uint8* src, int width, int height,
									   int destStride, int srcStride)
{
	CompositeBuffer(dest, src, width, height, destStride, srcStride, COMP_OP_DST_ATOP);
}


void
AGGRender::Xor(uint8* dest, const uint8* src, int width, int height,
						   int destStride, int srcStride)
{
	CompositeBuffer(dest, src, width, height, destStride, srcStride, COMP_OP_XOR);
}


void
AGGRender::Darken(uint8* dest, const uint8* src, int width, int height,
							  int destStride, int srcStride)
{
	CompositeBuffer(dest, src, width, height, destStride, srcStride, COMP_OP_DARKEN);
}


void
AGGRender::Lighten(uint8* dest, const uint8* src, int width, int height,
							   int destStride, int srcStride)
{
	CompositeBuffer(dest, src, width, height, destStride, srcStride, COMP_OP_LIGHTEN);
}


void
AGGRender::Difference(uint8* dest, const uint8* src, int width, int height,
								  int destStride, int srcStride)
{
	CompositeBuffer(dest, src, width, height, destStride, srcStride, COMP_OP_DIFFERENCE);
}


// Private helper functions
void
AGGRender::_BlendPixelClear(uint8* dest)
{
	dest[0] = 0; // R
	dest[1] = 0; // G
	dest[2] = 0; // B
	dest[3] = 0; // A
}


void
AGGRender::_BlendPixelSrcOver(uint8* dest, const uint8* src)
{
	uint8 srcAlpha = src[3];
	if (srcAlpha == 0)
		return;
		
	if (srcAlpha == 255) {
		dest[0] = src[0];
		dest[1] = src[1];
		dest[2] = src[2];
		dest[3] = src[3];
		return;
	}

	uint8 destAlpha = dest[3];
	uint8 invSrcAlpha = 255 - srcAlpha;

	dest[0] = (src[0] * srcAlpha + dest[0] * invSrcAlpha) / 255;
	dest[1] = (src[1] * srcAlpha + dest[1] * invSrcAlpha) / 255;
	dest[2] = (src[2] * srcAlpha + dest[2] * invSrcAlpha) / 255;
	dest[3] = srcAlpha + (destAlpha * invSrcAlpha) / 255;
}


void
AGGRender::_BlendPixelDarken(uint8* dest, const uint8* src)
{
	dest[0] = min_c(dest[0], src[0]);
	dest[1] = min_c(dest[1], src[1]);
	dest[2] = min_c(dest[2], src[2]);
	// Alpha blending for darken mode
	_BlendPixelSrcOver(dest, src);
}


void
AGGRender::_BlendPixelLighten(uint8* dest, const uint8* src)
{
	dest[0] = max_c(dest[0], src[0]);
	dest[1] = max_c(dest[1], src[1]);
	dest[2] = max_c(dest[2], src[2]);
	// Alpha blending for lighten mode
	_BlendPixelSrcOver(dest, src);
}


void
AGGRender::_BlendPixelDifference(uint8* dest, const uint8* src)
{
	dest[0] = abs(dest[0] - src[0]);
	dest[1] = abs(dest[1] - src[1]);
	dest[2] = abs(dest[2] - src[2]);
	// Alpha blending for difference mode
	_BlendPixelSrcOver(dest, src);
}


void
AGGRender::_BlendPixelXor(uint8* dest, const uint8* src)
{
	uint8 srcAlpha = src[3];
	uint8 destAlpha = dest[3];
	uint8 invSrcAlpha = 255 - srcAlpha;
	uint8 invDestAlpha = 255 - destAlpha;

	dest[0] = (src[0] * invDestAlpha + dest[0] * invSrcAlpha) / 255;
	dest[1] = (src[1] * invDestAlpha + dest[1] * invSrcAlpha) / 255;
	dest[2] = (src[2] * invDestAlpha + dest[2] * invSrcAlpha) / 255;
	dest[3] = (srcAlpha * invDestAlpha + destAlpha * invSrcAlpha) / 255;
}


// Color math utilities
uint8
AGGRender::_MultiplyAlpha(uint8 value, uint8 alpha)
{
	return (value * alpha) / 255;
}


uint8
AGGRender::_PremultiplyAlpha(uint8 value, uint8 alpha)
{
	return _MultiplyAlpha(value, alpha);
}


uint8
AGGRender::_UnpremultiplyAlpha(uint8 value, uint8 alpha)
{
	if (alpha == 0)
		return 0;
	return min_c(255, (value * 255) / alpha);
}