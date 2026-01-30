/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <spektr/KosmFlexLayout.hpp>
#include <algorithm>

namespace KosmOS {
namespace Flex {

static constexpr uint32_t MAX_LINES = 64;
static constexpr uint32_t MAX_ITEMS = 256;
static constexpr int SHRINK_PRIORITY_LEVELS = 8;
static constexpr int SHRINK_PRIORITY_OFFSET = 4;

static float GetEffectiveCross(const FlexItem& item, float container_cross,
                               FlexDirection dir) {
	float cross = GetCross(item.measured, dir);
	float min_cross = GetCross(item.min_size, dir);
	float max_cross = GetCross(item.max_size, dir);
	return ClampSize(cross, min_cross, max_cross);
}

static float GetEffectiveMain(const FlexItem& item, float container_main,
                              FlexDirection dir) {
	return GetBasis(item, dir);
}

static void ApplyGrowShrink(
	FlexItem* items,
	const uint32_t* indices,
	uint32_t start,
	uint32_t count,
	float container_main,
	float container_cross,
	float gap,
	FlexDirection dir
) {
	if (count == 0) return;

	float total_basis = 0;
	float total_grow = 0;
	uint32_t visible_count = 0;

	for (uint32_t i = 0; i < count; ++i) {
		uint32_t idx = indices ? indices[start + i] : (start + i);
		FlexItem& item = items[idx];
		if (item.gone) continue;

		float base = GetEffectiveMain(item, container_main, dir);
		float margins = GetMainMargins(item.margin, dir);
		total_basis += base + margins;
		total_grow += item.grow;
		++visible_count;
	}

	float total_gap = (visible_count > 1) ? gap * (visible_count - 1) : 0;
	float remaining = container_main - total_basis - total_gap;

	float shrink_by_priority[SHRINK_PRIORITY_LEVELS] = {0};
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t idx = indices ? indices[start + i] : (start + i);
		FlexItem& item = items[idx];
		if (item.gone || item.shrink <= 0) continue;

		int prio_idx = item.shrink_priority + SHRINK_PRIORITY_OFFSET;
		if (prio_idx < 0) prio_idx = 0;
		if (prio_idx >= SHRINK_PRIORITY_LEVELS) prio_idx = SHRINK_PRIORITY_LEVELS - 1;

		float base = GetEffectiveMain(item, container_main, dir);
		shrink_by_priority[prio_idx] += item.shrink * base;
	}

	float shrink_taken_by_priority[SHRINK_PRIORITY_LEVELS] = {0};
	float deficit = (remaining < 0) ? -remaining : 0;

	for (int p = SHRINK_PRIORITY_LEVELS - 1; p >= 0 && deficit > 0; --p) {
		if (shrink_by_priority[p] <= 0) continue;
		float can_take = shrink_by_priority[p];
		float will_take = (deficit < can_take) ? deficit : can_take;
		shrink_taken_by_priority[p] = will_take;
		deficit -= will_take;
	}

