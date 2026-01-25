#ifndef _KOSM_GEOMETRY_H
#define _KOSM_GEOMETRY_H

#include <math.h>

struct KosmPoint;
struct KosmSize;
struct KosmRect;
struct KosmInsets;
struct KosmMatrix;
struct KosmMatrix3x3;
struct KosmCornerRadii;
struct KosmRange;


// KosmPoint

struct KosmPoint {
	float	x;
	float	y;

	KosmPoint() : x(0), y(0) {}
	KosmPoint(float x, float y) : x(x), y(y) {}

	// Arithmetic
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

	// Vector operations
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

	KosmPoint Perpendicular() const {
		return KosmPoint(-y, x);
	}

	float DistanceTo(const KosmPoint& other) const {
		return (*this - other).Length();
	}

	float DistanceSquaredTo(const KosmPoint& other) const {
		return (*this - other).LengthSquared();
	}

	float AngleTo(const KosmPoint& other) const {
		return atan2f(other.y - y, other.x - x);
	}

	KosmPoint Lerp(const KosmPoint& other, float t) const {
		return *this + (other - *this) * t;
	}

	KosmPoint Rotated(float radians) const {
		float c = cosf(radians);
		float s = sinf(radians);
		return KosmPoint(x * c - y * s, x * s + y * c);
	}

	KosmPoint Rotated(float radians, const KosmPoint& pivot) const {
		return (*this - pivot).Rotated(radians) + pivot;
	}

	KosmPoint Clamped(const KosmPoint& min, const KosmPoint& max) const {
		return KosmPoint(
			fminf(fmaxf(x, min.x), max.x),
			fminf(fmaxf(y, min.y), max.y)
		);
	}

	KosmPoint Rounded() const {
		return KosmPoint(roundf(x), roundf(y));
	}

	KosmPoint Floor() const {
		return KosmPoint(floorf(x), floorf(y));
	}

	KosmPoint Ceil() const {
		return KosmPoint(ceilf(x), ceilf(y));
	}

	// Named constructors
	static KosmPoint Zero() { return KosmPoint(0, 0); }

	static KosmPoint FromAngle(float radians, float length = 1.0f) {
		return KosmPoint(cosf(radians) * length, sinf(radians) * length);
	}
};


inline KosmPoint operator*(float scalar, const KosmPoint& point) {
	return point * scalar;
}


// KosmSize

struct KosmSize {
	float	width;
	float	height;

	KosmSize() : width(0), height(0) {}
	KosmSize(float width, float height) : width(width), height(height) {}
	explicit KosmSize(float size) : width(size), height(size) {}

	bool operator==(const KosmSize& other) const {
		return width == other.width && height == other.height;
	}

	bool operator!=(const KosmSize& other) const {
		return !(*this == other);
	}

	KosmSize operator+(const KosmSize& other) const {
		return KosmSize(width + other.width, height + other.height);
	}

	KosmSize operator-(const KosmSize& other) const {
		return KosmSize(width - other.width, height - other.height);
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

	bool IsValid() const {
		return width > 0 && height > 0;
	}

	float Area() const {
		return width * height;
	}

	float AspectRatio() const {
		if (height == 0) return 0;
		return width / height;
	}

	KosmSize Clamped(const KosmSize& min, const KosmSize& max) const {
		return KosmSize(
			fminf(fmaxf(width, min.width), max.width),
			fminf(fmaxf(height, min.height), max.height)
		);
	}

	KosmSize Rounded() const {
		return KosmSize(roundf(width), roundf(height));
	}

	KosmSize Ceil() const {
		return KosmSize(ceilf(width), ceilf(height));
	}

	KosmSize Floor() const {
		return KosmSize(floorf(width), floorf(height));
	}

	// Fit/Fill operations for aspect ratio handling
	KosmSize AspectFit(const KosmSize& bounds) const {
		if (IsEmpty()) return KosmSize();
		float scale = fminf(bounds.width / width, bounds.height / height);
		return *this * scale;
	}

	KosmSize AspectFill(const KosmSize& bounds) const {
		if (IsEmpty()) return KosmSize();
		float scale = fmaxf(bounds.width / width, bounds.height / height);
		return *this * scale;
	}

	// Named constructors
	static KosmSize Zero() { return KosmSize(0, 0); }
};


inline KosmSize operator*(float scalar, const KosmSize& size) {
	return size * scalar;
}


// KosmInsets

struct KosmInsets {
	float	top;
	float	left;
	float	bottom;
	float	right;

