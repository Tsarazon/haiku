/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmGradient.hpp>

#include "RenderBackend.hpp"

#include <cstring>
#include <new>
#include <vector>


struct KosmGradient::Data {
	RenderBackend*				backend;
	std::vector<KosmColorStop>	colorStops;
	kosm_gradient_spread		spread;
	KosmMatrix					transform;

	Data()
		:
		backend(RenderBackend::Create()),
		spread(KOSM_GRADIENT_SPREAD_PAD)
	{
	}

	virtual ~Data()
	{
		delete backend;
	}
};


KosmGradient::KosmGradient()
	:
	fData(nullptr)
{
}


KosmGradient::~KosmGradient()
{
}


void
KosmGradient::AddColorStop(float offset, const KosmColor& color)
{
	if (fData != nullptr)
		fData->colorStops.push_back(KosmColorStop(offset, color));
}


void
KosmGradient::AddColorStop(const KosmColorStop& stop)
{
	if (fData != nullptr)
		fData->colorStops.push_back(stop);
}


void
KosmGradient::SetColorStops(const KosmColorStop* stops, uint32 count)
{
	if (fData == nullptr)
		return;

	fData->colorStops.clear();
	for (uint32 i = 0; i < count; i++)
		fData->colorStops.push_back(stops[i]);
}


void
KosmGradient::ClearColorStops()
{
	if (fData != nullptr)
		fData->colorStops.clear();
}


uint32
KosmGradient::CountColorStops() const
{
	return fData != nullptr ? fData->colorStops.size() : 0;
}


KosmColorStop
KosmGradient::ColorStopAt(uint32 index) const
{
	if (fData != nullptr && index < fData->colorStops.size())
		return fData->colorStops[index];
	return KosmColorStop();
}


void
KosmGradient::SetSpread(kosm_gradient_spread spread)
{
	if (fData != nullptr)
		fData->spread = spread;
}


kosm_gradient_spread
KosmGradient::Spread() const
{
	return fData != nullptr ? fData->spread : KOSM_GRADIENT_SPREAD_PAD;
}


void
KosmGradient::SetTransform(const KosmMatrix& matrix)
{
	if (fData != nullptr)
		fData->transform = matrix;
}


KosmMatrix
KosmGradient::Transform() const
{
	return fData != nullptr ? fData->transform : KosmMatrix::Identity();
}


// ============================================================================
// KosmLinearGradient
// ============================================================================

struct KosmLinearGradient::LinearData {
	void*		handle;
	KosmPoint	start;
	KosmPoint	end;

	LinearData()
		: handle(nullptr), start(0, 0), end(1, 0)
	{
	}
};


KosmLinearGradient::KosmLinearGradient()
	:
	KosmGradient(),
	fLinearData(new(std::nothrow) LinearData)
{
	fData = new(std::nothrow) KosmGradient::Data;
	if (fData != nullptr && fLinearData != nullptr && fData->backend != nullptr) {
		fLinearData->handle = fData->backend->CreateLinearGradient(0, 0, 1, 0);
	}
}


KosmLinearGradient::KosmLinearGradient(const KosmPoint& start, const KosmPoint& end)
	:
	KosmGradient(),
	fLinearData(new(std::nothrow) LinearData)
{
	fData = new(std::nothrow) KosmGradient::Data;
	if (fData != nullptr && fLinearData != nullptr && fData->backend != nullptr) {
		fLinearData->start = start;
		fLinearData->end = end;
		fLinearData->handle = fData->backend->CreateLinearGradient(
			start.x, start.y, end.x, end.y);
	}
}


KosmLinearGradient::KosmLinearGradient(float x1, float y1, float x2, float y2)
	:
	KosmLinearGradient(KosmPoint(x1, y1), KosmPoint(x2, y2))
{
}


KosmLinearGradient::~KosmLinearGradient()
{
	if (fData != nullptr && fLinearData != nullptr &&
		fData->backend != nullptr && fLinearData->handle != nullptr) {
		fData->backend->DestroyGradient(fLinearData->handle);
	}
	delete fLinearData;
	delete fData;
}


void
KosmLinearGradient::SetPoints(const KosmPoint& start, const KosmPoint& end)
{
	if (fData == nullptr || fLinearData == nullptr || fData->backend == nullptr)
		return;

	fLinearData->start = start;
	fLinearData->end = end;

	if (fLinearData->handle != nullptr)
		fData->backend->DestroyGradient(fLinearData->handle);

	fLinearData->handle = fData->backend->CreateLinearGradient(
		start.x, start.y, end.x, end.y);

	// Re-add color stops
	for (const auto& stop : fData->colorStops) {
		fData->backend->GradientAddColorStop(fLinearData->handle,
			stop.offset, stop.color);
	}
}


void
KosmLinearGradient::SetPoints(float x1, float y1, float x2, float y2)
{
	SetPoints(KosmPoint(x1, y1), KosmPoint(x2, y2));
}


KosmPoint
KosmLinearGradient::StartPoint() const
{
	return fLinearData != nullptr ? fLinearData->start : KosmPoint();
}


