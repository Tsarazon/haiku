/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender_ImageBitmap.cpp - Image and bitmap processing operations using AGG
 */

#include "AGGRender.h"

#include <agg_basics.h>
#include <agg_pixfmt_bgra32.h>
#include <agg_pixfmt_rgba32.h>
#include <agg_rendering_buffer.h>
#include <agg_image_accessors.h>
#include <agg_span_image_filter_rgba.h>
#include <agg_span_interpolator_linear.h>
#include <agg_span_allocator.h>
#include <agg_image_filters.h>
#include <agg_blur.h>

#include <SupportDefs.h>
#include <string.h>


// Image Accessor Management
AGGRender::ImageAccessorHandle*
AGGRender::CreateImageAccessorClone(uint8* buffer, int width, int height, int stride)
{
	if (buffer == NULL || width <= 0 || height <= 0 || stride <= 0)
		return NULL;

	ImageAccessorHandle* handle = new(std::nothrow) ImageAccessorHandle;
	if (handle == NULL)
		return NULL;

	agg::rendering_buffer* renderBuf = new(std::nothrow) agg::rendering_buffer(buffer, width, height, stride);
	if (renderBuf == NULL) {
		delete handle;
		return NULL;
	}

	typedef agg::pixfmt_bgra32 pixfmt_type;
	pixfmt_type* pixf = new(std::nothrow) pixfmt_type(*renderBuf);
	if (pixf == NULL) {
		delete renderBuf;
		delete handle;
		return NULL;
	}

	agg::image_accessor_clone<pixfmt_type>* accessor = 
		new(std::nothrow) agg::image_accessor_clone<pixfmt_type>(*pixf);
	if (accessor == NULL) {
		delete pixf;
		delete renderBuf;
		delete handle;
		return NULL;
	}

	handle->accessor = accessor;
	handle->pixfmt = pixf;
	handle->renderBuffer = renderBuf;
	
	return handle;
}


AGGRender::ImageAccessorHandle*
AGGRender::CreateImageAccessorWrap(uint8* buffer, int width, int height, int stride, wrap_mode_e wrapMode)
{
	if (buffer == NULL || width <= 0 || height <= 0 || stride <= 0)
		return NULL;

	ImageAccessorHandle* handle = new(std::nothrow) ImageAccessorHandle;
	if (handle == NULL)
		return NULL;

	agg::rendering_buffer* renderBuf = new(std::nothrow) agg::rendering_buffer(buffer, width, height, stride);
	if (renderBuf == NULL) {
		delete handle;
		return NULL;
	}

	typedef agg::pixfmt_bgra32 pixfmt_type;
	pixfmt_type* pixf = new(std::nothrow) pixfmt_type(*renderBuf);
	if (pixf == NULL) {
		delete renderBuf;
		delete handle;
		return NULL;
	}

	// Create wrapped accessor based on wrap mode
	switch (wrapMode) {
		case WRAP_MODE_REPEAT: {
			agg::image_accessor_wrap<pixfmt_type, agg::wrap_mode_repeat>* accessor = 
				new(std::nothrow) agg::image_accessor_wrap<pixfmt_type, agg::wrap_mode_repeat>(*pixf);
			handle->accessor = accessor;
			break;
		}
		case WRAP_MODE_REFLECT: {
			agg::image_accessor_wrap<pixfmt_type, agg::wrap_mode_reflect>* accessor = 
				new(std::nothrow) agg::image_accessor_wrap<pixfmt_type, agg::wrap_mode_reflect>(*pixf);
			handle->accessor = accessor;
			break;
		}
		case WRAP_MODE_REFLECT_AUTOSCALE: {
			agg::image_accessor_wrap<pixfmt_type, agg::wrap_mode_reflect_autoscale>* accessor = 
				new(std::nothrow) agg::image_accessor_wrap<pixfmt_type, agg::wrap_mode_reflect_autoscale>(*pixf);
			handle->accessor = accessor;
			break;
		}
		default:
			delete pixf;
			delete renderBuf;
			delete handle;
			return NULL;
	}

	handle->pixfmt = pixf;
	handle->renderBuffer = renderBuf;
	
	return handle;
}


void
AGGRender::DeleteImageAccessor(ImageAccessorHandle* handle)
{
	if (handle == NULL)
		return;

	delete handle->accessor;
	delete handle->pixfmt;
	delete handle->renderBuffer;
	delete handle;
}


