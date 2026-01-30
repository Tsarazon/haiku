/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KOSM_ANCHOR_LAYOUT_HPP
#define _KOSM_ANCHOR_LAYOUT_HPP

#include <spektr/KosmLayoutTypes.hpp>

namespace KosmOS {

struct AnchorItem {
	KosmSize	size;
	KosmInsets	anchors;
	KosmPoint	center_offset;
	KosmSize	min_size;
	KosmSize	max_size;

	AnchorEdge	anchor;
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

	static AnchorItem Default() {
		AnchorItem item = {};
		item.anchor = AnchorEdge::None;
		item.dirty = DirtyFlag::All;
		return item;
	}
};

struct AnchorLayoutParams {
	bool	pixel_snap;

	static AnchorLayoutParams Default() {
		AnchorLayoutParams params = {};
		params.pixel_snap = true;
		return params;
	}
};

namespace Anchor {

LayoutResult Layout(
	AnchorItem* items,
	uint32_t count,
	const KosmSize& container_size,
	const AnchorLayoutParams& params
);

LayoutResult Layout(
	AnchorItem* items,
	uint32_t count,
	const LayoutContext& context,
	const AnchorLayoutParams& params
);

} // namespace Anchor
} // namespace KosmOS

#endif /* _KOSM_ANCHOR_LAYOUT_HPP */
