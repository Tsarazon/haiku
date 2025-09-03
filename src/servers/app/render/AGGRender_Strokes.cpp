/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender_Strokes.cpp - Stroke and line style operations using AGG
 */

#include "AGGRender.h"

#include <agg_basics.h>
#include <agg_conv_stroke.h>
#include <agg_alpha_mask_u8.h>
#include <agg_clipped_alpha_mask.h>
#include <agg_pixfmt_amask_adaptor.h>
#include <agg_path_storage.h>
#include <agg_rasterizer_outline_aa.h>
#include <agg_line_aa_basics.h>

#include <SupportDefs.h>
#include <string.h>


// Line Cap Operations
AGGRender::line_cap_e
AGGRender::ConvertToAGGLineCap(uint32 haikuLineCap)
{
	switch (haikuLineCap) {
		case B_BUTT_CAP:
			return LINE_CAP_BUTT;
		case B_ROUND_CAP:
			return LINE_CAP_ROUND;
		case B_SQUARE_CAP:
			return LINE_CAP_SQUARE;
		default:
			return LINE_CAP_BUTT;
	}
}


uint32
AGGRender::ConvertFromAGGLineCap(line_cap_e aggLineCap)
{
	switch (aggLineCap) {
		case LINE_CAP_BUTT:
			return B_BUTT_CAP;
		case LINE_CAP_ROUND:
			return B_ROUND_CAP;
		case LINE_CAP_SQUARE:
			return B_SQUARE_CAP;
		default:
			return B_BUTT_CAP;
	}
}


// Line Join Operations
AGGRender::line_join_e
AGGRender::ConvertToAGGLineJoin(uint32 haikuLineJoin)
{
	switch (haikuLineJoin) {
		case B_MITER_JOIN:
			return LINE_JOIN_MITER;
		case B_ROUND_JOIN:
			return LINE_JOIN_ROUND;
		case B_BEVEL_JOIN:
			return LINE_JOIN_BEVEL;
		default:
			return LINE_JOIN_MITER;
	}
}


uint32
AGGRender::ConvertFromAGGLineJoin(line_join_e aggLineJoin)
{
	switch (aggLineJoin) {
		case LINE_JOIN_MITER:
			return B_MITER_JOIN;
		case LINE_JOIN_ROUND:
			return B_ROUND_JOIN;
		case LINE_JOIN_BEVEL:
			return B_BEVEL_JOIN;
		default:
			return B_MITER_JOIN;
	}
}


// Inner Join Operations  
AGGRender::inner_join_e
AGGRender::ConvertToAGGInnerJoin(uint32 haikuInnerJoin)
{
	switch (haikuInnerJoin) {
		case 0: // inner_bevel
			return INNER_JOIN_BEVEL;
		case 1: // inner_miter
			return INNER_JOIN_MITER;
		case 2: // inner_jag
			return INNER_JOIN_JAG;
		case 3: // inner_round
			return INNER_JOIN_ROUND;
		default:
			return INNER_JOIN_BEVEL;
	}
}


// Stroke Style Management
AGGRender::StrokeStyleHandle*
AGGRender::CreateStrokeStyle()
{
	StrokeStyleHandle* handle = new(std::nothrow) StrokeStyleHandle;
	if (handle == NULL)
		return NULL;

	agg::conv_stroke<agg::path_storage>* stroke = 
		new(std::nothrow) agg::conv_stroke<agg::path_storage>(*(agg::path_storage*)NULL);
	if (stroke == NULL) {
		delete handle;
		return NULL;
	}

	handle->stroke = stroke;
	handle->width = 1.0;
	handle->lineCap = LINE_CAP_BUTT;
	handle->lineJoin = LINE_JOIN_MITER;
	handle->miterLimit = 4.0;
	
	return handle;
}


void
AGGRender::DeleteStrokeStyle(StrokeStyleHandle* handle)
{
	if (handle == NULL)
		return;

	delete handle->stroke;
	delete handle;
}


status_t
AGGRender::SetStrokeWidth(StrokeStyleHandle* handle, double width)
{
	if (handle == NULL || handle->stroke == NULL || width < 0)
		return B_BAD_VALUE;

	static_cast<agg::conv_stroke<agg::path_storage>*>(handle->stroke)->width(width);
	handle->width = width;
	
	return B_OK;
}


