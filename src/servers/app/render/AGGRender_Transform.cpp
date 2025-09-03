/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AGGRender_Transform.cpp - Transforms and math operations using AGG
 */

#include "AGGRender.h"

#include <agg_basics.h>
#include <agg_trans_affine.h>
#include <agg_trans_bilinear.h>
#include <agg_trans_perspective.h>
#include <agg_bounding_rect.h>
#include <agg_math.h>

#include <SupportDefs.h>
#include <math.h>


// Affine transform creation and management
AGGRender::AffineTransformHandle*
AGGRender::CreateAffineTransform()
{
	AffineTransformHandle* handle = new(std::nothrow) AffineTransformHandle;
	if (handle == NULL)
		return NULL;

	agg::trans_affine* transform = new(std::nothrow) agg::trans_affine;
	if (transform == NULL) {
		delete handle;
		return NULL;
	}

	handle->transform = transform;
	return handle;
}


AGGRender::AffineTransformHandle*
AGGRender::CreateAffineTransformFromMatrix(double sx, double shy, double shx, 
										   double sy, double tx, double ty)
{
	AffineTransformHandle* handle = new(std::nothrow) AffineTransformHandle;
	if (handle == NULL)
		return NULL;

	agg::trans_affine* transform = new(std::nothrow) agg::trans_affine(sx, shy, shx, sy, tx, ty);
	if (transform == NULL) {
		delete handle;
		return NULL;
	}

	handle->transform = transform;
	return handle;
}


status_t
AGGRender::DeleteAffineTransform(AffineTransformHandle* transform)
{
	if (transform == NULL)
		return B_BAD_VALUE;

	if (transform->transform != NULL) {
		delete static_cast<agg::trans_affine*>(transform->transform);
	}

	delete transform;
	return B_OK;
}


// Affine transform operations
status_t
AGGRender::ResetAffineTransform(AffineTransformHandle* transform)
{
	if (transform == NULL || transform->transform == NULL)
		return B_BAD_VALUE;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	t->reset();
	return B_OK;
}


status_t
AGGRender::TranslateAffine(AffineTransformHandle* transform, double dx, double dy)
{
	if (transform == NULL || transform->transform == NULL)
		return B_BAD_VALUE;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	t->translate(dx, dy);
	return B_OK;
}


status_t
AGGRender::ScaleAffine(AffineTransformHandle* transform, double sx, double sy)
{
	if (transform == NULL || transform->transform == NULL)
		return B_BAD_VALUE;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	t->scale(sx, sy);
	return B_OK;
}


status_t
AGGRender::RotateAffine(AffineTransformHandle* transform, double angle)
{
	if (transform == NULL || transform->transform == NULL)
		return B_BAD_VALUE;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	t->rotate(angle);
	return B_OK;
}


status_t
AGGRender::SkewAffine(AffineTransformHandle* transform, double sx, double sy)
{
	if (transform == NULL || transform->transform == NULL)
		return B_BAD_VALUE;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	t->skew(sx, sy);
	return B_OK;
}


status_t
AGGRender::MultiplyAffine(AffineTransformHandle* transform, AffineTransformHandle* other)
{
	if (transform == NULL || other == NULL || 
		transform->transform == NULL || other->transform == NULL)
		return B_BAD_VALUE;

	agg::trans_affine* t1 = static_cast<agg::trans_affine*>(transform->transform);
	agg::trans_affine* t2 = static_cast<agg::trans_affine*>(other->transform);
	
	*t1 *= *t2;
	return B_OK;
}


status_t
AGGRender::InvertAffine(AffineTransformHandle* transform)
{
	if (transform == NULL || transform->transform == NULL)
		return B_BAD_VALUE;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	t->invert();
	return B_OK;
}


// Transform point and coordinate operations
status_t
AGGRender::TransformPoint(AffineTransformHandle* transform, double* x, double* y)
{
	if (transform == NULL || transform->transform == NULL || x == NULL || y == NULL)
		return B_BAD_VALUE;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	t->transform(x, y);
	return B_OK;
}


status_t
AGGRender::InverseTransformPoint(AffineTransformHandle* transform, double* x, double* y)
{
	if (transform == NULL || transform->transform == NULL || x == NULL || y == NULL)
		return B_BAD_VALUE;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	t->inverse_transform(x, y);
	return B_OK;
}


// Specialized transform creation
AGGRender::AffineTransformHandle*
AGGRender::CreateTranslationTransform(double dx, double dy)
{
	AffineTransformHandle* handle = CreateAffineTransform();
	if (handle == NULL)
		return NULL;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(handle->transform);
	*t = agg::trans_affine_translation(dx, dy);
	return handle;
}


AGGRender::AffineTransformHandle*
AGGRender::CreateScalingTransform(double sx, double sy)
{
	AffineTransformHandle* handle = CreateAffineTransform();
	if (handle == NULL)
		return NULL;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(handle->transform);
	*t = agg::trans_affine_scaling(sx, sy);
	return handle;
}