	for (uint32_t i = 0; i < count; ++i) {
		uint32_t idx = indices ? indices[start + i] : (start + i);
		FlexItem& item = items[idx];

		if (item.gone) {
			item.frame = KosmRect();
			continue;
		}

		float base = GetEffectiveMain(item, container_main, dir);
		float effective_cross = GetEffectiveCross(item, container_cross, dir);
		float final_main;

		if (remaining > 0 && total_grow > 0) {
			final_main = base + remaining * (item.grow / total_grow);
		} else if (remaining < 0 && item.shrink > 0) {
			int prio_idx = item.shrink_priority + SHRINK_PRIORITY_OFFSET;
			if (prio_idx < 0) prio_idx = 0;
			if (prio_idx >= SHRINK_PRIORITY_LEVELS) prio_idx = SHRINK_PRIORITY_LEVELS - 1;

			if (shrink_by_priority[prio_idx] > 0) {
				float ratio = (item.shrink * base) / shrink_by_priority[prio_idx];
				float shrink_amount = shrink_taken_by_priority[prio_idx] * ratio;
				final_main = base - shrink_amount;
			} else {
				final_main = base;
			}
		} else {
			final_main = base;
		}

		float min_main = GetMain(item.min_size, dir);
		float max_main = GetMain(item.max_size, dir);
		final_main = ClampSize(final_main, min_main, max_main);
		if (final_main < 0) final_main = 0;

		float final_cross = effective_cross;
		if (item.aspect_ratio > 0) {
			if (IsHorizontal(dir)) {
				final_cross = final_main / item.aspect_ratio;
			} else {
				final_cross = final_main * item.aspect_ratio;
			}
			float min_cross = GetCross(item.min_size, dir);
			float max_cross = GetCross(item.max_size, dir);
			final_cross = ClampSize(final_cross, min_cross, max_cross);
		}

		item.frame = MakeFrame(0, 0, final_main, final_cross, dir);
	}
}

static void PositionMainAxis(
	FlexItem* items,
	const uint32_t* indices,
	uint32_t start,
	uint32_t count,
	float container_main,
	float gap,
	FlexAlign main_align,
	bool reverse,
	float padding_start,
	float padding_end,
	FlexDirection dir
) {
	if (count == 0) return;

	float total_size = 0;
	uint32_t visible_count = 0;

	for (uint32_t i = 0; i < count; ++i) {
		uint32_t idx = indices ? indices[start + i] : (start + i);
		FlexItem& item = items[idx];
		if (item.gone) continue;
		float main_size = IsHorizontal(dir) ? item.frame.width : item.frame.height;
		total_size += main_size + GetMainMargins(item.margin, dir);
		++visible_count;
	}

	float usable_main = container_main - padding_start - padding_end;
	float total_gap = (visible_count > 1) ? gap * (visible_count - 1) : 0;
	float free_space = usable_main - total_size - total_gap;
	if (free_space < 0) free_space = 0;

	float pos = padding_start;
	float between = gap;

	switch (main_align) {
		case FlexAlign::Start:
		case FlexAlign::Auto:
			pos = padding_start;
			break;
		case FlexAlign::End:
			pos = padding_start + free_space;
			break;
		case FlexAlign::Center:
			pos = padding_start + free_space * 0.5f;
			break;
		case FlexAlign::SpaceBetween:
			pos = padding_start;
			between = (visible_count > 1)
			          ? (free_space + total_gap) / (visible_count - 1) : 0;
			break;
		case FlexAlign::SpaceAround:
			between = (visible_count > 0)
			          ? (free_space + total_gap) / visible_count : 0;
			pos = padding_start + between * 0.5f;
			break;
		case FlexAlign::SpaceEvenly:
			between = (free_space + total_gap) / (visible_count + 1);
			pos = padding_start + between;
			break;
		default:
			break;
	}

	if (reverse) {
		pos = container_main - padding_end;
		for (uint32_t i = 0; i < count; ++i) {
			uint32_t idx = indices ? indices[start + i] : (start + i);
			FlexItem& item = items[idx];
			if (item.gone) continue;

			float main_size = IsHorizontal(dir) ? item.frame.width : item.frame.height;
			float margin_start = IsHorizontal(dir) ? item.margin.left : item.margin.top;
			float margin_end = IsHorizontal(dir) ? item.margin.right : item.margin.bottom;

			pos -= margin_end;
			pos -= main_size;

			if (IsHorizontal(dir))
				item.frame.x = pos;
			else
				item.frame.y = pos;

			pos -= margin_start;
			pos -= between;
		}
	} else {
		for (uint32_t i = 0; i < count; ++i) {
			uint32_t idx = indices ? indices[start + i] : (start + i);
			FlexItem& item = items[idx];
			if (item.gone) continue;

			float main_size = IsHorizontal(dir) ? item.frame.width : item.frame.height;
			float margin_start = IsHorizontal(dir) ? item.margin.left : item.margin.top;
			float margin_end = IsHorizontal(dir) ? item.margin.right : item.margin.bottom;

			pos += margin_start;

			if (IsHorizontal(dir))
				item.frame.x = pos;
			else
				item.frame.y = pos;

			pos += main_size + margin_end + between;
		}
	}
}

static void AlignCrossAxis(
	FlexItem* items,
	const uint32_t* indices,
	uint32_t start,
	uint32_t count,
	float line_cross,
	float line_cross_pos,
	FlexAlign cross_align,
	float max_baseline,
	FlexDirection dir
) {
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t idx = indices ? indices[start + i] : (start + i);
		FlexItem& item = items[idx];

		if (item.gone) continue;

		FlexAlign align = (item.align_self == FlexAlign::Auto)
		                  ? cross_align : item.align_self;

		float cross_size = IsHorizontal(dir) ? item.frame.height : item.frame.width;
		float margin_start = IsHorizontal(dir) ? item.margin.top : item.margin.left;
		float margin_end = IsHorizontal(dir) ? item.margin.bottom : item.margin.right;
		float available_cross = line_cross - margin_start - margin_end;

		float offset = margin_start;

		switch (align) {
			case FlexAlign::Start:
			case FlexAlign::Auto:
				break;
			case FlexAlign::End:
				offset = line_cross - margin_end - cross_size;
				break;
			case FlexAlign::Center:
				offset = margin_start + (available_cross - cross_size) * 0.5f;
				break;
			case FlexAlign::Stretch:
				cross_size = available_cross;
				if (IsHorizontal(dir))
					item.frame.height = cross_size;
				else
					item.frame.width = cross_size;
				break;
			case FlexAlign::Baseline:
				offset = margin_start + (max_baseline - item.baseline);
				break;
			default:
				break;
		}

		float min_cross = GetCross(item.min_size, dir);
		float max_cross = GetCross(item.max_size, dir);
		cross_size = ClampSize(cross_size, min_cross, max_cross);

		if (IsHorizontal(dir)) {
			item.frame.height = cross_size;
			item.frame.y = line_cross_pos + offset;
		} else {
			item.frame.width = cross_size;
			item.frame.x = line_cross_pos + offset;
		}
	}
}