status_t
AGGRender::SetStrokeLineCap(StrokeStyleHandle* handle, line_cap_e cap)
{
	if (handle == NULL || handle->stroke == NULL)
		return B_BAD_VALUE;

	agg::line_cap_e aggCap;
	switch (cap) {
		case LINE_CAP_BUTT:
			aggCap = agg::butt_cap;
			break;
		case LINE_CAP_ROUND:
			aggCap = agg::round_cap;
			break;
		case LINE_CAP_SQUARE:
			aggCap = agg::square_cap;
			break;
		default:
			return B_BAD_VALUE;
	}

	static_cast<agg::conv_stroke<agg::path_storage>*>(handle->stroke)->line_cap(aggCap);
	handle->lineCap = cap;
	
	return B_OK;
}


status_t
AGGRender::SetStrokeLineJoin(StrokeStyleHandle* handle, line_join_e join)
{
	if (handle == NULL || handle->stroke == NULL)
		return B_BAD_VALUE;

	agg::line_join_e aggJoin;
	switch (join) {
		case LINE_JOIN_MITER:
			aggJoin = agg::miter_join;
			break;
		case LINE_JOIN_ROUND:
			aggJoin = agg::round_join;
			break;
		case LINE_JOIN_BEVEL:
			aggJoin = agg::bevel_join;
			break;
		default:
			return B_BAD_VALUE;
	}

	static_cast<agg::conv_stroke<agg::path_storage>*>(handle->stroke)->line_join(aggJoin);
	handle->lineJoin = join;
	
	return B_OK;
}


status_t
AGGRender::SetStrokeMiterLimit(StrokeStyleHandle* handle, double miterLimit)
{
	if (handle == NULL || handle->stroke == NULL || miterLimit < 1.0)
		return B_BAD_VALUE;

	static_cast<agg::conv_stroke<agg::path_storage>*>(handle->stroke)->miter_limit(miterLimit);
	handle->miterLimit = miterLimit;
	
	return B_OK;
}


// Line Profile Management
AGGRender::LineProfileHandle*
AGGRender::CreateLineProfileAA()
{
	LineProfileHandle* handle = new(std::nothrow) LineProfileHandle;
	if (handle == NULL)
		return NULL;

	agg::line_profile_aa* profile = new(std::nothrow) agg::line_profile_aa();
	if (profile == NULL) {
		delete handle;
		return NULL;
	}

	handle->profile = profile;
	
	return handle;
}


void
AGGRender::DeleteLineProfile(LineProfileHandle* handle)
{
	if (handle == NULL)
		return;

	delete handle->profile;
	delete handle;
}


status_t
AGGRender::SetLineProfileWidth(LineProfileHandle* handle, double width)
{
	if (handle == NULL || handle->profile == NULL || width < 0)
		return B_BAD_VALUE;

	static_cast<agg::line_profile_aa*>(handle->profile)->width(width);
	
	return B_OK;
}


// Filling Rule Operations
AGGRender::filling_rule_e
AGGRender::ConvertToAGGFillingRule(uint32 haikuFillingRule)
{
	switch (haikuFillingRule) {
		case B_NONZERO:
			return FILLING_RULE_NON_ZERO;
		case B_EVEN_ODD:
			return FILLING_RULE_EVEN_ODD;
		default:
			return FILLING_RULE_NON_ZERO;
	}
}


uint32
AGGRender::ConvertFromAGGFillingRule(filling_rule_e aggFillingRule)
{
	switch (aggFillingRule) {
		case FILLING_RULE_NON_ZERO:
			return B_NONZERO;
		case FILLING_RULE_EVEN_ODD:
			return B_EVEN_ODD;
		default:
			return B_NONZERO;
	}
}


// Alpha Mask Management
AGGRender::AlphaMaskHandle*
AGGRender::CreateAlphaMaskGray8(uint8* buffer, int width, int height, int stride)
{
	if (buffer == NULL || width <= 0 || height <= 0 || stride <= 0)
		return NULL;

	AlphaMaskHandle* handle = new(std::nothrow) AlphaMaskHandle;
	if (handle == NULL)
		return NULL;

	agg::alpha_mask_gray8* mask = new(std::nothrow) agg::alpha_mask_gray8(
		agg::rendering_buffer(buffer, width, height, stride));
	if (mask == NULL) {
		delete handle;
		return NULL;
	}

	handle->mask = mask;
	handle->maskType = ALPHA_MASK_GRAY8;
	
	return handle;
}


