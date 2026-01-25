/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_GRADIENT_HPP
#define _KOSM_GRADIENT_HPP

#include <KosmColor.hpp>
#include <KosmGeometry.hpp>

enum kosm_gradient_spread {
	KOSM_GRADIENT_SPREAD_PAD = 0,
	KOSM_GRADIENT_SPREAD_REFLECT,
	KOSM_GRADIENT_SPREAD_REPEAT
};

struct KosmColorStop {
	float		offset;		// 0.0 - 1.0
	KosmColor	color;

	KosmColorStop()
		: offset(0), color() {}

	KosmColorStop(float o, const KosmColor& c)
		: offset(o), color(c) {}
};


class KosmGradient {
public:
	virtual					~KosmGradient();

			void			AddColorStop(float offset, const KosmColor& color);
			void			AddColorStop(const KosmColorStop& stop);
			void			SetColorStops(const KosmColorStop* stops,
								uint32 count);
			void			ClearColorStops();

			uint32			CountColorStops() const;
			KosmColorStop	ColorStopAt(uint32 index) const;

			void			SetSpread(kosm_gradient_spread spread);
			kosm_gradient_spread Spread() const;

			void			SetTransform(const KosmMatrix& matrix);
			KosmMatrix		Transform() const;

	virtual void*			NativeHandle() const = 0;

protected:
							KosmGradient();

			struct Data;
			Data*			fData;
};


class KosmLinearGradient : public KosmGradient {
public:
							KosmLinearGradient();
							KosmLinearGradient(const KosmPoint& start,
								const KosmPoint& end);
							KosmLinearGradient(float x1, float y1,
								float x2, float y2);
							~KosmLinearGradient();

			void			SetPoints(const KosmPoint& start,
								const KosmPoint& end);
			void			SetPoints(float x1, float y1, float x2, float y2);

			KosmPoint		StartPoint() const;
			KosmPoint		EndPoint() const;

	virtual void*			NativeHandle() const;

private:
			struct LinearData;
			LinearData*		fLinearData;
};


class KosmRadialGradient : public KosmGradient {
public:
							KosmRadialGradient();
							KosmRadialGradient(const KosmPoint& center,
								float radius);
							KosmRadialGradient(const KosmPoint& center,
								float radius,
								const KosmPoint& focal,
								float focalRadius);
							~KosmRadialGradient();

			void			SetCenter(const KosmPoint& center, float radius);
			void			SetFocal(const KosmPoint& focal, float radius);

			KosmPoint		Center() const;
			float			Radius() const;
			KosmPoint		Focal() const;
			float			FocalRadius() const;

	virtual void*			NativeHandle() const;

private:
			struct RadialData;
			RadialData*		fRadialData;
};

#endif /* _KOSM_GRADIENT_HPP */
