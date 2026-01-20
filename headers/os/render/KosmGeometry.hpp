/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_GEOMETRY_H
#define _KOSM_GEOMETRY_H

#include <math.h>

struct KosmPoint;
struct KosmSize;
struct KosmRect;
struct KosmMatrix;


struct KosmPoint {
	float	x;
	float	y;

	KosmPoint() : x(0), y(0) {}
	KosmPoint(float x, float y) : x(x), y(y) {}

	KosmPoint operator+(const KosmPoint& other) const {
		return KosmPoint(x + other.x, y + other.y);
	}

	KosmPoint operator-(const KosmPoint& other) const {
		return KosmPoint(x - other.x, y - other.y);
	}

	KosmPoint operator*(float scalar) const {
		return KosmPoint(x * scalar, y * scalar);
	}

	KosmPoint operator/(float scalar) const {
		return KosmPoint(x / scalar, y / scalar);
	}

	KosmPoint& operator+=(const KosmPoint& other) {
		x += other.x;
		y += other.y;
		return *this;
	}

	KosmPoint& operator-=(const KosmPoint& other) {
		x -= other.x;
		y -= other.y;
		return *this;
	}

	KosmPoint& operator*=(float scalar) {
		x *= scalar;
		y *= scalar;
		return *this;
	}

	KosmPoint& operator/=(float scalar) {
		x /= scalar;
		y /= scalar;
		return *this;
	}

	bool operator==(const KosmPoint& other) const {
		return x == other.x && y == other.y;
	}

	bool operator!=(const KosmPoint& other) const {
		return !(*this == other);
	}

	KosmPoint operator-() const {
		return KosmPoint(-x, -y);
	}

	float Length() const {
		return sqrtf(x * x + y * y);
	}

	float LengthSquared() const {
		return x * x + y * y;
	}

	float Dot(const KosmPoint& other) const {
		return x * other.x + y * other.y;
	}

	float Cross(const KosmPoint& other) const {
		return x * other.y - y * other.x;
	}

	KosmPoint Normalized() const {
		float len = Length();
		if (len == 0)
			return KosmPoint(0, 0);
		return *this / len;
	}

	float DistanceTo(const KosmPoint& other) const {
		return (*this - other).Length();
	}

	KosmPoint Lerp(const KosmPoint& other, float t) const {
		return *this + (other - *this) * t;
	}
};


inline KosmPoint operator*(float scalar, const KosmPoint& point) {
	return point * scalar;
}


struct KosmSize {
	float	width;
	float	height;

	KosmSize() : width(0), height(0) {}
	KosmSize(float width, float height) : width(width), height(height) {}

	bool operator==(const KosmSize& other) const {
		return width == other.width && height == other.height;
	}

	bool operator!=(const KosmSize& other) const {
		return !(*this == other);
	}

	KosmSize operator*(float scalar) const {
		return KosmSize(width * scalar, height * scalar);
	}

	KosmSize operator/(float scalar) const {
		return KosmSize(width / scalar, height / scalar);
	}

	bool IsEmpty() const {
		return width <= 0 || height <= 0;
	}

	float Area() const {
		return width * height;
	}
};


struct KosmRect {
	float	x;
	float	y;
	float	width;
	float	height;

	KosmRect() : x(0), y(0), width(0), height(0) {}
	KosmRect(float x, float y, float width, float height)
		: x(x), y(y), width(width), height(height) {}
	KosmRect(const KosmPoint& origin, const KosmSize& size)
		: x(origin.x), y(origin.y), width(size.width), height(size.height) {}

	static KosmRect FromLTRB(float left, float top, float right, float bottom) {
		return KosmRect(left, top, right - left, bottom - top);
	}

	bool operator==(const KosmRect& other) const {
		return x == other.x && y == other.y
			&& width == other.width && height == other.height;
	}

	bool operator!=(const KosmRect& other) const {
		return !(*this == other);
	}

	float Left() const { return x; }
	float Top() const { return y; }
	float Right() const { return x + width; }
	float Bottom() const { return y + height; }