	KosmInsets() : top(0), left(0), bottom(0), right(0) {}
	
	explicit KosmInsets(float all) 
		: top(all), left(all), bottom(all), right(all) {}
	
	KosmInsets(float vertical, float horizontal)
		: top(vertical), left(horizontal), bottom(vertical), right(horizontal) {}
	
	KosmInsets(float top, float left, float bottom, float right)
		: top(top), left(left), bottom(bottom), right(right) {}

	bool operator==(const KosmInsets& other) const {
		return top == other.top && left == other.left 
			&& bottom == other.bottom && right == other.right;
	}

	bool operator!=(const KosmInsets& other) const {
		return !(*this == other);
	}

	KosmInsets operator+(const KosmInsets& other) const {
		return KosmInsets(
			top + other.top,
			left + other.left,
			bottom + other.bottom,
			right + other.right
		);
	}

	KosmInsets operator-(const KosmInsets& other) const {
		return KosmInsets(
			top - other.top,
			left - other.left,
			bottom - other.bottom,
			right - other.right
		);
	}

	KosmInsets operator*(float scalar) const {
		return KosmInsets(
			top * scalar,
			left * scalar,
			bottom * scalar,
			right * scalar
		);
	}

	KosmInsets operator-() const {
		return KosmInsets(-top, -left, -bottom, -right);
	}

	float Horizontal() const { return left + right; }
	float Vertical() const { return top + bottom; }

	KosmSize Size() const { return KosmSize(Horizontal(), Vertical()); }

	bool IsZero() const {
		return top == 0 && left == 0 && bottom == 0 && right == 0;
	}

	bool IsUniform() const {
		return top == left && left == bottom && bottom == right;
	}

	// Named constructors
	static KosmInsets Zero() { return KosmInsets(); }
	
	static KosmInsets Horizontal(float h) { return KosmInsets(0, h, 0, h); }
	static KosmInsets Vertical(float v) { return KosmInsets(v, 0, v, 0); }
	static KosmInsets Top(float t) { return KosmInsets(t, 0, 0, 0); }
	static KosmInsets Left(float l) { return KosmInsets(0, l, 0, 0); }
	static KosmInsets Bottom(float b) { return KosmInsets(0, 0, b, 0); }
	static KosmInsets Right(float r) { return KosmInsets(0, 0, 0, r); }
};


// KosmRect

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

	static KosmRect FromCenter(const KosmPoint& center, const KosmSize& size) {
		return KosmRect(
			center.x - size.width * 0.5f,
			center.y - size.height * 0.5f,
			size.width,
			size.height
		);
	}

	static KosmRect FromPoints(const KosmPoint& p1, const KosmPoint& p2) {
		float minX = fminf(p1.x, p2.x);
		float minY = fminf(p1.y, p2.y);
		float maxX = fmaxf(p1.x, p2.x);
		float maxY = fmaxf(p1.y, p2.y);
		return KosmRect(minX, minY, maxX - minX, maxY - minY);
	}

	bool operator==(const KosmRect& other) const {
		return x == other.x && y == other.y
			&& width == other.width && height == other.height;
	}

	bool operator!=(const KosmRect& other) const {
		return !(*this == other);
	}

	// Edges
	float Left() const { return x; }
	float Top() const { return y; }
	float Right() const { return x + width; }
	float Bottom() const { return y + height; }

	// Parts
	KosmPoint Origin() const { return KosmPoint(x, y); }
	KosmSize Size() const { return KosmSize(width, height); }
	
	KosmPoint Center() const {
		return KosmPoint(x + width * 0.5f, y + height * 0.5f);
	}

	// Corners
	KosmPoint TopLeft() const { return KosmPoint(x, y); }
	KosmPoint TopRight() const { return KosmPoint(x + width, y); }
	KosmPoint BottomLeft() const { return KosmPoint(x, y + height); }
	KosmPoint BottomRight() const { return KosmPoint(x + width, y + height); }

