/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender_ColorGradient.cpp - Color and gradient operations using AGG
 */

#include "AGGRender.h"

#include <agg_basics.h>
#include <agg_color_rgba.h>
#include <agg_color_gray.h>
#include <agg_gradient_lut.h>
#include <agg_span_gradient.h>
#include <agg_span_allocator.h>
#include <agg_span_interpolator_linear.h>
#include <agg_gamma_lut.h>
#include <agg_gamma_functions.h>

#include <SupportDefs.h>
#include <math.h>


// Color utilities
agg::rgba8
AGGRender::ConvertToAGGColor(const rgb_color& color)
{
	return agg::rgba8(color.red, color.green, color.blue, color.alpha);
}


rgb_color
AGGRender::ConvertFromAGGColor(const agg::rgba8& color)
{
	rgb_color result;
	result.red = color.r;
	result.green = color.g;
	result.blue = color.b;
	result.alpha = color.a;
	return result;
}


agg::rgba
AGGRender::ConvertToAGGColorFloat(const rgb_color& color)
{
	return agg::rgba(color.red / 255.0, color.green / 255.0, 
					 color.blue / 255.0, color.alpha / 255.0);
}


rgb_color
AGGRender::ConvertFromAGGColorFloat(const agg::rgba& color)
{
	rgb_color result;
	result.red = (uint8)(color.r * 255.0);
	result.green = (uint8)(color.g * 255.0);
	result.blue = (uint8)(color.b * 255.0);
	result.alpha = (uint8)(color.a * 255.0);
	return result;
}


// Color interpolation
AGGRender::ColorInterpolatorHandle*
AGGRender::CreateColorInterpolator(const rgb_color& color1, const rgb_color& color2, int32 steps)
{
	if (steps <= 0)
		return NULL;

	ColorInterpolatorHandle* handle = new(std::nothrow) ColorInterpolatorHandle;
	if (handle == NULL)
		return NULL;

	typedef agg::color_interpolator<agg::rgba8> interpolator_type;
	interpolator_type* interpolator = new(std::nothrow) interpolator_type(
		ConvertToAGGColor(color1), ConvertToAGGColor(color2), steps);

	if (interpolator == NULL) {
		delete handle;
		return NULL;
	}

	handle->interpolator = interpolator;
	handle->steps = steps;
	handle->currentStep = 0;

	return handle;
}


status_t
AGGRender::DeleteColorInterpolator(ColorInterpolatorHandle* interpolator)
{
	if (interpolator == NULL)
		return B_BAD_VALUE;

	if (interpolator->interpolator != NULL) {
		typedef agg::color_interpolator<agg::rgba8> interpolator_type;
		delete static_cast<interpolator_type*>(interpolator->interpolator);
	}

	delete interpolator;
	return B_OK;
}


rgb_color
AGGRender::GetInterpolatedColor(ColorInterpolatorHandle* interpolator, float position)
{
	if (interpolator == NULL || interpolator->interpolator == NULL)
		return make_color(0, 0, 0, 0);

	typedef agg::color_interpolator<agg::rgba8> interpolator_type;
	interpolator_type* interp = static_cast<interpolator_type*>(interpolator->interpolator);

	// Clamp position to [0, 1]
	position = max_c(0.0f, min_c(1.0f, position));
	int32 step = (int32)(position * (interpolator->steps - 1));

	// Reset interpolator and advance to desired step
	interp->reset();
	for (int32 i = 0; i < step; i++) {
		++(*interp);
	}

	return ConvertFromAGGColor(interp->color());
}


// Gradient creation and management
AGGRender::GradientHandle*
AGGRender::CreateGradient(gradient_type_e type)
{
	GradientHandle* handle = new(std::nothrow) GradientHandle;
	if (handle == NULL)
		return NULL;

	handle->type = type;
	handle->gradient = NULL;

	switch (type) {
		case GRADIENT_LINEAR_X:
		{
			typedef agg::gradient_x gradient_type;
			handle->gradient = new(std::nothrow) gradient_type;
			break;
		}
		case GRADIENT_RADIAL:
		{
			typedef agg::gradient_radial gradient_type;
			handle->gradient = new(std::nothrow) gradient_type;
			break;
		}
		case GRADIENT_RADIAL_FOCUS:
		{
			typedef agg::gradient_radial_focus gradient_type;
			handle->gradient = new(std::nothrow) gradient_type;
			break;
		}
		case GRADIENT_DIAMOND:
		{
			typedef agg::gradient_diamond gradient_type;
			handle->gradient = new(std::nothrow) gradient_type;
			break;
		}
		case GRADIENT_CONIC:
		{
			typedef agg::gradient_conic gradient_type;
			handle->gradient = new(std::nothrow) gradient_type;
			break;
		}
		default:
			delete handle;
			return NULL;
	}

	if (handle->gradient == NULL) {
		delete handle;
		return NULL;
	}

	return handle;
}