KosmPoint
KosmLinearGradient::EndPoint() const
{
	return fLinearData != nullptr ? fLinearData->end : KosmPoint();
}


void*
KosmLinearGradient::NativeHandle() const
{
	if (fData == nullptr || fLinearData == nullptr)
		return nullptr;

	// Update native handle with current color stops
	for (const auto& stop : fData->colorStops) {
		fData->backend->GradientAddColorStop(fLinearData->handle,
			stop.offset, stop.color);
	}

	fData->backend->GradientSetSpread(fLinearData->handle, fData->spread);

	if (!fData->transform.IsIdentity()) {
		fData->backend->GradientSetTransform(fLinearData->handle,
			fData->transform);
	}

	return fLinearData->handle;
}


// ============================================================================
// KosmRadialGradient
// ============================================================================

struct KosmRadialGradient::RadialData {
	void*		handle;
	KosmPoint	center;
	float		radius;
	KosmPoint	focal;
	float		focalRadius;

	RadialData()
		: handle(nullptr),
		  center(0.5f, 0.5f),
		  radius(0.5f),
		  focal(0.5f, 0.5f),
		  focalRadius(0)
	{
	}
};


KosmRadialGradient::KosmRadialGradient()
	:
	KosmGradient(),
	fRadialData(new(std::nothrow) RadialData)
{
	fData = new(std::nothrow) KosmGradient::Data;
	if (fData != nullptr && fRadialData != nullptr && fData->backend != nullptr) {
		fRadialData->handle = fData->backend->CreateRadialGradient(
			0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0);
	}
}


KosmRadialGradient::KosmRadialGradient(const KosmPoint& center, float radius)
	:
	KosmGradient(),
	fRadialData(new(std::nothrow) RadialData)
{
	fData = new(std::nothrow) KosmGradient::Data;
	if (fData != nullptr && fRadialData != nullptr && fData->backend != nullptr) {
		fRadialData->center = center;
		fRadialData->radius = radius;
		fRadialData->focal = center;
		fRadialData->focalRadius = 0;
		fRadialData->handle = fData->backend->CreateRadialGradient(
			center.x, center.y, radius, center.x, center.y, 0);
	}
}


KosmRadialGradient::KosmRadialGradient(const KosmPoint& center, float radius,
	const KosmPoint& focal, float focalRadius)
	:
	KosmGradient(),
	fRadialData(new(std::nothrow) RadialData)
{
	fData = new(std::nothrow) KosmGradient::Data;
	if (fData != nullptr && fRadialData != nullptr && fData->backend != nullptr) {
		fRadialData->center = center;
		fRadialData->radius = radius;
		fRadialData->focal = focal;
		fRadialData->focalRadius = focalRadius;
		fRadialData->handle = fData->backend->CreateRadialGradient(
			center.x, center.y, radius, focal.x, focal.y, focalRadius);
	}
}


KosmRadialGradient::~KosmRadialGradient()
{
	if (fData != nullptr && fRadialData != nullptr &&
		fData->backend != nullptr && fRadialData->handle != nullptr) {
		fData->backend->DestroyGradient(fRadialData->handle);
	}
	delete fRadialData;
	delete fData;
}


void
KosmRadialGradient::SetCenter(const KosmPoint& center, float radius)
{
	if (fData == nullptr || fRadialData == nullptr || fData->backend == nullptr)
		return;

	fRadialData->center = center;
	fRadialData->radius = radius;

	if (fRadialData->handle != nullptr)
		fData->backend->DestroyGradient(fRadialData->handle);

	fRadialData->handle = fData->backend->CreateRadialGradient(
		center.x, center.y, radius,
		fRadialData->focal.x, fRadialData->focal.y, fRadialData->focalRadius);

	for (const auto& stop : fData->colorStops) {
		fData->backend->GradientAddColorStop(fRadialData->handle,
			stop.offset, stop.color);
	}
}


void
KosmRadialGradient::SetFocal(const KosmPoint& focal, float radius)
{
	if (fRadialData != nullptr) {
		fRadialData->focal = focal;
		fRadialData->focalRadius = radius;
	}
}


KosmPoint
KosmRadialGradient::Center() const
{
	return fRadialData != nullptr ? fRadialData->center : KosmPoint();
}


float
KosmRadialGradient::Radius() const
{
	return fRadialData != nullptr ? fRadialData->radius : 0;
}


KosmPoint
KosmRadialGradient::Focal() const
{
	return fRadialData != nullptr ? fRadialData->focal : KosmPoint();
}


float
KosmRadialGradient::FocalRadius() const
{
	return fRadialData != nullptr ? fRadialData->focalRadius : 0;
}


void*
KosmRadialGradient::NativeHandle() const
{
	if (fData == nullptr || fRadialData == nullptr)
		return nullptr;

	for (const auto& stop : fData->colorStops) {
		fData->backend->GradientAddColorStop(fRadialData->handle,
			stop.offset, stop.color);
	}

	fData->backend->GradientSetSpread(fRadialData->handle, fData->spread);

	if (!fData->transform.IsIdentity()) {
		fData->backend->GradientSetTransform(fRadialData->handle,
			fData->transform);
	}

	return fRadialData->handle;
}