	// Edge midpoints
	KosmPoint TopCenter() const { return KosmPoint(x + width * 0.5f, y); }
	KosmPoint BottomCenter() const { return KosmPoint(x + width * 0.5f, y + height); }
	KosmPoint LeftCenter() const { return KosmPoint(x, y + height * 0.5f); }
	KosmPoint RightCenter() const { return KosmPoint(x + width, y + height * 0.5f); }

	// Queries
	bool IsEmpty() const {
		return width <= 0 || height <= 0;
	}

	bool IsValid() const {
		return width > 0 && height > 0;
	}

	float Area() const {
		return width * height;
	}

	float AspectRatio() const {
		if (height == 0) return 0;
		return width / height;
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

	// Operations
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

	KosmRect Inset(const KosmInsets& insets) const {
		return KosmRect(
			x + insets.left,
			y + insets.top,
			width - insets.Horizontal(),
			height - insets.Vertical()
		);
	}

	KosmRect Outset(float dx, float dy) const {
		return Inset(-dx, -dy);
	}

	KosmRect Outset(float d) const {
		return Inset(-d, -d);
	}

	KosmRect Outset(const KosmInsets& insets) const {
		return Inset(-insets);
	}

	KosmRect Offset(float dx, float dy) const {
		return KosmRect(x + dx, y + dy, width, height);
	}

	KosmRect Offset(const KosmPoint& delta) const {
		return Offset(delta.x, delta.y);
	}

	KosmRect WithOrigin(const KosmPoint& origin) const {
		return KosmRect(origin.x, origin.y, width, height);
	}

	KosmRect WithSize(const KosmSize& size) const {
		return KosmRect(x, y, size.width, size.height);
	}

	KosmRect WithCenter(const KosmPoint& center) const {
		return KosmRect(
			center.x - width * 0.5f,
			center.y - height * 0.5f,
			width,
			height
		);
	}

	KosmRect Scale(float sx, float sy) const {
		return KosmRect(x * sx, y * sy, width * sx, height * sy);
	}

	KosmRect Scale(float s) const {
		return Scale(s, s);
	}

	KosmRect ScaleFromCenter(float sx, float sy) const {
		KosmPoint c = Center();
		return KosmRect(
			c.x - (width * sx) * 0.5f,
			c.y - (height * sy) * 0.5f,
			width * sx,
			height * sy
		);
	}

	KosmRect ScaleFromCenter(float s) const {
		return ScaleFromCenter(s, s);
	}

	KosmRect Rounded() const {
		return KosmRect(floorf(x), floorf(y), ceilf(width), ceilf(height));
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

	// Lerp between two rects
	KosmRect Lerp(const KosmRect& other, float t) const {
		return KosmRect(
			x + (other.x - x) * t,
			y + (other.y - y) * t,
			width + (other.width - width) * t,
			height + (other.height - height) * t
		);
	}

	// Named constructors
	static KosmRect Zero() { return KosmRect(); }
};


// KosmCornerRadii

struct KosmCornerRadii {
	float	topLeft;
	float	topRight;
	float	bottomLeft;
	float	bottomRight;

	KosmCornerRadii()
		: topLeft(0), topRight(0), bottomLeft(0), bottomRight(0) {}

	explicit KosmCornerRadii(float all)
		: topLeft(all), topRight(all), bottomLeft(all), bottomRight(all) {}

	KosmCornerRadii(float topLeft, float topRight, float bottomLeft, float bottomRight)
		: topLeft(topLeft), topRight(topRight),
		  bottomLeft(bottomLeft), bottomRight(bottomRight) {}

	bool operator==(const KosmCornerRadii& other) const {
		return topLeft == other.topLeft && topRight == other.topRight
			&& bottomLeft == other.bottomLeft && bottomRight == other.bottomRight;
	}

	bool operator!=(const KosmCornerRadii& other) const {
		return !(*this == other);
	}

	bool IsZero() const {
		return topLeft == 0 && topRight == 0
			&& bottomLeft == 0 && bottomRight == 0;
	}

	bool IsUniform() const {
		return topLeft == topRight && topRight == bottomLeft
			&& bottomLeft == bottomRight;
	}

	float Uniform() const {
		return IsUniform() ? topLeft : 0;
	}

	KosmCornerRadii Clamped(float maxRadius) const {
		return KosmCornerRadii(
			fminf(topLeft, maxRadius),
			fminf(topRight, maxRadius),
			fminf(bottomLeft, maxRadius),
			fminf(bottomRight, maxRadius)
		);
	}

	// Named constructors
	static KosmCornerRadii Zero() { return KosmCornerRadii(); }
	static KosmCornerRadii Top(float r) { return KosmCornerRadii(r, r, 0, 0); }
	static KosmCornerRadii Bottom(float r) { return KosmCornerRadii(0, 0, r, r); }
	static KosmCornerRadii Left(float r) { return KosmCornerRadii(r, 0, r, 0); }
	static KosmCornerRadii Right(float r) { return KosmCornerRadii(0, r, 0, r); }
};


// KosmRange

struct KosmRange {
	float	start;
	float	length;

	KosmRange() : start(0), length(0) {}
	KosmRange(float start, float length) : start(start), length(length) {}

	static KosmRange FromStartEnd(float start, float end) {
		return KosmRange(start, end - start);
	}

	bool operator==(const KosmRange& other) const {
		return start == other.start && length == other.length;
	}

	bool operator!=(const KosmRange& other) const {
		return !(*this == other);
	}

	float End() const { return start + length; }
	float Mid() const { return start + length * 0.5f; }

	bool IsEmpty() const { return length <= 0; }

	bool Contains(float value) const {
		return value >= start && value < End();
	}

	bool Contains(const KosmRange& other) const {
		return other.start >= start && other.End() <= End();
	}

	bool Intersects(const KosmRange& other) const {
		return start < other.End() && End() > other.start;
	}

	KosmRange Intersection(const KosmRange& other) const {
		float s = fmaxf(start, other.start);
		float e = fminf(End(), other.End());
		if (s >= e)
			return KosmRange();
		return KosmRange(s, e - s);
	}

	KosmRange Union(const KosmRange& other) const {
		if (IsEmpty()) return other;
		if (other.IsEmpty()) return *this;
		float s = fminf(start, other.start);
		float e = fmaxf(End(), other.End());
		return KosmRange(s, e - s);
	}

	float Clamp(float value) const {
		return fminf(fmaxf(value, start), End());
	}

	// Normalize value to 0-1 range
	float Normalize(float value) const {
		if (length == 0) return 0;
		return (value - start) / length;
	}

	// Denormalize from 0-1 to actual range
	float Denormalize(float normalized) const {
		return start + normalized * length;
	}

	KosmRange Lerp(const KosmRange& other, float t) const {
		return KosmRange(
			start + (other.start - start) * t,
			length + (other.length - length) * t
		);
	}

	static KosmRange Zero() { return KosmRange(); }
	static KosmRange Unit() { return KosmRange(0, 1); }
};


// KosmMatrix (Affine 2D - 6 elements)

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

	// Accessors
	float A() const { return m[0]; }
	float B() const { return m[1]; }
	float C() const { return m[3]; }
	float D() const { return m[4]; }
	float Tx() const { return m[2]; }
	float Ty() const { return m[5]; }

	// Named constructors
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

	static KosmMatrix RotateDegrees(float degrees) {
		return Rotate(degrees * (M_PI / 180.0f));
	}

	static KosmMatrix Rotate(float radians, const KosmPoint& center) {
		return Translate(center.x, center.y)
			.Multiply(Rotate(radians))
			.Multiply(Translate(-center.x, -center.y));
	}

	static KosmMatrix Skew(float sx, float sy) {
		return KosmMatrix(1, tanf(sx), tanf(sy), 1, 0, 0);
	}

	// Queries
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

	// Operations
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

	KosmMatrix operator*(const KosmMatrix& other) const {
		return Multiply(other);
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

	// Chained operations
	KosmMatrix Translated(float tx, float ty) const {
		return Multiply(Translate(tx, ty));
	}

	KosmMatrix Translated(const KosmPoint& t) const {
		return Translated(t.x, t.y);
	}

	KosmMatrix Scaled(float sx, float sy) const {
		return Multiply(Scale(sx, sy));
	}

	KosmMatrix Scaled(float s) const {
		return Scaled(s, s);
	}

	KosmMatrix Rotated(float radians) const {
		return Multiply(Rotate(radians));
	}

	KosmMatrix RotatedDegrees(float degrees) const {
		return Rotated(degrees * (M_PI / 180.0f));
	}

	// Transform operations
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

	KosmSize TransformSize(const KosmSize& s) const {
		// Note: this assumes no rotation, just scale
		return KosmSize(
			fabsf(m[0]) * s.width + fabsf(m[1]) * s.height,
			fabsf(m[3]) * s.width + fabsf(m[4]) * s.height
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


// KosmMatrix3x3 (Full 3x3 - compatible with ThorVG)

struct KosmMatrix3x3 {
	float	e11, e12, e13;
	float	e21, e22, e23;
	float	e31, e32, e33;

	KosmMatrix3x3() {
		e11 = 1; e12 = 0; e13 = 0;
		e21 = 0; e22 = 1; e23 = 0;
		e31 = 0; e32 = 0; e33 = 1;
	}

	KosmMatrix3x3(float e11, float e12, float e13,
	              float e21, float e22, float e23,
	              float e31, float e32, float e33)
		: e11(e11), e12(e12), e13(e13),
		  e21(e21), e22(e22), e23(e23),
		  e31(e31), e32(e32), e33(e33) {}

	// Conversion from affine matrix
	explicit KosmMatrix3x3(const KosmMatrix& affine)
		: e11(affine.A()), e12(affine.B()), e13(affine.Tx()),
		  e21(affine.C()), e22(affine.D()), e23(affine.Ty()),
		  e31(0), e32(0), e33(1) {}

	// Conversion to affine matrix (loses e31, e32 if non-zero)
	KosmMatrix ToAffine() const {
		return KosmMatrix(e11, e12, e21, e22, e13, e23);
	}

	// Named constructors
	static KosmMatrix3x3 Identity() {
		return KosmMatrix3x3();
	}

	static KosmMatrix3x3 Translate(float tx, float ty) {
		return KosmMatrix3x3(
			1, 0, tx,
			0, 1, ty,
			0, 0, 1
		);
	}

	static KosmMatrix3x3 Scale(float sx, float sy) {
		return KosmMatrix3x3(
			sx, 0, 0,
			0, sy, 0,
			0, 0, 1
		);
	}

	static KosmMatrix3x3 Rotate(float radians) {
		float c = cosf(radians);
		float s = sinf(radians);
		return KosmMatrix3x3(
			c, -s, 0,
			s, c, 0,
			0, 0, 1
		);
	}

	// Queries
	bool IsIdentity() const {
		return e11 == 1 && e12 == 0 && e13 == 0
			&& e21 == 0 && e22 == 1 && e23 == 0
			&& e31 == 0 && e32 == 0 && e33 == 1;
	}

	bool IsAffine() const {
		return e31 == 0 && e32 == 0 && e33 == 1;
	}

	float Determinant() const {
		return e11 * (e22 * e33 - e23 * e32)
		     - e12 * (e21 * e33 - e23 * e31)
		     + e13 * (e21 * e32 - e22 * e31);
	}

	// Operations
	KosmMatrix3x3 Multiply(const KosmMatrix3x3& other) const {
		return KosmMatrix3x3(
			e11 * other.e11 + e12 * other.e21 + e13 * other.e31,
			e11 * other.e12 + e12 * other.e22 + e13 * other.e32,
			e11 * other.e13 + e12 * other.e23 + e13 * other.e33,
			e21 * other.e11 + e22 * other.e21 + e23 * other.e31,
			e21 * other.e12 + e22 * other.e22 + e23 * other.e32,
			e21 * other.e13 + e22 * other.e23 + e23 * other.e33,
			e31 * other.e11 + e32 * other.e21 + e33 * other.e31,
			e31 * other.e12 + e32 * other.e22 + e33 * other.e32,
			e31 * other.e13 + e32 * other.e23 + e33 * other.e33
		);
	}

	KosmMatrix3x3 operator*(const KosmMatrix3x3& other) const {
		return Multiply(other);
	}

	// Transform point (assumes affine, ignores perspective row)
	KosmPoint TransformPoint(const KosmPoint& p) const {
		return KosmPoint(
			e11 * p.x + e12 * p.y + e13,
			e21 * p.x + e22 * p.y + e23
		);
	}
};

#endif /* _KOSM_GEOMETRY_H */
