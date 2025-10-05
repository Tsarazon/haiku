/*
 * Copyright 2005, Stephan Aßmus <superstippi@gmx.de>. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * A handy front-end to BLMatrix2D transformation matrix.
 *
 */

#include <stdio.h>
#include <string.h>

#include <Message.h>

#include "Transformable.h"


inline float
min4(float a, float b, float c, float d)
{
	return min_c(a, min_c(b, min_c(c, d)));
} 


inline float
max4(float a, float b, float c, float d)
{
	return max_c(a, max_c(b, max_c(c, d)));
} 


Transformable::Transformable()
	: fMatrix(BLMatrix2D::makeIdentity())
{
}


Transformable::Transformable(const Transformable& other)
	: fMatrix(other.fMatrix)
{
}


Transformable::Transformable(const BMessage* archive)
	: fMatrix(BLMatrix2D::makeIdentity())
{
	if (archive != NULL) {
		double storage[6];
		status_t ret = B_OK;
		for (int32 i = 0; i < 6; i++) {
			ret = archive->FindDouble("affine matrix", i, &storage[i]);
			if (ret < B_OK)
				break;
		}
		if (ret >= B_OK) {
			fMatrix.reset(storage[0], storage[1], storage[2], 
						  storage[3], storage[4], storage[5]);
		}
	}
}


Transformable::~Transformable()
{
}


status_t
Transformable::Archive(BMessage* into, bool deep) const
{
	status_t ret = BArchivable::Archive(into, deep);
	if (ret == B_OK) {
		double storage[6];
		StoreTo(storage);
		for (int32 i = 0; i < 6; i++) {
			ret = into->AddDouble("affine matrix", storage[i]);
			if (ret < B_OK)
				break;
		}
		// finish off
		if (ret == B_OK)
			ret = into->AddString("class", "Transformable");
	}
	return ret;
}


void
Transformable::StoreTo(double matrix[6]) const
{
	matrix[0] = fMatrix.m00;
	matrix[1] = fMatrix.m01;
	matrix[2] = fMatrix.m10;
	matrix[3] = fMatrix.m11;
	matrix[4] = fMatrix.m20;
	matrix[5] = fMatrix.m21;
}


void
Transformable::LoadFrom(double matrix[6])
{
	// Before calling the potentially heavy TransformationChanged()
	// hook function, we make sure that it is actually true
	Transformable t;
	t.fMatrix.reset(matrix[0], matrix[1], matrix[2], 
					matrix[3], matrix[4], matrix[5]);
	if (*this != t) {
		fMatrix.reset(matrix[0], matrix[1], matrix[2], 
					  matrix[3], matrix[4], matrix[5]);
		TransformationChanged();
	}
}


void
Transformable::SetTransformable(const Transformable& other)
{
	if (*this != other) {
		*this = other;
		TransformationChanged();
	}
}


Transformable&
Transformable::operator=(const Transformable& other)
{
	if (other != *this) {
		fMatrix = other.fMatrix;
		TransformationChanged();
	}
	return *this;
}


Transformable&
Transformable::operator=(const BLMatrix2D& other)
{
	if (other != fMatrix) {
		fMatrix = other;
		TransformationChanged();
	}
	return *this;
}


Transformable&
Transformable::Multiply(const Transformable& other)
{
	if (!other.IsIdentity()) {
		fMatrix.transform(other.fMatrix);
		TransformationChanged();
	}
	return *this;
}


void
Transformable::Reset()
{
	fMatrix.reset();
}


bool
Transformable::IsIdentity() const
{
	return fMatrix.m00 == 1.0 &&
		   fMatrix.m01 == 0.0 &&
		   fMatrix.m10 == 0.0 &&
		   fMatrix.m11 == 1.0 &&
		   fMatrix.m20 == 0.0 &&
		   fMatrix.m21 == 0.0;
}


