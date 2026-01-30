/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_FLEX_LAYOUT_HPP
#define _KOSM_FLEX_LAYOUT_HPP

#include <spektr/KosmLayoutTypes.hpp>

namespace KosmOS {

struct FlexItem {
	KosmSize	measured;
	KosmInsets	margin;
	KosmSize	min_size;
	KosmSize	max_size;

	float		grow;
	float		shrink;
	float		basis;
	float		baseline;
	float		aspect_ratio;

	FlexAlign	align_self;
	int8_t		shrink_priority;
	int16_t		order;
	bool		gone;
	DirtyFlag	dirty;

	KosmRect	frame;

	void*		user_data;

	void MarkDirty(DirtyFlag flag = DirtyFlag::All) {
		dirty = dirty | flag;
	}

	void ClearDirty() {
		dirty = DirtyFlag::Clean;
	}

	bool NeedsLayout() const {
		return dirty != DirtyFlag::Clean;
	}

	static FlexItem Default() {
		FlexItem item = {};
		item.basis = -1;
		item.shrink = 1;
		item.align_self = FlexAlign::Auto;
		item.dirty = DirtyFlag::All;
		return item;
	}
};

struct FlexLayoutParams {
	FlexDirection	direction;
	FlexAlign		main_align;
	FlexAlign		cross_align;
	FlexAlign		lines_align;
	FlexWrap		wrap;

	KosmInsets		padding;
	float			gap;
	float			cross_gap;

	bool			pixel_snap;
	bool			use_order;
	bool			rtl;

	static FlexLayoutParams Default() {
		FlexLayoutParams params = {};
		params.direction = FlexDirection::Row;
		params.main_align = FlexAlign::Start;
		params.cross_align = FlexAlign::Stretch;
		params.lines_align = FlexAlign::Start;
		params.wrap = FlexWrap::NoWrap;
		params.pixel_snap = true;
		return params;
	}
};

struct FlexLine {
	uint32_t	start_index;
	uint32_t	count;
	float		main_size;
	float		cross_size;
	float		cross_position;
	float		max_baseline;
};

namespace Flex {

inline bool IsHorizontal(FlexDirection dir) {
	return dir == FlexDirection::Row || dir == FlexDirection::RowReverse;
}

inline bool IsReverse(FlexDirection dir) {
	return dir == FlexDirection::RowReverse || dir == FlexDirection::ColumnReverse;
}

inline float GetMain(const KosmSize& size, FlexDirection dir) {
	return IsHorizontal(dir) ? size.width : size.height;
}

inline float GetCross(const KosmSize& size, FlexDirection dir) {
	return IsHorizontal(dir) ? size.height : size.width;
}

inline float GetMainStart(const KosmInsets& insets, FlexDirection dir, bool rtl) {
	if (IsHorizontal(dir)) {
		bool reverse = IsReverse(dir) != rtl;
		return reverse ? insets.right : insets.left;
	}
	return IsReverse(dir) ? insets.bottom : insets.top;
}

inline float GetMainEnd(const KosmInsets& insets, FlexDirection dir, bool rtl) {
	if (IsHorizontal(dir)) {
		bool reverse = IsReverse(dir) != rtl;
		return reverse ? insets.left : insets.right;
	}
	return IsReverse(dir) ? insets.top : insets.bottom;
}

inline float GetCrossStart(const KosmInsets& insets, FlexDirection dir) {
	return IsHorizontal(dir) ? insets.top : insets.left;
}

inline float GetCrossEnd(const KosmInsets& insets, FlexDirection dir) {
	return IsHorizontal(dir) ? insets.bottom : insets.right;
}

inline float GetMainMargins(const KosmInsets& margin, FlexDirection dir) {
	if (IsHorizontal(dir))
		return margin.left + margin.right;
	return margin.top + margin.bottom;
}

inline float GetCrossMargins(const KosmInsets& margin, FlexDirection dir) {
	if (IsHorizontal(dir))
		return margin.top + margin.bottom;
	return margin.left + margin.right;
}

inline float GetBasis(const FlexItem& item, FlexDirection dir) {
	if (item.basis >= 0)
		return item.basis;
	return GetMain(item.measured, dir);
}

inline KosmRect MakeFrame(float main_pos, float cross_pos,
                          float main_size, float cross_size,
                          FlexDirection dir) {
	if (IsHorizontal(dir))
		return KosmRect(main_pos, cross_pos, main_size, cross_size);
	return KosmRect(cross_pos, main_pos, cross_size, main_size);
}

inline float ClampSize(float size, float min_size, float max_size) {
	if (min_size > 0 && size < min_size) size = min_size;
	if (max_size > 0 && size > max_size) size = max_size;
	return size;
}

LayoutResult Layout(
	FlexItem* items,
	uint32_t count,
	const KosmSize& container_size,
	const FlexLayoutParams& params
);

LayoutResult Layout(
	FlexItem* items,
	uint32_t count,
	const LayoutContext& context,
	const FlexLayoutParams& params
);

LayoutResult LayoutCached(
	FlexItem* items,
	uint32_t count,
	const KosmSize& container_size,
	const FlexLayoutParams& params,
	LayoutCache& cache
);

void GetSortedIndices(
	const FlexItem* items,
	uint32_t count,
	uint32_t* out_indices
);

uint32_t ComputeItemsHash(
	const FlexItem* items,
	uint32_t count,
	const FlexLayoutParams& params
);

bool AnyDirty(const FlexItem* items, uint32_t count);
void ClearAllDirty(FlexItem* items, uint32_t count);

} // namespace Flex
} // namespace KosmOS

#endif /* _KOSM_FLEX_LAYOUT_HPP */