AGGRender::AffineTransformHandle*
AGGRender::CreateRotationTransform(double angle)
{
	AffineTransformHandle* handle = CreateAffineTransform();
	if (handle == NULL)
		return NULL;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(handle->transform);
	*t = agg::trans_affine_rotation(angle);
	return handle;
}


AGGRender::AffineTransformHandle*
AGGRender::CreateSkewingTransform(double sx, double sy)
{
	AffineTransformHandle* handle = CreateAffineTransform();
	if (handle == NULL)
		return NULL;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(handle->transform);
	*t = agg::trans_affine_skewing(sx, sy);
	return handle;
}


// Bilinear transform
AGGRender::BilinearTransformHandle*
AGGRender::CreateBilinearTransform(const BPoint* quad, const BRect& rect)
{
	if (quad == NULL)
		return NULL;

	BilinearTransformHandle* handle = new(std::nothrow) BilinearTransformHandle;
	if (handle == NULL)
		return NULL;

	double src[8];
	src[0] = rect.left;   src[1] = rect.top;
	src[2] = rect.right;  src[3] = rect.top;
	src[4] = rect.right;  src[5] = rect.bottom;
	src[6] = rect.left;   src[7] = rect.bottom;

	double dst[8];
	dst[0] = quad[0].x;   dst[1] = quad[0].y;
	dst[2] = quad[1].x;   dst[3] = quad[1].y;
	dst[4] = quad[2].x;   dst[5] = quad[2].y;
	dst[6] = quad[3].x;   dst[7] = quad[3].y;

	agg::trans_bilinear* transform = new(std::nothrow) agg::trans_bilinear(src, dst);
	if (transform == NULL) {
		delete handle;
		return NULL;
	}

	handle->transform = transform;
	return handle;
}


status_t
AGGRender::DeleteBilinearTransform(BilinearTransformHandle* transform)
{
	if (transform == NULL)
		return B_BAD_VALUE;

	if (transform->transform != NULL) {
		delete static_cast<agg::trans_bilinear*>(transform->transform);
	}

	delete transform;
	return B_OK;
}


status_t
AGGRender::TransformPointBilinear(BilinearTransformHandle* transform, double* x, double* y)
{
	if (transform == NULL || transform->transform == NULL || x == NULL || y == NULL)
		return B_BAD_VALUE;

	agg::trans_bilinear* t = static_cast<agg::trans_bilinear*>(transform->transform);
	t->transform(x, y);
	return B_OK;
}


// Perspective transform
AGGRender::PerspectiveTransformHandle*
AGGRender::CreatePerspectiveTransform(const BPoint* quad, const BRect& rect)
{
	if (quad == NULL)
		return NULL;

	PerspectiveTransformHandle* handle = new(std::nothrow) PerspectiveTransformHandle;
	if (handle == NULL)
		return NULL;

	double src[8];
	src[0] = rect.left;   src[1] = rect.top;
	src[2] = rect.right;  src[3] = rect.top;
	src[4] = rect.right;  src[5] = rect.bottom;
	src[6] = rect.left;   src[7] = rect.bottom;

	double dst[8];
	dst[0] = quad[0].x;   dst[1] = quad[0].y;
	dst[2] = quad[1].x;   dst[3] = quad[1].y;
	dst[4] = quad[2].x;   dst[5] = quad[2].y;
	dst[6] = quad[3].x;   dst[7] = quad[3].y;

	agg::trans_perspective* transform = new(std::nothrow) agg::trans_perspective(src, dst);
	if (transform == NULL) {
		delete handle;
		return NULL;
	}

	handle->transform = transform;
	return handle;
}


status_t
AGGRender::DeletePerspectiveTransform(PerspectiveTransformHandle* transform)
{
	if (transform == NULL)
		return B_BAD_VALUE;

	if (transform->transform != NULL) {
		delete static_cast<agg::trans_perspective*>(transform->transform);
	}

	delete transform;
	return B_OK;
}


status_t
AGGRender::TransformPointPerspective(PerspectiveTransformHandle* transform, double* x, double* y)
{
	if (transform == NULL || transform->transform == NULL || x == NULL || y == NULL)
		return B_BAD_VALUE;

	agg::trans_perspective* t = static_cast<agg::trans_perspective*>(transform->transform);
	t->transform(x, y);
	return B_OK;
}


// Math utilities
double
AGGRender::DegreesToRadians(double degrees)
{
	return agg::deg2rad(degrees);
}


double
AGGRender::RadiansToDegrees(double radians)
{
	return agg::rad2deg(radians);
}