bool
Transformable::IsDilation() const
{
	return fMatrix.m01 == 0.0 && fMatrix.m10 == 0.0;
}


bool
Transformable::operator==(const Transformable& other) const
{
	return fMatrix == other.fMatrix;
}


bool
Transformable::operator!=(const Transformable& other) const
{
	return fMatrix != other.fMatrix;
}


void
Transformable::Transform(double* x, double* y) const
{
	BLPoint p = fMatrix.mapPoint(*x, *y);
	*x = p.x;
	*y = p.y;
}


void
Transformable::Transform(BPoint* point) const
{
	if (point) {
		double x = point->x;
		double y = point->y;

		BLPoint p = fMatrix.mapPoint(x, y);
	
		point->x = p.x;
		point->y = p.y;
	}
}


BPoint
Transformable::Transform(const BPoint& point) const
{
	BPoint p(point);
	Transform(&p);
	return p;
}


void
Transformable::InverseTransform(double* x, double* y) const
{
	BLMatrix2D inverted;
	BLMatrix2D::invert(inverted, fMatrix);
	BLPoint p = inverted.mapPoint(*x, *y);
	*x = p.x;
	*y = p.y;
}


void
Transformable::InverseTransform(BPoint* point) const
{
	if (point) {
		double x = point->x;
		double y = point->y;

		BLMatrix2D inverted;
		BLMatrix2D::invert(inverted, fMatrix);
		BLPoint p = inverted.mapPoint(x, y);
	
		point->x = p.x;
		point->y = p.y;
	}
}


BPoint
Transformable::InverseTransform(const BPoint& point) const
{
	BPoint p(point);
	InverseTransform(&p);
	return p;
}


BRect
Transformable::TransformBounds(const BRect& bounds) const
{
	if (bounds.IsValid()) {
		BPoint lt(bounds.left, bounds.top);
		BPoint rt(bounds.right, bounds.top);
		BPoint lb(bounds.left, bounds.bottom);
		BPoint rb(bounds.right, bounds.bottom);
	
		Transform(&lt);
		Transform(&rt);
		Transform(&lb);
		Transform(&rb);
	
		return BRect(floorf(min4(lt.x, rt.x, lb.x, rb.x)),
					 floorf(min4(lt.y, rt.y, lb.y, rb.y)),
					 ceilf(max4(lt.x, rt.x, lb.x, rb.x)),
					 ceilf(max4(lt.y, rt.y, lb.y, rb.y)));
	}
	return bounds;
}


bool
Transformable::IsTranslationOnly() const
{
	return fMatrix.m00 == 1.0 && fMatrix.m01 == 0.0
		&& fMatrix.m10 == 0.0 && fMatrix.m11 == 1.0;
}



void
Transformable::TranslateBy(BPoint offset)
{
	if (offset.x != 0.0 || offset.y != 0.0) {
		fMatrix.translate(offset.x, offset.y);
		TransformationChanged();
	}
}


void
Transformable::RotateBy(BPoint origin, double radians)
{
	if (radians != 0.0) {
		fMatrix.translate(-origin.x, -origin.y);
		fMatrix.rotate(radians);
		fMatrix.translate(origin.x, origin.y);
		TransformationChanged();
	}
}


void
Transformable::ScaleBy(BPoint origin, double xScale, double yScale)
{
	if (xScale != 1.0 || yScale != 1.0) {
		fMatrix.translate(-origin.x, -origin.y);
		fMatrix.scale(xScale, yScale);
		fMatrix.translate(origin.x, origin.y);
		TransformationChanged();
	}
}


void
Transformable::ShearBy(BPoint origin, double xShear, double yShear)
{
	if (xShear != 0.0 || yShear != 0.0) {
		fMatrix.translate(-origin.x, -origin.y);
		fMatrix.skew(xShear, yShear);
		fMatrix.translate(origin.x, origin.y);
		TransformationChanged();
	}
}