status_t
AGGRender::DeleteGradient(GradientHandle* gradient)
{
	if (gradient == NULL)
		return B_BAD_VALUE;

	if (gradient->gradient != NULL) {
		switch (gradient->type) {
			case GRADIENT_LINEAR_X:
			{
				typedef agg::gradient_x gradient_type;
				delete static_cast<gradient_type*>(gradient->gradient);
				break;
			}
			case GRADIENT_RADIAL:
			{
				typedef agg::gradient_radial gradient_type;
				delete static_cast<gradient_type*>(gradient->gradient);
				break;
			}
			case GRADIENT_RADIAL_FOCUS:
			{
				typedef agg::gradient_radial_focus gradient_type;
				delete static_cast<gradient_type*>(gradient->gradient);
				break;
			}
			case GRADIENT_DIAMOND:
			{
				typedef agg::gradient_diamond gradient_type;
				delete static_cast<gradient_type*>(gradient->gradient);
				break;
			}
			case GRADIENT_CONIC:
			{
				typedef agg::gradient_conic gradient_type;
				delete static_cast<gradient_type*>(gradient->gradient);
				break;
			}
			default:
				break;
		}
	}

	delete gradient;
	return B_OK;
}


// Gradient LUT (Lookup Table)
AGGRender::GradientLUTHandle*
AGGRender::CreateGradientLUT(int32 size)
{
	if (size <= 0 || size > 65536)
		return NULL;

	GradientLUTHandle* handle = new(std::nothrow) GradientLUTHandle;
	if (handle == NULL)
		return NULL;

	typedef agg::gradient_lut<agg::color_interpolator<agg::rgba8>, 1024> lut_type;
	lut_type* lut = new(std::nothrow) lut_type;

	if (lut == NULL) {
		delete handle;
		return NULL;
	}

	handle->lut = lut;
	handle->size = size;

	return handle;
}


status_t
AGGRender::DeleteGradientLUT(GradientLUTHandle* lut)
{
	if (lut == NULL)
		return B_BAD_VALUE;

	if (lut->lut != NULL) {
		typedef agg::gradient_lut<agg::color_interpolator<agg::rgba8>, 1024> lut_type;
		delete static_cast<lut_type*>(lut->lut);
	}

	delete lut;
	return B_OK;
}


status_t
AGGRender::BuildGradientLUT(GradientLUTHandle* lut, const rgb_color* colors, 
							const float* stops, int32 colorCount)
{
	if (lut == NULL || lut->lut == NULL || colors == NULL || stops == NULL || colorCount < 2)
		return B_BAD_VALUE;

	typedef agg::gradient_lut<agg::color_interpolator<agg::rgba8>, 1024> lut_type;
	lut_type* gradientLUT = static_cast<lut_type*>(lut->lut);

	// Build gradient from color stops
	for (int32 i = 0; i < colorCount - 1; i++) {
		agg::rgba8 c1 = ConvertToAGGColor(colors[i]);
		agg::rgba8 c2 = ConvertToAGGColor(colors[i + 1]);
		
		float start = stops[i];
		float end = stops[i + 1];
		
		// Ensure stops are in valid range [0, 1]
		start = max_c(0.0f, min_c(1.0f, start));
		end = max_c(0.0f, min_c(1.0f, end));
		
		if (start < end) {
			gradientLUT->add_color(start, c1);
			gradientLUT->add_color(end, c2);
		}
	}

	gradientLUT->build_lut();
	return B_OK;
}


// Span gradient
AGGRender::SpanGradientHandle*
AGGRender::CreateSpanGradient(GradientHandle* gradient, GradientLUTHandle* lut,
							  const BAffineTransform& transform)
{
	if (gradient == NULL || lut == NULL)
		return NULL;

	SpanGradientHandle* handle = new(std::nothrow) SpanGradientHandle;
	if (handle == NULL)
		return NULL;

	// Convert BAffineTransform to agg::trans_affine
	agg::trans_affine aggTransform(
		transform.sx, transform.shy, transform.shx,
		transform.sy, transform.tx, transform.ty);

	typedef agg::span_interpolator_linear<> interpolator_type;
	interpolator_type* interpolator = new(std::nothrow) interpolator_type(aggTransform);

	if (interpolator == NULL) {
		delete handle;
		return NULL;
	}

	handle->gradient = gradient;
	handle->lut = lut;
	handle->interpolator = interpolator;
	handle->spanGradient = NULL; // Will be created when needed based on gradient type

	return handle;
}


status_t
AGGRender::DeleteSpanGradient(SpanGradientHandle* spanGradient)
{
	if (spanGradient == NULL)
		return B_BAD_VALUE;

	if (spanGradient->interpolator != NULL) {
		typedef agg::span_interpolator_linear<> interpolator_type;
		delete static_cast<interpolator_type*>(spanGradient->interpolator);
	}

	// Note: Don't delete gradient and lut as they are managed separately
	// spanGradient object itself would need to be deleted based on its type

	delete spanGradient;
	return B_OK;
}


