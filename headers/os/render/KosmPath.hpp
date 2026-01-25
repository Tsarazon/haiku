/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_PATH_HPP
#define _KOSM_PATH_HPP

#include <KosmGeometry.hpp>

class RenderBackend;

class KosmPath {
public:
							KosmPath();
							KosmPath(const KosmPath& other);
							~KosmPath();

			KosmPath&		operator=(const KosmPath& other);

	// Path construction
			void			MoveTo(float x, float y);
			void			MoveTo(const KosmPoint& point);

			void			LineTo(float x, float y);
			void			LineTo(const KosmPoint& point);

			void			CubicTo(float cx1, float cy1,
								float cx2, float cy2,
								float x, float y);
			void			CubicTo(const KosmPoint& control1,
								const KosmPoint& control2,
								const KosmPoint& end);

			void			QuadTo(float cx, float cy, float x, float y);
			void			QuadTo(const KosmPoint& control,
								const KosmPoint& end);

			void			ArcTo(float rx, float ry, float rotation,
								bool largeArc, bool sweep,
								float x, float y);

			void			Close();
			void			Reset();

	// Shape helpers
			void			AddRect(const KosmRect& rect);
			void			AddRoundRect(const KosmRect& rect, float radius);
			void			AddRoundRect(const KosmRect& rect,
								float rx, float ry);
			void			AddCircle(const KosmPoint& center, float radius);
			void			AddEllipse(const KosmPoint& center,
								float rx, float ry);
			void			AddArc(const KosmPoint& center, float radius,
								float startAngle, float sweepAngle);
			void			AddLine(const KosmPoint& from, const KosmPoint& to);

	// Path operations
			void			Append(const KosmPath& other);
			void			Transform(const KosmMatrix& matrix);
			KosmPath		Transformed(const KosmMatrix& matrix) const;
			void			Reverse();

	// Queries
			bool			IsEmpty() const;
			KosmRect		Bounds() const;
			bool			Contains(const KosmPoint& point) const;
			float			Length() const;
			KosmPoint		PointAt(float t) const;

	// Internal access
			void*			NativeHandle() const;

private:
	friend class RenderBackend;

			struct Data;
			Data*			fData;
};

#endif /* _KOSM_PATH_HPP */