// Bounding rectangle calculations
BRect
AGGRender::CalculateBoundingRect(RenderPath* path, AffineTransformHandle* transform)
{
	if (path == NULL)
		return BRect();

	AGGPath* aggPath = static_cast<AGGPath*>(path);
	agg::path_storage* storage = static_cast<agg::path_storage*>(aggPath->fPath);

	double x1, y1, x2, y2;
	
	if (transform != NULL && transform->transform != NULL) {
		agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
		agg::bounding_rect_single(*storage, *t, &x1, &y1, &x2, &y2);
	} else {
		agg::bounding_rect_single(*storage, 0, &x1, &y1, &x2, &y2);
	}

	return BRect(x1, y1, x2, y2);
}


BRect
AGGRender::CalculateBoundingRectD(const BPoint* points, int32 count)
{
	if (points == NULL || count <= 0)
		return BRect();

	double minX = points[0].x, minY = points[0].y;
	double maxX = points[0].x, maxY = points[0].y;

	for (int32 i = 1; i < count; i++) {
		minX = min_c(minX, (double)points[i].x);
		minY = min_c(minY, (double)points[i].y);
		maxX = max_c(maxX, (double)points[i].x);
		maxY = max_c(maxY, (double)points[i].y);
	}

	return BRect(minX, minY, maxX, maxY);
}


// Rectangle operations with double precision
AGGRender::RectD
AGGRender::CreateRectD(double x1, double y1, double x2, double y2)
{
	RectD rect;
	rect.x1 = x1;
	rect.y1 = y1;
	rect.x2 = x2;
	rect.y2 = y2;
	return rect;
}


AGGRender::RectD
AGGRender::TransformRectD(const RectD& rect, AffineTransformHandle* transform)
{
	if (transform == NULL || transform->transform == NULL)
		return rect;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	
	// Transform all four corners
	double x1 = rect.x1, y1 = rect.y1;
	double x2 = rect.x2, y2 = rect.y1;
	double x3 = rect.x2, y3 = rect.y2;
	double x4 = rect.x1, y4 = rect.y2;
	
	t->transform(&x1, &y1);
	t->transform(&x2, &y2);
	t->transform(&x3, &y3);
	t->transform(&x4, &y4);
	
	// Find bounding rect
	double minX = min_c(min_c(x1, x2), min_c(x3, x4));
	double minY = min_c(min_c(y1, y2), min_c(y3, y4));
	double maxX = max_c(max_c(x1, x2), max_c(x3, x4));
	double maxY = max_c(max_c(y1, y2), max_c(y3, y4));
	
	return CreateRectD(minX, minY, maxX, maxY);
}


BRect
AGGRender::RectDToBRect(const RectD& rect)
{
	return BRect(rect.x1, rect.y1, rect.x2, rect.y2);
}


AGGRender::RectD
AGGRender::BRectToRectD(const BRect& rect)
{
	return CreateRectD(rect.left, rect.top, rect.right, rect.bottom);
}


// Transform utility functions
bool
AGGRender::IsTransformIdentity(AffineTransformHandle* transform)
{
	if (transform == NULL || transform->transform == NULL)
		return true;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	return t->is_identity();
}


bool
AGGRender::IsTransformValid(AffineTransformHandle* transform)
{
	if (transform == NULL || transform->transform == NULL)
		return false;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	return t->is_valid();
}


double
AGGRender::GetTransformScale(AffineTransformHandle* transform)
{
	if (transform == NULL || transform->transform == NULL)
		return 1.0;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	return t->scale();
}


double
AGGRender::GetTransformRotation(AffineTransformHandle* transform)
{
	if (transform == NULL || transform->transform == NULL)
		return 0.0;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(transform->transform);
	return t->rotation();
}


// Convert between BAffineTransform and AGG transform
status_t
AGGRender::ConvertToAGGTransform(const BAffineTransform& haikuTransform, 
								 AffineTransformHandle** aggTransform)
{
	if (aggTransform == NULL)
		return B_BAD_VALUE;

	*aggTransform = CreateAffineTransformFromMatrix(
		haikuTransform.sx, haikuTransform.shy, haikuTransform.shx,
		haikuTransform.sy, haikuTransform.tx, haikuTransform.ty);

	return (*aggTransform != NULL) ? B_OK : B_NO_MEMORY;
}


status_t
AGGRender::ConvertFromAGGTransform(AffineTransformHandle* aggTransform,
								   BAffineTransform* haikuTransform)
{
	if (aggTransform == NULL || aggTransform->transform == NULL || haikuTransform == NULL)
		return B_BAD_VALUE;

	agg::trans_affine* t = static_cast<agg::trans_affine*>(aggTransform->transform);
	
	haikuTransform->sx = t->sx;
	haikuTransform->shy = t->shy;
	haikuTransform->shx = t->shx;
	haikuTransform->sy = t->sy;
	haikuTransform->tx = t->tx;
	haikuTransform->ty = t->ty;

	return B_OK;
}