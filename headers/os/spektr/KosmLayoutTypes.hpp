/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_LAYOUT_TYPES_HPP
#define _KOSM_LAYOUT_TYPES_HPP

#include <render/KosmGeometry.hpp>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

namespace KosmOS {

enum class FlexDirection : uint8_t {
	Row,
	RowReverse,
	Column,
	ColumnReverse
};

enum class FlexAlign : uint8_t {
	Auto,
	Start,
	End,
	Center,
	SpaceBetween,
	SpaceAround,
	SpaceEvenly,
	Stretch,
	Baseline
};

enum class FlexWrap : uint8_t {
	NoWrap,
	Wrap,
	WrapReverse
};

enum class TrackUnit : uint8_t {
	Px,
	Fr,
	Auto,
	MinMax
};

struct TrackSize {
	float		value;
	float		min_value;
	float		max_value;
	TrackUnit	unit;

	static TrackSize Pixels(float px) {
		return {px, 0, 0, TrackUnit::Px};
	}

	static TrackSize Fraction(float fr) {
		return {fr, 0, 0, TrackUnit::Fr};
	}

	static TrackSize AutoSize() {
		return {0, 0, 0, TrackUnit::Auto};
	}

	static TrackSize MinMax(float min_px, float max_fr) {
		return {max_fr, min_px, 0, TrackUnit::MinMax};
	}
};

enum class AnchorEdge : uint8_t {
	None    = 0,
	Left    = 1 << 0,
	Right   = 1 << 1,
	Top     = 1 << 2,
	Bottom  = 1 << 3,
	CenterX = 1 << 4,
	CenterY = 1 << 5,

	Fill = Left | Right | Top | Bottom,
	FillHorizontal = Left | Right,
	FillVertical = Top | Bottom,
	Center = CenterX | CenterY
};

inline AnchorEdge operator|(AnchorEdge a, AnchorEdge b) {
	return static_cast<AnchorEdge>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool HasAnchor(AnchorEdge flags, AnchorEdge check) {
	return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(check)) != 0;
}

enum class DirtyFlag : uint8_t {
	Clean       = 0,
	Size        = 1 << 0,
	Position    = 1 << 1,
	Children    = 1 << 2,
	Constraints = 1 << 3,
	All         = 0xFF
};

inline DirtyFlag operator|(DirtyFlag a, DirtyFlag b) {
	return static_cast<DirtyFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool IsDirty(DirtyFlag flags, DirtyFlag check) {
	return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(check)) != 0;
}

struct LayoutCache {
	KosmSize	container_size;
	uint32_t	item_count;
	uint32_t	hash;
	bool		valid;

	void Invalidate() { valid = false; }

	bool IsValid(const KosmSize& size, uint32_t count, uint32_t new_hash) const {
		return valid &&
		       container_size == size &&
		       item_count == count &&
		       hash == new_hash;
	}

	void Update(const KosmSize& size, uint32_t count, uint32_t new_hash) {
		container_size = size;
		item_count = count;
		hash = new_hash;
		valid = true;
	}
};

struct LayoutResult {
	KosmSize	content_size;
	bool		success;
	uint32_t	line_count;
	uint32_t	unsatisfied_count;

	static LayoutResult Success(const KosmSize& size, uint32_t lines = 0) {
		return {size, true, lines, 0};
	}

	static LayoutResult Failed(uint32_t unsatisfied = 0) {
		return {KosmSize(), false, 0, unsatisfied};
	}
};

struct LayoutContext {
	KosmSize	container_size;
	KosmInsets	safe_area;
	bool		respect_safe_area;
	bool		rtl;
	bool		pixel_snap;

	static LayoutContext Default(const KosmSize& size) {
		LayoutContext ctx = {};
		ctx.container_size = size;
		ctx.pixel_snap = true;
		return ctx;
	}

	KosmInsets EffectivePadding(const KosmInsets& padding) const {
		if (!respect_safe_area)
			return padding;
		return KosmInsets(
			fmaxf(padding.top, safe_area.top),
			fmaxf(padding.left, safe_area.left),
			fmaxf(padding.bottom, safe_area.bottom),
			fmaxf(padding.right, safe_area.right)
		);
	}
};

inline uint32_t HashFloat(float f) {
	union { float f; uint32_t u; } conv;
	conv.f = f;
	return conv.u;
}

inline uint32_t HashCombine(uint32_t seed, uint32_t value) {
	return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

inline uint32_t HashSize(const KosmSize& s) {
	return HashCombine(HashFloat(s.width), HashFloat(s.height));
}

inline uint32_t HashInsets(const KosmInsets& i) {
	uint32_t h = HashFloat(i.top);
	h = HashCombine(h, HashFloat(i.left));
	h = HashCombine(h, HashFloat(i.bottom));
	h = HashCombine(h, HashFloat(i.right));
	return h;
}

inline KosmRect SnapRectToPixels(const KosmRect& r) {
	float x2 = roundf(r.x + r.width);
	float y2 = roundf(r.y + r.height);
	float x = roundf(r.x);
	float y = roundf(r.y);
	return KosmRect(x, y, x2 - x, y2 - y);
}

template<typename T, size_t StackSize = 32>
struct SmallBuffer {
	T		stack_data[StackSize];
	T*		data;
	size_t	count;
	size_t	capacity;

	SmallBuffer() : data(stack_data), count(0), capacity(StackSize) {}

	~SmallBuffer() {
		if (data != stack_data)
			free(data);
	}

	void Clear() { count = 0; }

	void Push(const T& item) {
		if (count >= capacity)
			Grow();
		data[count++] = item;
	}

	T& operator[](size_t i) { return data[i]; }
	const T& operator[](size_t i) const { return data[i]; }

	T* begin() { return data; }
	T* end() { return data + count; }
	const T* begin() const { return data; }
	const T* end() const { return data + count; }

private:
	void Grow() {
		size_t new_capacity = capacity * 2;
		T* new_data = static_cast<T*>(malloc(new_capacity * sizeof(T)));
		memcpy(new_data, data, count * sizeof(T));
		if (data != stack_data)
			free(data);
		data = new_data;
		capacity = new_capacity;
	}

	SmallBuffer(const SmallBuffer&) = delete;
	SmallBuffer& operator=(const SmallBuffer&) = delete;
};

} // namespace KosmOS

#endif /* _KOSM_LAYOUT_TYPES_HPP */
