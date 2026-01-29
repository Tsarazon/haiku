/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <KosmPath.hpp>

#include "RenderBackend.hpp"

#include <cmath>
#include <new>


struct KosmPath::Data {
	RenderBackend*		backend;
	void*				handle;
	KosmPoint			currentPoint;
	kosm_fill_rule		fillRule;

	Data()
		:
		backend(nullptr),
		handle(nullptr),
		currentPoint(0, 0),
		fillRule(KOSM_FILL_RULE_NON_ZERO)
	{
		backend = RenderBackend::Instance();
		if (backend != nullptr)
			handle = backend->CreatePath();
	}

	~Data()
	{
		if (backend != nullptr && handle != nullptr)
			backend->DestroyPath(handle);
		// Don't delete backend - it's a singleton
	}
};


KosmPath::KosmPath()
	:
	fData(new(std::nothrow) Data)
{
}


KosmPath::KosmPath(const KosmPath& other)
	:
	fData(new(std::nothrow) Data)
{
	if (fData != nullptr && other.fData != nullptr &&
		fData->backend != nullptr && other.fData->handle != nullptr) {

		if (fData->handle != nullptr)
			fData->backend->DestroyPath(fData->handle);

		fData->handle = fData->backend->DuplicatePath(other.fData->handle);
		fData->currentPoint = other.fData->currentPoint;
		fData->fillRule = other.fData->fillRule;
	}
}


KosmPath::~KosmPath()
{
	delete fData;
}


KosmPath&
KosmPath::operator=(const KosmPath& other)
{
	if (this != &other && fData != nullptr && other.fData != nullptr) {
		if (fData->handle != nullptr)
			fData->backend->DestroyPath(fData->handle);

		fData->handle = fData->backend->DuplicatePath(other.fData->handle);
		fData->currentPoint = other.fData->currentPoint;
		fData->fillRule = other.fData->fillRule;
	}
	return *this;
}


void
KosmPath::MoveTo(float x, float y)
{
	if (fData != nullptr && fData->backend != nullptr) {
		fData->backend->PathMoveTo(fData->handle, x, y);
		fData->currentPoint = KosmPoint(x, y);
	}
}


void
KosmPath::MoveTo(const KosmPoint& point)
{
	MoveTo(point.x, point.y);
}


void
KosmPath::LineTo(float x, float y)
{
	if (fData != nullptr && fData->backend != nullptr) {
		fData->backend->PathLineTo(fData->handle, x, y);
		fData->currentPoint = KosmPoint(x, y);
	}
}


void
KosmPath::LineTo(const KosmPoint& point)
{
	LineTo(point.x, point.y);
}


void
KosmPath::CubicTo(float cx1, float cy1, float cx2, float cy2, float x, float y)
{
	if (fData != nullptr && fData->backend != nullptr) {
		fData->backend->PathCubicTo(fData->handle, cx1, cy1, cx2, cy2, x, y);
		fData->currentPoint = KosmPoint(x, y);
	}
}


void
KosmPath::CubicTo(const KosmPoint& control1, const KosmPoint& control2,
	const KosmPoint& end)
{
	CubicTo(control1.x, control1.y, control2.x, control2.y, end.x, end.y);
}


void
KosmPath::QuadTo(float cx, float cy, float x, float y)
{
	if (fData == nullptr)
		return;

	// Convert quadratic bezier to cubic
	float x0 = fData->currentPoint.x;
	float y0 = fData->currentPoint.y;

	float cx1 = x0 + 2.0f / 3.0f * (cx - x0);
	float cy1 = y0 + 2.0f / 3.0f * (cy - y0);
	float cx2 = x + 2.0f / 3.0f * (cx - x);
	float cy2 = y + 2.0f / 3.0f * (cy - y);

	CubicTo(cx1, cy1, cx2, cy2, x, y);
}


void
KosmPath::QuadTo(const KosmPoint& control, const KosmPoint& end)
{
	QuadTo(control.x, control.y, end.x, end.y);
}