	KosmPoint Origin() const { return KosmPoint(x, y); }
	KosmSize Size() const { return KosmSize(width, height); }
	KosmPoint Center() const {
		return KosmPoint(x + width * 0.5f, y + height * 0.5f);
	}

	KosmPoint TopLeft() const { return KosmPoint(x, y); }
	KosmPoint TopRight() const { return KosmPoint(x + width, y); }
	KosmPoint BottomLeft() const { return KosmPoint(x, y + height); }
	KosmPoint BottomRight() const { return KosmPoint(x + width, y + height); }

	bool IsEmpty() const {
		return width <= 0 || height <= 0;
	}

	bool IsValid() const {
		return width > 0 && height > 0;
	}

	float Area() const {
		return width * height;
	}

	bool Contains(const KosmPoint& point) const {
		return point.x >= x && point.x < x + width
			&& point.y >= y && point.y < y + height;
	}

	bool Contains(const KosmRect& other) const {
		return other.x >= x && other.Right() <= Right()
			&& other.y >= y && other.Bottom() <= Bottom();
	}

	bool Intersects(const KosmRect& other) const {
		return x < other.Right() && Right() > other.x
			&& y < other.Bottom() && Bottom() > other.y;
	}

	KosmRect Intersection(const KosmRect& other) const {
		float l = fmaxf(x, other.x);
		float t = fmaxf(y, other.y);
		float r = fminf(Right(), other.Right());
		float b = fminf(Bottom(), other.Bottom());
		if (l >= r || t >= b)
			return KosmRect();
		return KosmRect(l, t, r - l, b - t);
	}

	KosmRect Union(const KosmRect& other) const {
		if (IsEmpty())
			return other;
		if (other.IsEmpty())
			return *this;
		float l = fminf(x, other.x);
		float t = fminf(y, other.y);
		float r = fmaxf(Right(), other.Right());
		float b = fmaxf(Bottom(), other.Bottom());
		return KosmRect(l, t, r - l, b - t);
	}

	KosmRect Inset(float dx, float dy) const {
		return KosmRect(x + dx, y + dy, width - dx * 2, height - dy * 2);
	}

	KosmRect Inset(float d) const {
		return Inset(d, d);
	}

	KosmRect Outset(float dx, float dy) const {
		return Inset(-dx, -dy);
	}

	KosmRect Outset(float d) const {
		return Inset(-d, -d);
	}

	KosmRect Offset(float dx, float dy) const {
		return KosmRect(x + dx, y + dy, width, height);
	}

	KosmRect Offset(const KosmPoint& delta) const {
		return Offset(delta.x, delta.y);
	}

	KosmRect Scale(float sx, float sy) const {
		return KosmRect(x * sx, y * sy, width * sx, height * sy);
	}

	KosmRect Scale(float s) const {
		return Scale(s, s);
	}

	KosmRect Rounded() const {
		return KosmRect(floorf(x), floorf(y),
			ceilf(width), ceilf(height));
	}

	KosmRect Normalized() const {
		KosmRect r = *this;
		if (r.width < 0) {
			r.x += r.width;
			r.width = -r.width;
		}
		if (r.height < 0) {
			r.y += r.height;
			r.height = -r.height;
		}
		return r;
	}
};


struct KosmMatrix {
	float	m[6];
	// Affine 2D matrix stored as:
	// | m[0] m[1] m[2] |   | a  b  tx |
	// | m[3] m[4] m[5] | = | c  d  ty |
	// |  0    0    1   |   | 0  0  1  |

	KosmMatrix() {
		m[0] = 1; m[1] = 0; m[2] = 0;
		m[3] = 0; m[4] = 1; m[5] = 0;
	}

	KosmMatrix(float a, float b, float c, float d, float tx, float ty) {
		m[0] = a;  m[1] = b;  m[2] = tx;
		m[3] = c;  m[4] = d;  m[5] = ty;
	}

	static KosmMatrix Identity() {
		return KosmMatrix();
	}

	static KosmMatrix Translate(float tx, float ty) {
		return KosmMatrix(1, 0, 0, 1, tx, ty);
	}

	static KosmMatrix Translate(const KosmPoint& t) {
		return Translate(t.x, t.y);
	}