LayoutResult Layout(
	FlexItem* items,
	uint32_t count,
	const KosmSize& container_size,
	const FlexLayoutParams& params
) {
	LayoutContext ctx = LayoutContext::Default(container_size);
	ctx.pixel_snap = params.pixel_snap;
	ctx.rtl = params.rtl;
	return Layout(items, count, ctx, params);
}

LayoutResult Layout(
	FlexItem* items,
	uint32_t count,
	const LayoutContext& context,
	const FlexLayoutParams& params
) {
	if (count == 0)
		return LayoutResult::Success(KosmSize());

	FlexDirection dir = params.direction;
	bool do_wrap = (params.wrap != FlexWrap::NoWrap);
	bool reverse = IsReverse(dir) != (params.rtl && IsHorizontal(dir));
	bool wrap_reverse = (params.wrap == FlexWrap::WrapReverse);

	KosmInsets padding = context.EffectivePadding(params.padding);

	float container_main = GetMain(context.container_size, dir);
	float container_cross = GetCross(context.container_size, dir);
	float padding_start = GetMainStart(padding, dir, params.rtl);
	float padding_end = GetMainEnd(padding, dir, params.rtl);
	float padding_cross_start = GetCrossStart(padding, dir);
	float padding_cross_end = GetCrossEnd(padding, dir);

	float usable_main = container_main - padding_start - padding_end;
	float usable_cross = container_cross - padding_cross_start - padding_cross_end;

	uint32_t sorted_indices[MAX_ITEMS];
	const uint32_t* indices = nullptr;

	if (params.use_order && count <= MAX_ITEMS) {
		GetSortedIndices(items, count, sorted_indices);
		indices = sorted_indices;
	}

	FlexLine lines[MAX_LINES];
	uint32_t line_count = 0;

	if (!do_wrap) {
		lines[0] = {0, count, 0, 0, 0, 0};
		line_count = 1;
	} else {
		uint32_t line_start = 0;
		float line_main = 0;
		uint32_t line_visible = 0;

		for (uint32_t i = 0; i < count; ++i) {
			uint32_t idx = indices ? indices[i] : i;
			if (items[idx].gone) continue;

			float item_main = GetBasis(items[idx], dir)
			                  + GetMainMargins(items[idx].margin, dir);
			float gap_add = (line_visible > 0) ? params.gap : 0;

			if (line_main + gap_add + item_main > usable_main && line_visible > 0) {
				lines[line_count++] = {line_start, i - line_start, line_main, 0, 0, 0};
				if (line_count >= MAX_LINES) break;

				line_start = i;
				line_main = item_main;
				line_visible = 1;
			} else {
				line_main += gap_add + item_main;
				++line_visible;
			}
		}

		if (line_count < MAX_LINES && line_start < count) {
			lines[line_count++] = {line_start, count - line_start, line_main, 0, 0, 0};
		}
	}

	for (uint32_t l = 0; l < line_count; ++l) {
		FlexLine& line = lines[l];
		float max_cross = 0;
		float max_baseline = 0;

		for (uint32_t i = 0; i < line.count; ++i) {
			uint32_t idx = indices ? indices[line.start_index + i]
			                       : (line.start_index + i);
			FlexItem& item = items[idx];
			if (item.gone) continue;

			float item_cross = GetEffectiveCross(item, container_cross, dir)
			                   + GetCrossMargins(item.margin, dir);
			if (item_cross > max_cross)
				max_cross = item_cross;
			if (item.baseline > max_baseline)
				max_baseline = item.baseline;
		}

		line.cross_size = max_cross;
		line.max_baseline = max_baseline;
	}

	float total_cross = 0;
	for (uint32_t l = 0; l < line_count; ++l) {
		total_cross += lines[l].cross_size;
		if (l > 0) total_cross += params.cross_gap;
	}

	float cross_free = usable_cross - total_cross;
	if (cross_free < 0) cross_free = 0;

	float cross_pos = padding_cross_start;
	float cross_between = params.cross_gap;

	switch (params.lines_align) {
		case FlexAlign::Start:
		case FlexAlign::Auto:
			cross_pos = padding_cross_start;
			break;
		case FlexAlign::End:
			cross_pos = padding_cross_start + cross_free;
			break;
		case FlexAlign::Center:
			cross_pos = padding_cross_start + cross_free * 0.5f;
			break;
		case FlexAlign::SpaceBetween:
			cross_pos = padding_cross_start;
			cross_between = (line_count > 1)
			                ? (cross_free + (line_count - 1) * params.cross_gap)
			                  / (line_count - 1)
			                : 0;
			break;
		case FlexAlign::SpaceAround:
			cross_between = (line_count > 0)
			                ? (cross_free + (line_count - 1) * params.cross_gap)
			                  / line_count
			                : 0;
			cross_pos = padding_cross_start + cross_between * 0.5f;
			break;
		case FlexAlign::SpaceEvenly:
			cross_between = (cross_free + (line_count - 1) * params.cross_gap)
			                / (line_count + 1);
			cross_pos = padding_cross_start + cross_between;
			break;
		case FlexAlign::Stretch:
			cross_pos = padding_cross_start;
			if (line_count > 0) {
				float extra = cross_free / line_count;
				for (uint32_t l = 0; l < line_count; ++l)
					lines[l].cross_size += extra;
			}
			break;
		default:
			break;
	}

	if (wrap_reverse) {
		cross_pos = container_cross - padding_cross_end;
		for (uint32_t l = 0; l < line_count; ++l) {
			cross_pos -= lines[l].cross_size;
			lines[l].cross_position = cross_pos;
			cross_pos -= cross_between;
		}
	} else {
		for (uint32_t l = 0; l < line_count; ++l) {
			lines[l].cross_position = cross_pos;
			cross_pos += lines[l].cross_size + cross_between;
		}
	}

	for (uint32_t l = 0; l < line_count; ++l) {
		FlexLine& line = lines[l];

		ApplyGrowShrink(items, indices, line.start_index, line.count,
		                usable_main, usable_cross, params.gap, dir);

		PositionMainAxis(items, indices, line.start_index, line.count,
		                 container_main, params.gap, params.main_align, reverse,
		                 padding_start, padding_end, dir);

		AlignCrossAxis(items, indices, line.start_index, line.count,
		               line.cross_size, line.cross_position, params.cross_align,
		               line.max_baseline, dir);
	}

	KosmSize content_size;
	for (uint32_t i = 0; i < count; ++i) {
		if (items[i].gone) continue;
		float x_end = items[i].frame.x + items[i].frame.width;
		float y_end = items[i].frame.y + items[i].frame.height;
		if (x_end > content_size.width) content_size.width = x_end;
		if (y_end > content_size.height) content_size.height = y_end;
	}

	if (context.pixel_snap) {
		for (uint32_t i = 0; i < count; ++i) {
			if (items[i].gone) continue;
			items[i].frame = SnapRectToPixels(items[i].frame);
		}
	}

	for (uint32_t i = 0; i < count; ++i)
		items[i].ClearDirty();

	return LayoutResult::Success(content_size, line_count);
}