// Span Image Filter Management
AGGRender::SpanImageFilterHandle*
AGGRender::CreateSpanImageFilterRGBA32Bilinear(const ImageAccessorHandle* accessor)
{
	if (accessor == NULL)
		return NULL;

	SpanImageFilterHandle* handle = new(std::nothrow) SpanImageFilterHandle;
	if (handle == NULL)
		return NULL;

	// Create linear interpolator
	agg::span_interpolator_linear<>* interpolator = 
		new(std::nothrow) agg::span_interpolator_linear<>();
	if (interpolator == NULL) {
		delete handle;
		return NULL;
	}

	// Create bilinear filter
	agg::span_image_filter_rgba32_bilinear<agg::image_accessor_clone<agg::pixfmt_bgra32>, agg::span_interpolator_linear<>>* filter = 
		new(std::nothrow) agg::span_image_filter_rgba32_bilinear<agg::image_accessor_clone<agg::pixfmt_bgra32>, agg::span_interpolator_linear<>>(
			*static_cast<agg::image_accessor_clone<agg::pixfmt_bgra32>*>(accessor->accessor), *interpolator);

	if (filter == NULL) {
		delete interpolator;
		delete handle;
		return NULL;
	}

	handle->filter = filter;
	handle->interpolator = interpolator;
	
	return handle;
}


AGGRender::SpanImageFilterHandle*
AGGRender::CreateSpanImageFilterRGBA32NearestNeighbor(const ImageAccessorHandle* accessor)
{
	if (accessor == NULL)
		return NULL;

	SpanImageFilterHandle* handle = new(std::nothrow) SpanImageFilterHandle;
	if (handle == NULL)
		return NULL;

	// Create linear interpolator
	agg::span_interpolator_linear<>* interpolator = 
		new(std::nothrow) agg::span_interpolator_linear<>();
	if (interpolator == NULL) {
		delete handle;
		return NULL;
	}

	// Create nearest neighbor filter
	agg::span_image_filter_rgba32_nn<agg::image_accessor_clone<agg::pixfmt_bgra32>, agg::span_interpolator_linear<>>* filter = 
		new(std::nothrow) agg::span_image_filter_rgba32_nn<agg::image_accessor_clone<agg::pixfmt_bgra32>, agg::span_interpolator_linear<>>(
			*static_cast<agg::image_accessor_clone<agg::pixfmt_bgra32>*>(accessor->accessor), *interpolator);

	if (filter == NULL) {
		delete interpolator;
		delete handle;
		return NULL;
	}

	handle->filter = filter;
	handle->interpolator = interpolator;
	
	return handle;
}


void
AGGRender::DeleteSpanImageFilter(SpanImageFilterHandle* handle)
{
	if (handle == NULL)
		return;

	delete handle->filter;
	delete handle->interpolator;
	delete handle;
}


// Span Interpolator Management
AGGRender::SpanInterpolatorHandle*
AGGRender::CreateSpanInterpolatorLinear()
{
	SpanInterpolatorHandle* handle = new(std::nothrow) SpanInterpolatorHandle;
	if (handle == NULL)
		return NULL;

	agg::span_interpolator_linear<>* interpolator = 
		new(std::nothrow) agg::span_interpolator_linear<>();
	if (interpolator == NULL) {
		delete handle;
		return NULL;
	}

	handle->interpolator = interpolator;
	
	return handle;
}


void
AGGRender::SetSpanInterpolatorTransform(SpanInterpolatorHandle* handle, const AffineTransformHandle* transform)
{
	if (handle == NULL || transform == NULL || handle->interpolator == NULL || transform->transform == NULL)
		return;

	static_cast<agg::span_interpolator_linear<>*>(handle->interpolator)->transformer(*transform->transform);
}


void
AGGRender::DeleteSpanInterpolator(SpanInterpolatorHandle* handle)
{
	if (handle == NULL)
		return;

	delete handle->interpolator;
	delete handle;
}


// Span Allocator Management
AGGRender::SpanAllocatorHandle*
AGGRender::CreateSpanAllocator()
{
	SpanAllocatorHandle* handle = new(std::nothrow) SpanAllocatorHandle;
	if (handle == NULL)
		return NULL;

	agg::span_allocator<agg::rgba8>* allocator = 
		new(std::nothrow) agg::span_allocator<agg::rgba8>();
	if (allocator == NULL) {
		delete handle;
		return NULL;
	}

	handle->allocator = allocator;
	
	return handle;
}


void
AGGRender::DeleteSpanAllocator(SpanAllocatorHandle* handle)
{
	if (handle == NULL)
		return;

	delete handle->allocator;
	delete handle;
}


// Pixel Format Management
AGGRender::PixelFormatHandle*
AGGRender::CreatePixelFormatBGRA32(uint8* buffer, int width, int height, int stride)
{
	if (buffer == NULL || width <= 0 || height <= 0 || stride <= 0)
		return NULL;

	PixelFormatHandle* handle = new(std::nothrow) PixelFormatHandle;
	if (handle == NULL)
		return NULL;

	agg::rendering_buffer* renderBuf = new(std::nothrow) agg::rendering_buffer(buffer, width, height, stride);
	if (renderBuf == NULL) {
		delete handle;
		return NULL;
	}

	agg::pixfmt_bgra32* pixfmt = new(std::nothrow) agg::pixfmt_bgra32(*renderBuf);
	if (pixfmt == NULL) {
		delete renderBuf;
		delete handle;
		return NULL;
	}

	handle->pixfmt = pixfmt;
	handle->renderBuffer = renderBuf;
	handle->format = PIXFMT_BGRA32;
	
	return handle;
}