void
KosmPath::ArcTo(float rx, float ry, float rotation, bool largeArc,
	bool sweep, float x, float y)
{
	if (fData == nullptr || rx <= 0 || ry <= 0)
		return;

	// SVG arc to bezier conversion
	float x0 = fData->currentPoint.x;
	float y0 = fData->currentPoint.y;

	if (x0 == x && y0 == y)
		return;

	float cosRot = cosf(rotation);
	float sinRot = sinf(rotation);

	// Transform to unit circle space
	float dx = (x0 - x) / 2.0f;
	float dy = (y0 - y) / 2.0f;
	float x1 = cosRot * dx + sinRot * dy;
	float y1 = -sinRot * dx + cosRot * dy;

	// Scale radii
	float lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
	if (lambda > 1.0f) {
		float sqrtLambda = sqrtf(lambda);
		rx *= sqrtLambda;
		ry *= sqrtLambda;
	}

	// Compute center
	float sq = ((rx * rx) * (ry * ry) - (rx * rx) * (y1 * y1) - (ry * ry) * (x1 * x1)) /
			   ((rx * rx) * (y1 * y1) + (ry * ry) * (x1 * x1));
	if (sq < 0)
		sq = 0;
	float coef = sqrtf(sq) * ((largeArc == sweep) ? -1 : 1);

	float cx1 = coef * rx * y1 / ry;
	float cy1 = -coef * ry * x1 / rx;

	float cx = cosRot * cx1 - sinRot * cy1 + (x0 + x) / 2.0f;
	float cy = sinRot * cx1 + cosRot * cy1 + (y0 + y) / 2.0f;

	// Compute angles
	auto angle = [](float ux, float uy, float vx, float vy) {
		float dot = ux * vx + uy * vy;
		float len = sqrtf((ux * ux + uy * uy) * (vx * vx + vy * vy));
		float ang = acosf(fmaxf(-1.0f, fminf(1.0f, dot / len)));
		if (ux * vy - uy * vx < 0)
			ang = -ang;
		return ang;
	};

	float theta1 = angle(1.0f, 0.0f, (x1 - cx1) / rx, (y1 - cy1) / ry);
	float dtheta = angle((x1 - cx1) / rx, (y1 - cy1) / ry,
						(-x1 - cx1) / rx, (-y1 - cy1) / ry);

	if (!sweep && dtheta > 0)
		dtheta -= 2.0f * M_PI;
	else if (sweep && dtheta < 0)
		dtheta += 2.0f * M_PI;

	// Draw arc segments
	int segments = static_cast<int>(ceilf(fabsf(dtheta) / (M_PI / 2.0f)));
	float delta = dtheta / segments;
	float t = tanf(delta / 4.0f) * 4.0f / 3.0f;

	for (int i = 0; i < segments; i++) {
		float theta = theta1 + i * delta;
		float thetaNext = theta + delta;

		float cos1 = cosf(theta);
		float sin1 = sinf(theta);
		float cos2 = cosf(thetaNext);
		float sin2 = sinf(thetaNext);

		float px2 = cx + rx * cos2 * cosRot - ry * sin2 * sinRot;
		float py2 = cy + rx * cos2 * sinRot + ry * sin2 * cosRot;

		float c1x = (cx + rx * cos1 * cosRot - ry * sin1 * sinRot) -
					t * (rx * (-sin1) * cosRot - ry * cos1 * sinRot);
		float c1y = (cy + rx * cos1 * sinRot + ry * sin1 * cosRot) -
					t * (rx * (-sin1) * sinRot + ry * cos1 * cosRot);
		float c2x = px2 + t * (rx * (-sin2) * cosRot - ry * cos2 * sinRot);
		float c2y = py2 + t * (rx * (-sin2) * sinRot + ry * cos2 * cosRot);

		CubicTo(c1x, c1y, c2x, c2y, px2, py2);
	}
}


void
KosmPath::Close()
{
	if (fData != nullptr && fData->backend != nullptr)
		fData->backend->PathClose(fData->handle);
}


void
KosmPath::Reset()
{
	if (fData != nullptr && fData->backend != nullptr) {
		fData->backend->PathReset(fData->handle);
		fData->currentPoint = KosmPoint(0, 0);
	}
}


void
KosmPath::AddRect(const KosmRect& rect)
{
	if (fData != nullptr && fData->backend != nullptr)
		fData->backend->PathAddRect(fData->handle, rect);
}


void
KosmPath::AddRoundRect(const KosmRect& rect, float radius)
{
	AddRoundRect(rect, radius, radius);
}


void
KosmPath::AddRoundRect(const KosmRect& rect, float rx, float ry)
{
	if (fData != nullptr && fData->backend != nullptr)
		fData->backend->PathAddRoundRect(fData->handle, rect, rx, ry);
}


void
KosmPath::AddCircle(const KosmPoint& center, float radius)
{
	if (fData != nullptr && fData->backend != nullptr)
		fData->backend->PathAddCircle(fData->handle, center, radius);
}


void
KosmPath::AddEllipse(const KosmPoint& center, float rx, float ry)
{
	if (fData != nullptr && fData->backend != nullptr)
		fData->backend->PathAddEllipse(fData->handle, center, rx, ry);
}


void
KosmPath::AddArc(const KosmPoint& center, float radius,
	float startAngle, float sweepAngle)
{
	if (fData != nullptr && fData->backend != nullptr)
		fData->backend->PathAddArc(fData->handle, center, radius,
			startAngle, sweepAngle);
}


void
KosmPath::AddLine(const KosmPoint& from, const KosmPoint& to)
{
	MoveTo(from);
	LineTo(to);
}


void
KosmPath::Append(const KosmPath& other)
{
	if (fData != nullptr && fData->backend != nullptr &&
		other.fData != nullptr && other.fData->handle != nullptr) {
		fData->backend->PathAppend(fData->handle, other.fData->handle);
	}
}


void
KosmPath::Transform(const KosmMatrix& matrix)
{
	(void)matrix;
}


KosmPath
KosmPath::Transformed(const KosmMatrix& matrix) const
{
	KosmPath result(*this);
	result.Transform(matrix);
	return result;
}


void
KosmPath::Reverse()
{
}


void
KosmPath::SetFillRule(kosm_fill_rule rule)
{
	if (fData != nullptr) {
		fData->fillRule = rule;
		if (fData->backend != nullptr && fData->handle != nullptr)
			fData->backend->PathSetFillRule(fData->handle, rule);
	}
}


kosm_fill_rule
KosmPath::FillRule() const
{
	return fData != nullptr ? fData->fillRule : KOSM_FILL_RULE_NON_ZERO;
}


bool
KosmPath::IsEmpty() const
{
	if (fData == nullptr || fData->backend == nullptr)
		return true;

	KosmRect bounds = fData->backend->PathBounds(fData->handle);
	return bounds.IsEmpty();
}


KosmRect
KosmPath::Bounds() const
{
	if (fData != nullptr && fData->backend != nullptr)
		return fData->backend->PathBounds(fData->handle);
	return KosmRect();
}


bool
KosmPath::Contains(const KosmPoint& point) const
{
	(void)point;
	return false;
}


float
KosmPath::Length() const
{
	return 0;
}


KosmPoint
KosmPath::PointAt(float t) const
{
	(void)t;
	return KosmPoint();
}


void*
KosmPath::NativeHandle() const
{
	return fData != nullptr ? fData->handle : nullptr;
}