void GetSortedIndices(
	const FlexItem* items,
	uint32_t count,
	uint32_t* out_indices
) {
	for (uint32_t i = 0; i < count; ++i)
		out_indices[i] = i;

	for (uint32_t i = 1; i < count; ++i) {
		uint32_t key = out_indices[i];
		int16_t key_order = items[key].order;

		int32_t j = static_cast<int32_t>(i) - 1;
		while (j >= 0 && items[out_indices[j]].order > key_order) {
			out_indices[j + 1] = out_indices[j];
			--j;
		}
		out_indices[j + 1] = key;
	}
}

uint32_t ComputeItemsHash(
	const FlexItem* items,
	uint32_t count,
	const FlexLayoutParams& params
) {
	uint32_t hash = 0;

	hash = HashCombine(hash, static_cast<uint32_t>(params.direction));
	hash = HashCombine(hash, static_cast<uint32_t>(params.wrap));
	hash = HashCombine(hash, static_cast<uint32_t>(params.main_align));
	hash = HashCombine(hash, static_cast<uint32_t>(params.cross_align));
	hash = HashCombine(hash, static_cast<uint32_t>(params.lines_align));
	hash = HashCombine(hash, HashFloat(params.gap));
	hash = HashCombine(hash, HashFloat(params.cross_gap));
	hash = HashCombine(hash, HashInsets(params.padding));
	hash = HashCombine(hash, params.rtl ? 1u : 0u);

	for (uint32_t i = 0; i < count; ++i) {
		const FlexItem& item = items[i];

		hash = HashCombine(hash, HashSize(item.measured));
		hash = HashCombine(hash, HashFloat(item.basis));
		hash = HashCombine(hash, HashFloat(item.grow));
		hash = HashCombine(hash, HashFloat(item.shrink));
		hash = HashCombine(hash, static_cast<uint32_t>(item.shrink_priority));
		hash = HashCombine(hash, HashInsets(item.margin));
		hash = HashCombine(hash, HashSize(item.min_size));
		hash = HashCombine(hash, HashSize(item.max_size));
		hash = HashCombine(hash, HashFloat(item.aspect_ratio));
		hash = HashCombine(hash, HashFloat(item.baseline));
		hash = HashCombine(hash, static_cast<uint32_t>(item.align_self));
		hash = HashCombine(hash, static_cast<uint32_t>(item.order));
		hash = HashCombine(hash, item.gone ? 1u : 0u);
	}

	return hash;
}