	static KosmMatrix Scale(float sx, float sy) {
		return KosmMatrix(sx, 0, 0, sy, 0, 0);
	}

	static KosmMatrix Scale(float s) {
		return Scale(s, s);
	}

	static KosmMatrix Scale(float sx, float sy, const KosmPoint& center) {
		return Translate(center.x, center.y)
			.Multiply(Scale(sx, sy))
			.Multiply(Translate(-center.x, -center.y));
	}

	static KosmMatrix Rotate(float radians) {
		float c = cosf(radians);
		float s = sinf(radians);
		return KosmMatrix(c, -s, s, c, 0, 0);
	}

	static KosmMatrix Rotate(float radians, const KosmPoint& center) {
		return Translate(center.x, center.y)
			.Multiply(Rotate(radians))
			.Multiply(Translate(-center.x, -center.y));
	}

	static KosmMatrix Skew(float sx, float sy) {
		return KosmMatrix(1, tanf(sx), tanf(sy), 1, 0, 0);
	}

	float A() const { return m[0]; }
	float B() const { return m[1]; }
	float C() const { return m[3]; }
	float D() const { return m[4]; }
	float Tx() const { return m[2]; }
	float Ty() const { return m[5]; }

	bool IsIdentity() const {
		return m[0] == 1 && m[1] == 0 && m[2] == 0
			&& m[3] == 0 && m[4] == 1 && m[5] == 0;
	}

	float Determinant() const {
		return m[0] * m[4] - m[1] * m[3];
	}

	bool IsInvertible() const {
		return Determinant() != 0;
	}

	KosmMatrix Multiply(const KosmMatrix& other) const {
		return KosmMatrix(
			m[0] * other.m[0] + m[1] * other.m[3],
			m[0] * other.m[1] + m[1] * other.m[4],
			m[3] * other.m[0] + m[4] * other.m[3],
			m[3] * other.m[1] + m[4] * other.m[4],
			m[0] * other.m[2] + m[1] * other.m[5] + m[2],
			m[3] * other.m[2] + m[4] * other.m[5] + m[5]
		);
	}

	KosmMatrix Inverted() const {
		float det = Determinant();
		if (det == 0)
			return Identity();
		float invDet = 1.0f / det;
		return KosmMatrix(
			m[4] * invDet,
			-m[1] * invDet,
			-m[3] * invDet,
			m[0] * invDet,
			(m[3] * m[5] - m[4] * m[2]) * invDet,
			(m[1] * m[2] - m[0] * m[5]) * invDet
		);
	}

	KosmMatrix Translated(float tx, float ty) const {
		return Multiply(Translate(tx, ty));
	}

	KosmMatrix Scaled(float sx, float sy) const {
		return Multiply(Scale(sx, sy));
	}

	KosmMatrix Rotated(float radians) const {
		return Multiply(Rotate(radians));
	}

	KosmPoint TransformPoint(const KosmPoint& p) const {
		return KosmPoint(
			m[0] * p.x + m[1] * p.y + m[2],
			m[3] * p.x + m[4] * p.y + m[5]
		);
	}

	KosmPoint TransformVector(const KosmPoint& v) const {
		return KosmPoint(
			m[0] * v.x + m[1] * v.y,
			m[3] * v.x + m[4] * v.y
		);
	}

	KosmRect TransformRect(const KosmRect& r) const {
		KosmPoint p0 = TransformPoint(r.TopLeft());
		KosmPoint p1 = TransformPoint(r.TopRight());
		KosmPoint p2 = TransformPoint(r.BottomLeft());
		KosmPoint p3 = TransformPoint(r.BottomRight());

		float minX = fminf(fminf(p0.x, p1.x), fminf(p2.x, p3.x));
		float minY = fminf(fminf(p0.y, p1.y), fminf(p2.y, p3.y));
		float maxX = fmaxf(fmaxf(p0.x, p1.x), fmaxf(p2.x, p3.x));
		float maxY = fmaxf(fmaxf(p0.y, p1.y), fmaxf(p2.y, p3.y));

		return KosmRect(minX, minY, maxX - minX, maxY - minY);
	}
};

#endif /* _KOSM_GEOMETRY_H */