AGGRender::PixelFormatHandle*
AGGRender::CreatePixelFormatBGRA32Premultiplied(uint8* buffer, int width, int height, int stride)
{
	if (buffer == NULL || width <= 0 || height <= 0 || stride <= 0)
		return NULL;

	PixelFormatHandle* handle = new(std::nothrow) PixelFormatHandle;
	if (handle == NULL)
		return NULL;

	agg::rendering_buffer* renderBuf = new(std::nothrow) agg::rendering_buffer(buffer, width, height, stride);
	if (renderBuf == NULL) {
		delete handle;
		return NULL;
	}

	agg::pixfmt_bgra32_pre* pixfmt = new(std::nothrow) agg::pixfmt_bgra32_pre(*renderBuf);
	if (pixfmt == NULL) {
		delete renderBuf;
		delete handle;
		return NULL;
	}

	handle->pixfmt = pixfmt;
	handle->renderBuffer = renderBuf;
	handle->format = PIXFMT_BGRA32_PRE;
	
	return handle;
}


AGGRender::PixelFormatHandle*
AGGRender::CreatePixelFormatRGBA32(uint8* buffer, int width, int height, int stride)
{
	if (buffer == NULL || width <= 0 || height <= 0 || stride <= 0)
		return NULL;

	PixelFormatHandle* handle = new(std::nothrow) PixelFormatHandle;
	if (handle == NULL)
		return NULL;

	agg::rendering_buffer* renderBuf = new(std::nothrow) agg::rendering_buffer(buffer, width, height, stride);
	if (renderBuf == NULL) {
		delete handle;
		return NULL;
	}

	agg::pixfmt_rgba32* pixfmt = new(std::nothrow) agg::pixfmt_rgba32(*renderBuf);
	if (pixfmt == NULL) {
		delete renderBuf;
		delete handle;
		return NULL;
	}

	handle->pixfmt = pixfmt;
	handle->renderBuffer = renderBuf;
	handle->format = PIXFMT_RGBA32;
	
	return handle;
}


void
AGGRender::DeletePixelFormat(PixelFormatHandle* handle)
{
	if (handle == NULL)
		return;

	delete handle->pixfmt;
	delete handle->renderBuffer;
	delete handle;
}


// Blur Operations
status_t
AGGRender::ApplyRecursiveBlur(uint8* buffer, int width, int height, int stride, double radius)
{
	if (buffer == NULL || width <= 0 || height <= 0 || stride <= 0 || radius < 0)
		return B_BAD_VALUE;

	agg::rendering_buffer renderBuf(buffer, width, height, stride);
	agg::pixfmt_bgra32 pixf(renderBuf);

	agg::recursive_blur<agg::pixfmt_bgra32, agg::recursive_blur_calc_rgba<>> blur;
	blur.blur(pixf, radius);

	return B_OK;
}


status_t
AGGRender::ApplyRecursiveBlurRGBA(uint8* buffer, int width, int height, int stride, double radius)
{
	if (buffer == NULL || width <= 0 || height <= 0 || stride <= 0 || radius < 0)
		return B_BAD_VALUE;

	agg::rendering_buffer renderBuf(buffer, width, height, stride);
	agg::pixfmt_rgba32 pixf(renderBuf);

	agg::recursive_blur<agg::pixfmt_rgba32, agg::recursive_blur_calc_rgba<>> blur;
	blur.blur(pixf, radius);

	return B_OK;
}


// Image Filter Operations
AGGRender::ImageFilterHandle*
AGGRender::CreateImageFilterBilinear()
{
	ImageFilterHandle* handle = new(std::nothrow) ImageFilterHandle;
	if (handle == NULL)
		return NULL;

	agg::image_filter_bilinear* filter = new(std::nothrow) agg::image_filter_bilinear();
	if (filter == NULL) {
		delete handle;
		return NULL;
	}

	handle->filter = filter;
	handle->filterType = IMAGE_FILTER_BILINEAR;
	
	return handle;
}


void
AGGRender::DeleteImageFilter(ImageFilterHandle* handle)
{
	if (handle == NULL)
		return;

	delete handle->filter;
	delete handle;
}


// Utility Functions
void
AGGRender::GetImageDimensions(const ImageAccessorHandle* handle, int* width, int* height)
{
	if (handle == NULL || handle->renderBuffer == NULL || width == NULL || height == NULL)
		return;

	agg::rendering_buffer* renderBuf = static_cast<agg::rendering_buffer*>(handle->renderBuffer);
	*width = renderBuf->width();
	*height = renderBuf->height();
}


uint8*
AGGRender::GetImageBuffer(const ImageAccessorHandle* handle)
{
	if (handle == NULL || handle->renderBuffer == NULL)
		return NULL;

	agg::rendering_buffer* renderBuf = static_cast<agg::rendering_buffer*>(handle->renderBuffer);
	return renderBuf->buf();
}