/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <spektr/KosmGridLayout.hpp>

namespace KosmOS {
namespace Grid {

void ResolveTracks(
	const TrackSize* tracks,
	uint8_t count,
	float available,
	float gap,
	const float* content_sizes,
	float* out_sizes,
	float* out_positions
) {
	if (count == 0) return;

	float total_gap = (count > 1) ? gap * (count - 1) : 0;
	float remaining = available - total_gap;

	float total_fr = 0;
	float fixed_used = 0;

	for (uint8_t i = 0; i < count; ++i) {
		const TrackSize& track = tracks[i];

		switch (track.unit) {
			case TrackUnit::Px:
				out_sizes[i] = track.value;
				fixed_used += track.value;
				break;

			case TrackUnit::Auto:
				out_sizes[i] = (content_sizes != nullptr) ? content_sizes[i] : 0;
				fixed_used += out_sizes[i];
				break;

			case TrackUnit::Fr:
				total_fr += track.value;
				out_sizes[i] = 0;
				break;

			case TrackUnit::MinMax:
				out_sizes[i] = track.min_value;
				fixed_used += track.min_value;
				total_fr += track.value;
				break;
		}
	}

	float fr_space = remaining - fixed_used;
	if (fr_space < 0) fr_space = 0;

	float fr_unit = (total_fr > 0) ? fr_space / total_fr : 0;

	for (uint8_t i = 0; i < count; ++i) {
		const TrackSize& track = tracks[i];

		if (track.unit == TrackUnit::Fr) {
			out_sizes[i] = track.value * fr_unit;
		} else if (track.unit == TrackUnit::MinMax) {
			float extra = track.value * fr_unit;
			out_sizes[i] = track.min_value + extra;
			if (track.max_value > 0 && out_sizes[i] > track.max_value)
				out_sizes[i] = track.max_value;
		}
	}

	float pos = 0;
	for (uint8_t i = 0; i < count; ++i) {
		out_positions[i] = pos;
		pos += out_sizes[i] + gap;
	}
}

bool AutoPlace(
	GridMatrix& matrix,
	uint8_t row_span,
	uint8_t col_span,
	uint8_t* out_row,
	uint8_t* out_col,
	bool add_rows_if_needed
) {
	for (uint8_t r = 0; r <= matrix.rows - row_span; ++r) {
		for (uint8_t c = 0; c <= matrix.cols - col_span; ++c) {
			if (matrix.CanPlace(r, c, row_span, col_span)) {
				*out_row = r;
				*out_col = c;
				return true;
			}
		}
	}

	if (add_rows_if_needed) {
		while (matrix.rows < GRID_MAX_TRACKS) {
			matrix.AddRow();

			uint8_t r = matrix.rows - row_span;
			for (uint8_t c = 0; c <= matrix.cols - col_span; ++c) {
				if (matrix.CanPlace(r, c, row_span, col_span)) {
					*out_row = r;
					*out_col = c;
					return true;
				}
			}
		}
	}

	return false;
}

static void CalcAutoTrackSizes(
	GridItem* items,
	uint32_t count,
	uint8_t col_count,
	uint8_t row_count,
	float* col_content_sizes,
	float* row_content_sizes
) {
	for (uint8_t i = 0; i < col_count; ++i) col_content_sizes[i] = 0;
	for (uint8_t i = 0; i < row_count; ++i) row_content_sizes[i] = 0;

	for (uint32_t i = 0; i < count; ++i) {
		GridItem& item = items[i];
		if (item.gone) continue;

		if (item.col_span == 1 && item.col < col_count) {
			float w = item.measured.width + item.margin.left + item.margin.right;
			if (w > col_content_sizes[item.col])
				col_content_sizes[item.col] = w;
		}

		if (item.row_span == 1 && item.row < row_count) {
			float h = item.measured.height + item.margin.top + item.margin.bottom;
			if (h > row_content_sizes[item.row])
				row_content_sizes[item.row] = h;
		}
	}
}

LayoutResult Layout(
	GridItem* items,
	uint32_t count,
	const KosmSize& container_size,
	GridLayoutParams& params
) {
	LayoutContext ctx = LayoutContext::Default(container_size);
	ctx.pixel_snap = params.pixel_snap;
	return Layout(items, count, ctx, params);
}

LayoutResult Layout(
	GridItem* items,
	uint32_t count,
	const LayoutContext& context,
	GridLayoutParams& params
) {
	if (count == 0)
		return LayoutResult::Success(KosmSize());

	KosmInsets padding = context.respect_safe_area
	                     ? context.EffectivePadding(params.padding)
	                     : params.padding;

	float usable_width = context.container_size.width - padding.left - padding.right;
	float usable_height = context.container_size.height - padding.top - padding.bottom;

	GridMatrix matrix;
	matrix.Init(params.row_count, params.col_count);

	for (uint32_t i = 0; i < count; ++i) {
		GridItem& item = items[i];
		if (item.gone) continue;

		if (item.row_span == 0) item.row_span = 1;
		if (item.col_span == 0) item.col_span = 1;

		if (params.auto_place && item.row == 0 && item.col == 0) {
			uint8_t r, c;
			if (AutoPlace(matrix, item.row_span, item.col_span, &r, &c, true)) {
				item.row = r;
				item.col = c;
			}
		}

		if (item.row + item.row_span <= matrix.rows &&
		    item.col + item.col_span <= matrix.cols) {
			matrix.Place(item.row, item.col, item.row_span, item.col_span,
			             static_cast<int16_t>(i));
		}
	}

	uint8_t actual_rows = matrix.rows;

	for (uint8_t r = params.row_count; r < matrix.rows; ++r)
		params.row_tracks[r] = params.auto_row_size;
	params.row_count = matrix.rows;

	float col_content[GRID_MAX_TRACKS];
	float row_content[GRID_MAX_TRACKS];
	CalcAutoTrackSizes(items, count, params.col_count, params.row_count,
	                   col_content, row_content);

	float col_sizes[GRID_MAX_TRACKS];
	float col_positions[GRID_MAX_TRACKS];
	float row_sizes[GRID_MAX_TRACKS];
	float row_positions[GRID_MAX_TRACKS];

	ResolveTracks(params.col_tracks, params.col_count, usable_width,
	              params.col_gap, col_content, col_sizes, col_positions);
	ResolveTracks(params.row_tracks, params.row_count, usable_height,
	              params.row_gap, row_content, row_sizes, row_positions);

	KosmSize content_size;

	for (uint32_t i = 0; i < count; ++i) {
		GridItem& item = items[i];

		if (item.gone) {
			item.frame = KosmRect();
			continue;
		}

		if (item.col >= params.col_count || item.row >= params.row_count)
			continue;

		float cell_x = padding.left + col_positions[item.col];
		float cell_y = padding.top + row_positions[item.row];

		float cell_width = 0;
		uint8_t col_end = item.col + item.col_span;
		if (col_end > params.col_count) col_end = params.col_count;

		for (uint8_t c = item.col; c < col_end; ++c) {
			cell_width += col_sizes[c];
			if (c > item.col) cell_width += params.col_gap;
		}

		float cell_height = 0;
		uint8_t row_end = item.row + item.row_span;
		if (row_end > params.row_count) row_end = params.row_count;

		for (uint8_t r = item.row; r < row_end; ++r) {
			cell_height += row_sizes[r];
			if (r > item.row) cell_height += params.row_gap;
		}

		float avail_width = cell_width - item.margin.left - item.margin.right;
		float avail_height = cell_height - item.margin.top - item.margin.bottom;
		if (avail_width < 0) avail_width = 0;
		if (avail_height < 0) avail_height = 0;

		float item_width = item.measured.width;
		float item_height = item.measured.height;

		if (item.aspect_ratio > 0) {
			float ratio_width = avail_height * item.aspect_ratio;
			float ratio_height = avail_width / item.aspect_ratio;

			if (ratio_width <= avail_width) {
				item_width = ratio_width;
				item_height = avail_height;
			} else {
				item_width = avail_width;
				item_height = ratio_height;
			}
		}

		if (item.min_size.width > 0 && item_width < item.min_size.width)
			item_width = item.min_size.width;
		if (item.max_size.width > 0 && item_width > item.max_size.width)
			item_width = item.max_size.width;
		if (item.min_size.height > 0 && item_height < item.min_size.height)
			item_height = item.min_size.height;
		if (item.max_size.height > 0 && item_height > item.max_size.height)
			item_height = item.max_size.height;

		float x = cell_x + item.margin.left;
		float y = cell_y + item.margin.top;

		if (params.col_align == FlexAlign::Stretch) {
			item_width = avail_width;
		} else if (item_width < avail_width) {
			switch (params.col_align) {
				case FlexAlign::Center:
					x += (avail_width - item_width) * 0.5f;
					break;
				case FlexAlign::End:
					x += avail_width - item_width;
					break;
				default:
					break;
			}
		}

		if (params.row_align == FlexAlign::Stretch) {
			item_height = avail_height;
		} else if (item_height < avail_height) {
			switch (params.row_align) {
				case FlexAlign::Center:
					y += (avail_height - item_height) * 0.5f;
					break;
				case FlexAlign::End:
					y += avail_height - item_height;
					break;
				default:
					break;
			}
		}

		item.frame = KosmRect(x, y, item_width, item_height);

		float x_end = x + item_width;
		float y_end = y + item_height;
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

	return LayoutResult::Success(content_size);
}

uint32_t ComputeItemsHash(
	const GridItem* items,
	uint32_t count,
	const GridLayoutParams& params
) {
	uint32_t hash = 0;

	hash = HashCombine(hash, params.col_count);
	hash = HashCombine(hash, params.row_count);
	hash = HashCombine(hash, HashFloat(params.col_gap));
	hash = HashCombine(hash, HashFloat(params.row_gap));
	hash = HashCombine(hash, HashInsets(params.padding));

	for (uint32_t i = 0; i < count; ++i) {
		const GridItem& item = items[i];
		hash = HashCombine(hash, item.row);
		hash = HashCombine(hash, item.col);
		hash = HashCombine(hash, item.row_span);
		hash = HashCombine(hash, item.col_span);
		hash = HashCombine(hash, HashSize(item.measured));
		hash = HashCombine(hash, HashInsets(item.margin));
		hash = HashCombine(hash, item.gone ? 1u : 0u);
	}

	return hash;
}

bool AnyDirty(const GridItem* items, uint32_t count) {
	for (uint32_t i = 0; i < count; ++i) {
		if (items[i].NeedsLayout()) return true;
	}
	return false;
}

LayoutResult LayoutCached(
	GridItem* items,
	uint32_t count,
	const KosmSize& container_size,
	GridLayoutParams& params,
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

} // namespace Grid
} // namespace KosmOS