// Gamma correction
AGGRender::GammaLUTHandle*
AGGRender::CreateGammaLUT(gamma_type_e type, float gamma)
{
	GammaLUTHandle* handle = new(std::nothrow) GammaLUTHandle;
	if (handle == NULL)
		return NULL;

	handle->type = type;
	handle->gamma = gamma;
	handle->lut = NULL;

	switch (type) {
		case GAMMA_POWER:
		{
			typedef agg::gamma_lut<agg::int8u, agg::int8u, 8, 8> lut_type;
			lut_type* lut = new(std::nothrow) lut_type(gamma);
			handle->lut = lut;
			break;
		}
		case GAMMA_NONE:
		{
			typedef agg::gamma_none lut_type;
			lut_type* lut = new(std::nothrow) lut_type;
			handle->lut = lut;
			break;
		}
		case GAMMA_THRESHOLD:
		{
			typedef agg::gamma_threshold lut_type;
			lut_type* lut = new(std::nothrow) lut_type(gamma);
			handle->lut = lut;
			break;
		}
		default:
			delete handle;
			return NULL;
	}

	if (handle->lut == NULL) {
		delete handle;
		return NULL;
	}

	return handle;
}


status_t
AGGRender::DeleteGammaLUT(GammaLUTHandle* lut)
{
	if (lut == NULL)
		return B_BAD_VALUE;

	if (lut->lut != NULL) {
		switch (lut->type) {
			case GAMMA_POWER:
			{
				typedef agg::gamma_lut<agg::int8u, agg::int8u, 8, 8> lut_type;
				delete static_cast<lut_type*>(lut->lut);
				break;
			}
			case GAMMA_NONE:
			{
				typedef agg::gamma_none lut_type;
				delete static_cast<lut_type*>(lut->lut);
				break;
			}
			case GAMMA_THRESHOLD:
			{
				typedef agg::gamma_threshold lut_type;
				delete static_cast<lut_type*>(lut->lut);
				break;
			}
			default:
				break;
		}
	}

	delete lut;
	return B_OK;
}


uint8
AGGRender::ApplyGammaCorrection(GammaLUTHandle* lut, uint8 value)
{
	if (lut == NULL || lut->lut == NULL)
		return value;

	switch (lut->type) {
		case GAMMA_POWER:
		{
			typedef agg::gamma_lut<agg::int8u, agg::int8u, 8, 8> lut_type;
			lut_type* gammaLUT = static_cast<lut_type*>(lut->lut);
			return (*gammaLUT)(value);
		}
		case GAMMA_NONE:
		{
			return value;
		}
		case GAMMA_THRESHOLD:
		{
			typedef agg::gamma_threshold lut_type;
			lut_type* gammaLUT = static_cast<lut_type*>(lut->lut);
			return (*gammaLUT)(value);
		}
		default:
			return value;
	}
}


// Color manipulation utilities
rgb_color
AGGRender::BlendColors(const rgb_color& color1, const rgb_color& color2, float ratio)
{
	ratio = max_c(0.0f, min_c(1.0f, ratio));
	
	agg::rgba c1 = ConvertToAGGColorFloat(color1);
	agg::rgba c2 = ConvertToAGGColorFloat(color2);
	
	agg::rgba result(
		c1.r * (1.0 - ratio) + c2.r * ratio,
		c1.g * (1.0 - ratio) + c2.g * ratio,
		c1.b * (1.0 - ratio) + c2.b * ratio,
		c1.a * (1.0 - ratio) + c2.a * ratio
	);
	
	return ConvertFromAGGColorFloat(result);
}


rgb_color
AGGRender::AdjustColorBrightness(const rgb_color& color, float brightness)
{
	brightness = max_c(0.0f, brightness);
	
	agg::rgba c = ConvertToAGGColorFloat(color);
	c.r *= brightness;
	c.g *= brightness;
	c.b *= brightness;
	
	// Clamp to valid range
	c.r = min_c(1.0, c.r);
	c.g = min_c(1.0, c.g);
	c.b = min_c(1.0, c.b);
	
	return ConvertFromAGGColorFloat(c);
}


rgb_color
AGGRender::AdjustColorSaturation(const rgb_color& color, float saturation)
{
	agg::rgba c = ConvertToAGGColorFloat(color);
	
	// Convert to HSV, adjust saturation, convert back
	// Simplified implementation - full HSV conversion would be more accurate
	float gray = c.r * 0.299 + c.g * 0.587 + c.b * 0.114;
	
	c.r = gray + saturation * (c.r - gray);
	c.g = gray + saturation * (c.g - gray);
	c.b = gray + saturation * (c.b - gray);
	
	// Clamp to valid range
	c.r = max_c(0.0, min_c(1.0, c.r));
	c.g = max_c(0.0, min_c(1.0, c.g));
	c.b = max_c(0.0, min_c(1.0, c.b));
	
	return ConvertFromAGGColorFloat(c);
}


float
AGGRender::CalculateColorDistance(const rgb_color& color1, const rgb_color& color2)
{
	float dr = (color1.red - color2.red) / 255.0f;
	float dg = (color1.green - color2.green) / 255.0f;
	float db = (color1.blue - color2.blue) / 255.0f;
	float da = (color1.alpha - color2.alpha) / 255.0f;
	
	return sqrt(dr * dr + dg * dg + db * db + da * da);
}