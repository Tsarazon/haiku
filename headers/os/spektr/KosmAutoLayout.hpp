/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_AUTO_LAYOUT_HPP
#define _KOSM_AUTO_LAYOUT_HPP

#include <spektr/KosmLayoutTypes.hpp>

namespace KosmOS {
namespace AutoLayout {

static constexpr float PriorityRequired = 1001.0f;
static constexpr float PriorityDefaultHigh = 750.0f;
static constexpr float PriorityDefaultMedium = 500.0f;
static constexpr float PriorityDefaultLow = 250.0f;
static constexpr float PriorityFittingSizeLevel = 50.0f;

static constexpr uint32_t MAX_VIEWS = 64;
static constexpr uint32_t MAX_CONSTRAINTS = 256;

using ViewId = int16_t;
static constexpr ViewId SuperviewId = 0;

enum class Attribute : uint8_t {
	Left,
	Right,
	Top,
	Bottom,
	Leading,
	Trailing,
	Width,
	Height,
	CenterX,
	CenterY,
	NotAnAttribute
};

enum class Relation : uint8_t {
	Equal,
	LessOrEqual,
	GreaterOrEqual
};

struct Constraint {
	ViewId		view1;
	Attribute	attr1;
	Relation	relation;
	ViewId		view2;
	Attribute	attr2;
	float		multiplier;
	float		constant;
	float		priority;
	bool		active;

	static Constraint Make(
		ViewId v1, Attribute a1,
		Relation rel,
		ViewId v2, Attribute a2,
		float mult = 1.0f, float constant = 0.0f,
		float priority = PriorityRequired
	) {
		return {v1, a1, rel, v2, a2, mult, constant, priority, true};
	}

	static Constraint Constant(
		ViewId v, Attribute a,
		Relation rel,
		float value,
		float priority = PriorityRequired
	) {
		return {v, a, rel, -1, Attribute::NotAnAttribute, 0, value, priority, true};
	}

	static Constraint Size(ViewId v, Attribute sizeAttr, float value,
	                       float priority = PriorityRequired) {
		return Constant(v, sizeAttr, Relation::Equal, value, priority);
	}

	static Constraint Pin(ViewId v, Attribute edge, float inset = 0,
	                      float priority = PriorityRequired) {
		float constant = inset;
		if (edge == Attribute::Right || edge == Attribute::Trailing
		    || edge == Attribute::Bottom)
			constant = -inset;
		return Make(v, edge, Relation::Equal, SuperviewId, edge, 1.0f, constant,
		            priority);
	}

	static Constraint CenterIn(ViewId v, Attribute centerAttr, float offset = 0,
	                           float priority = PriorityRequired) {
		return Make(v, centerAttr, Relation::Equal, SuperviewId, centerAttr, 1.0f,
		            offset, priority);
	}

	static Constraint HorizontalSpacing(ViewId v1, ViewId v2, float spacing,
	                                    float priority = PriorityRequired) {
		return Make(v2, Attribute::Left, Relation::Equal, v1, Attribute::Right,
		            1.0f, spacing, priority);
	}

	static Constraint VerticalSpacing(ViewId v1, ViewId v2, float spacing,
	                                  float priority = PriorityRequired) {
		return Make(v2, Attribute::Top, Relation::Equal, v1, Attribute::Bottom,
		            1.0f, spacing, priority);
	}

	static Constraint Align(ViewId v1, ViewId v2, Attribute edge, float offset = 0,
	                        float priority = PriorityRequired) {
		return Make(v1, edge, Relation::Equal, v2, edge, 1.0f, offset, priority);
	}

	static Constraint EqualSize(ViewId v1, ViewId v2, Attribute sizeAttr,
	                            float priority = PriorityRequired) {
		return Make(v1, sizeAttr, Relation::Equal, v2, sizeAttr, 1.0f, 0, priority);
	}

	static Constraint AspectRatio(ViewId v, float ratio,
	                              float priority = PriorityRequired) {
		return Make(v, Attribute::Width, Relation::Equal, v, Attribute::Height,
		            ratio, 0, priority);
	}
};

struct View {
	KosmSize	intrinsic_size;

	float		hugging_h;
	float		hugging_v;
	float		compression_h;
	float		compression_v;

	bool		gone;

	KosmRect	frame;

	void*		user_data;

	static View Default() {
		View v = {};
		v.intrinsic_size = KosmSize(-1, -1);
		v.hugging_h = PriorityDefaultMedium;
		v.hugging_v = PriorityDefaultMedium;
		v.compression_h = PriorityDefaultHigh;
		v.compression_v = PriorityDefaultHigh;
		return v;
	}
};

struct AutoLayoutParams {
	bool	pixel_snap;
	bool	rtl;

	static AutoLayoutParams Default() {
		AutoLayoutParams params = {};
		params.pixel_snap = true;
		return params;
	}
};

LayoutResult Layout(
	View* views,
	uint32_t view_count,
	const Constraint* constraints,
	uint32_t constraint_count,
	const KosmSize& container_size,
	const AutoLayoutParams& params
);

LayoutResult Layout(
	View* views,
	uint32_t view_count,
	const Constraint* constraints,
	uint32_t constraint_count,
	const LayoutContext& context,
	const AutoLayoutParams& params
);

template<size_t N>
LayoutResult Layout(
	View* views,
	uint32_t view_count,
	const Constraint (&constraints)[N],
	const KosmSize& container_size,
	const AutoLayoutParams& params
) {
	return Layout(views, view_count, constraints, N, container_size, params);
}

} // namespace AutoLayout
} // namespace KosmOS

#endif /* _KOSM_AUTO_LAYOUT_HPP */