bool AnyDirty(const FlexItem* items, uint32_t count) {
	for (uint32_t i = 0; i < count; ++i) {
		if (items[i].NeedsLayout()) return true;
	}
	return false;
}

void ClearAllDirty(FlexItem* items, uint32_t count) {
	for (uint32_t i = 0; i < count; ++i)
		items[i].ClearDirty();
}

LayoutResult LayoutCached(
	FlexItem* items,
	uint32_t count,
	const KosmSize& container_size,
	const FlexLayoutParams& params,
	LayoutCache& cache
) {
	uint32_t hash = ComputeItemsHash(items, count, params);

	if (cache.IsValid(container_size, count, hash) && !AnyDirty(items, count)) {
		KosmSize content_size;
		for (uint32_t i = 0; i < count; ++i) {
			if (items[i].gone) continue;
			float x_end = items[i].frame.x + items[i].frame.width;
			float y_end = items[i].frame.y + items[i].frame.height;
			if (x_end > content_size.width) content_size.width = x_end;
			if (y_end > content_size.height) content_size.height = y_end;
		}
		return LayoutResult::Success(content_size);
	}

	LayoutResult result = Layout(items, count, container_size, params);
	cache.Update(container_size, count, hash);

	return result;
}

} // namespace Flex
} // namespace KosmOS