AGGRender::ClippedAlphaMaskHandle*
AGGRender::CreateClippedAlphaMask(const AlphaMaskHandle* baseMask, int x1, int y1, int x2, int y2)
{
	if (baseMask == NULL || baseMask->mask == NULL)
		return NULL;

	ClippedAlphaMaskHandle* handle = new(std::nothrow) ClippedAlphaMaskHandle;
	if (handle == NULL)
		return NULL;

	agg::clipped_alpha_mask<agg::alpha_mask_gray8>* clippedMask = 
		new(std::nothrow) agg::clipped_alpha_mask<agg::alpha_mask_gray8>(
			*static_cast<agg::alpha_mask_gray8*>(baseMask->mask), x1, y1, x2, y2);
	if (clippedMask == NULL) {
		delete handle;
		return NULL;
	}

	handle->clippedMask = clippedMask;
	
	return handle;
}


void
AGGRender::DeleteAlphaMask(AlphaMaskHandle* handle)
{
	if (handle == NULL)
		return;

	delete handle->mask;
	delete handle;
}


void
AGGRender::DeleteClippedAlphaMask(ClippedAlphaMaskHandle* handle)
{
	if (handle == NULL)
		return;

	delete handle->clippedMask;
	delete handle;
}


// Pixel Order Operations
AGGRender::pixel_order_e
AGGRender::ConvertToAGGPixelOrder(uint32 haikuPixelOrder)
{
	switch (haikuPixelOrder) {
		case B_RGBA32:
			return PIXEL_ORDER_RGBA;
		case B_BGRA32:
			return PIXEL_ORDER_BGRA;
		default:
			return PIXEL_ORDER_BGRA;
	}
}


uint32
AGGRender::ConvertFromAGGPixelOrder(pixel_order_e aggPixelOrder)
{
	switch (aggPixelOrder) {
		case PIXEL_ORDER_RGBA:
			return B_RGBA32;
		case PIXEL_ORDER_BGRA:
			return B_BGRA32;
		default:
			return B_BGRA32;
	}
}


// Advanced Stroke Operations
status_t
AGGRender::ApplyStrokeToPath(RenderPath* path, const StrokeStyleHandle* style)
{
	if (path == NULL || style == NULL || style->stroke == NULL)
		return B_BAD_VALUE;

	// Apply stroke style to path
	// This would involve integrating with the path storage system
	// Implementation depends on the specific RenderPath structure
	
	return B_OK;
}


status_t
AGGRender::CreateStrokedPath(const RenderPath* sourcePath, RenderPath** resultPath, 
							  const StrokeStyleHandle* style)
{
	if (sourcePath == NULL || resultPath == NULL || style == NULL)
		return B_BAD_VALUE;

	// Create a new path with stroke applied
	// This would involve creating a new RenderPath with stroke geometry
	// Implementation depends on the specific RenderPath structure
	
	return B_OK;
}


// Stroke Measurement
double
AGGRender::CalculateStrokeLength(const RenderPath* path, const StrokeStyleHandle* style)
{
	if (path == NULL || style == NULL)
		return 0.0;

	// Calculate the total length of stroked path
	// Implementation would measure the path with stroke applied
	
	return 0.0;
}


BRect
AGGRender::CalculateStrokeBounds(const RenderPath* path, const StrokeStyleHandle* style)
{
	if (path == NULL || style == NULL)
		return BRect();

	// Calculate bounding rectangle of stroked path
	// Implementation would compute bounds with stroke width considered
	
	return BRect();
}


// Dash Pattern Support
status_t
AGGRender::SetStrokeDashPattern(StrokeStyleHandle* handle, const float* dashArray, 
								int32 dashCount, float dashOffset)
{
	if (handle == NULL || handle->stroke == NULL || dashArray == NULL || dashCount <= 0)
		return B_BAD_VALUE;

	// Apply dash pattern to stroke
	// AGG supports dash patterns through conv_dash converter
	
	return B_OK;
}