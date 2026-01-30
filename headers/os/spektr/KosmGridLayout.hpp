/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_GRID_LAYOUT_HPP
#define _KOSM_GRID_LAYOUT_HPP

#include <spektr/KosmLayoutTypes.hpp>

namespace KosmOS {

static constexpr uint32_t GRID_MAX_TRACKS = 32;
static constexpr int16_t GRID_CELL_EMPTY = -1;

struct GridItem {
	uint16_t	row;
	uint16_t	col;
	uint8_t		row_span;
	uint8_t		col_span;

	KosmSize	measured;
	KosmInsets	margin;
	KosmSize	min_size;
	KosmSize	max_size;
	float		aspect_ratio;

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

	static GridItem Default() {
		GridItem item = {};
		item.row_span = 1;
		item.col_span = 1;
		item.dirty = DirtyFlag::All;
		return item;
	}
};

struct GridLayoutParams {
	TrackSize	col_tracks[GRID_MAX_TRACKS];
	TrackSize	row_tracks[GRID_MAX_TRACKS];
	uint8_t		col_count;
	uint8_t		row_count;

	float		col_gap;
	float		row_gap;
	FlexAlign	col_align;
	FlexAlign	row_align;

	KosmInsets	padding;
	TrackSize	auto_row_size;

	bool		auto_place;
	bool		pixel_snap;

	static GridLayoutParams Default() {
		GridLayoutParams params = {};
		params.col_count = 1;
		params.row_count = 1;
		params.col_tracks[0] = TrackSize::Fraction(1);
		params.row_tracks[0] = TrackSize::AutoSize();
		params.col_align = FlexAlign::Stretch;
		params.row_align = FlexAlign::Start;
		params.auto_row_size = TrackSize::AutoSize();
		params.pixel_snap = true;
		return params;
	}
};

struct GridMatrix {
	int16_t		cells[GRID_MAX_TRACKS][GRID_MAX_TRACKS];
	uint8_t		rows;
	uint8_t		cols;

	void Init(uint8_t r, uint8_t c) {
		rows = (r < GRID_MAX_TRACKS) ? r : GRID_MAX_TRACKS;
		cols = (c < GRID_MAX_TRACKS) ? c : GRID_MAX_TRACKS;
		for (uint8_t i = 0; i < GRID_MAX_TRACKS; ++i) {
			for (uint8_t j = 0; j < GRID_MAX_TRACKS; ++j) {
				cells[i][j] = GRID_CELL_EMPTY;
			}
		}
	}

	bool IsEmpty(uint8_t r, uint8_t c) const {
		if (r >= rows || c >= cols) return false;
		return cells[r][c] == GRID_CELL_EMPTY;
	}

	bool CanPlace(uint8_t row, uint8_t col, uint8_t row_span, uint8_t col_span) const {
		if (row + row_span > rows || col + col_span > cols) return false;
		for (uint8_t r = row; r < row + row_span; ++r) {
			for (uint8_t c = col; c < col + col_span; ++c) {
				if (cells[r][c] != GRID_CELL_EMPTY) return false;
			}
		}
		return true;
	}

	bool Place(uint8_t row, uint8_t col, uint8_t row_span, uint8_t col_span, int16_t index) {
		if (row + row_span > GRID_MAX_TRACKS || col + col_span > GRID_MAX_TRACKS)
			return false;
		if (row + row_span > rows || col + col_span > cols)
			return false;
		for (uint8_t r = row; r < row + row_span; ++r) {
			for (uint8_t c = col; c < col + col_span; ++c) {
				cells[r][c] = index;
			}
		}
		return true;
	}

	void AddRow() {
		if (rows < GRID_MAX_TRACKS)
			++rows;
	}
};

namespace Grid {

void ResolveTracks(
	const TrackSize* tracks,
	uint8_t count,
	float available,
	float gap,
	const float* content_sizes,
	float* out_sizes,
	float* out_positions
);

bool AutoPlace(
	GridMatrix& matrix,
	uint8_t row_span,
	uint8_t col_span,
	uint8_t* out_row,
	uint8_t* out_col,
	bool add_rows_if_needed
);

LayoutResult Layout(
	GridItem* items,
	uint32_t count,
	const KosmSize& container_size,
	GridLayoutParams& params
);

LayoutResult Layout(
	GridItem* items,
	uint32_t count,
	const LayoutContext& context,
	GridLayoutParams& params
);

LayoutResult LayoutCached(
	GridItem* items,
	uint32_t count,
	const KosmSize& container_size,
	GridLayoutParams& params,
	LayoutCache& cache
);

uint32_t ComputeItemsHash(
	const GridItem* items,
	uint32_t count,
	const GridLayoutParams& params
);

bool AnyDirty(const GridItem* items, uint32_t count);

} // namespace Grid
} // namespace KosmOS

#endif /* _KOSM_GRID_LAYOUT_HPP */
