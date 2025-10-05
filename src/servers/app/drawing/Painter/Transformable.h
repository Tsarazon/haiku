/*
 * Copyright 2005, Stephan Aßmus <superstippi@gmx.de>. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * A handy front-end to BLMatrix2D transformation matrix.
 *
 */


#ifndef TRANSFORMABLE_H
#define TRANSFORMABLE_H

#include <Archivable.h>
#include <Rect.h>

#include <blend2d.h>

class Transformable : public BArchivable {
 public:
								Transformable();
								Transformable(const Transformable& other);
								Transformable(const BMessage* archive);
	virtual						~Transformable();

								// the BArchivable protocol
								// stores matrix directly to message, deep is ignored
	virtual	status_t			Archive(BMessage* into, bool deep = true) const;

			void				StoreTo(double matrix[6]) const;
			void				LoadFrom(double matrix[6]);

								// set to or combine with other matrix
			void				SetTransformable(const Transformable& other);
			Transformable&		operator=(const BLMatrix2D& other);
			Transformable&		operator=(const Transformable& other);
			Transformable&		Multiply(const Transformable& other);
			void				Reset();

			bool				IsIdentity() const;
			bool				IsDilation() const;
			bool				operator==(const Transformable& other) const;
			bool				operator!=(const Transformable& other) const;

								// transforms coordiantes
			void				Transform(double* x, double* y) const;
			void				Transform(BPoint* point) const;
			BPoint				Transform(const BPoint& point) const;

			void				InverseTransform(double* x, double* y) const;
			void				InverseTransform(BPoint* point) const;
			BPoint				InverseTransform(const BPoint& point) const;

								// transforms the rectangle "bounds" and
								// returns the *bounding box* of that
			BRect				TransformBounds(const BRect& bounds) const;

			bool				IsTranslationOnly() const;

								// some convenience functions
	virtual	void				TranslateBy(BPoint offset);
	virtual	void				RotateBy(BPoint origin, double radians);
	virtual	void				ScaleBy(BPoint origin, double xScale, double yScale);
	virtual	void				ShearBy(BPoint origin, double xShear, double yShear);

	virtual	void				TransformationChanged() {}

			// Direct access to BLMatrix2D
			const BLMatrix2D&	Matrix() const { return fMatrix; }
			BLMatrix2D&			Matrix() { return fMatrix; }

 private:
			BLMatrix2D			fMatrix;
};

#endif // TRANSFORMABLE_H
