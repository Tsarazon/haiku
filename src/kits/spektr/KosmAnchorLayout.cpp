/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <spektr/KosmAnchorLayout.hpp>

namespace KosmOS {
namespace Anchor {

LayoutResult Layout(
	AnchorItem* items,
	uint32_t count,
	const KosmSize& container_size,
	const AnchorLayoutParams& params
) {
	LayoutContext ctx = LayoutContext::Default(container_size);
	ctx.pixel_snap = params.pixel_snap;
	return Layout(items, count, ctx, params);
}

LayoutResult Layout(
	AnchorItem* items,
	uint32_t count,
	const LayoutContext& context,
	const AnchorLayoutParams& params
) {
	KosmSize content_size;
	float container_width = context.container_size.width;
	float container_height = context.container_size.height;

	for (uint32_t i = 0; i < count; ++i) {
		AnchorItem& item = items[i];

		if (item.gone) {
			item.frame = KosmRect();
			continue;
		}

		float x, y, w, h;

		bool has_left = HasAnchor(item.anchor, AnchorEdge::Left);
		bool has_right = HasAnchor(item.anchor, AnchorEdge::Right);
		bool has_center_x = HasAnchor(item.anchor, AnchorEdge::CenterX);

		if (has_left && has_right) {
			x = item.anchors.left;
			w = container_width - item.anchors.left - item.anchors.right;
		} else if (has_left) {
			x = item.anchors.left;
			w = item.size.width;
		} else if (has_right) {
			w = item.size.width;
			x = container_width - item.anchors.right - w;
		} else if (has_center_x) {
			w = item.size.width;
			x = (container_width - w) * 0.5f + item.center_offset.x;
		} else {
			x = 0;
			w = item.size.width;
		}

		bool has_top = HasAnchor(item.anchor, AnchorEdge::Top);
		bool has_bottom = HasAnchor(item.anchor, AnchorEdge::Bottom);
		bool has_center_y = HasAnchor(item.anchor, AnchorEdge::CenterY);

		if (has_top && has_bottom) {
			y = item.anchors.top;
			h = container_height - item.anchors.top - item.anchors.bottom;
		} else if (has_top) {
			y = item.anchors.top;
			h = item.size.height;
		} else if (has_bottom) {
			h = item.size.height;
			y = container_height - item.anchors.bottom - h;
		} else if (has_center_y) {
			h = item.size.height;
			y = (container_height - h) * 0.5f + item.center_offset.y;
		} else {
			y = 0;
			h = item.size.height;
		}

		if (item.min_size.width > 0 && w < item.min_size.width)
			w = item.min_size.width;
		if (item.max_size.width > 0 && w > item.max_size.width)
			w = item.max_size.width;
		if (item.min_size.height > 0 && h < item.min_size.height)
			h = item.min_size.height;
		if (item.max_size.height > 0 && h > item.max_size.height)
			h = item.max_size.height;

		item.frame = KosmRect(x, y, w, h);

		if (context.pixel_snap)
			item.frame = SnapRectToPixels(item.frame);

		item.ClearDirty();

		float x_end = item.frame.x + item.frame.width;
		float y_end = item.frame.y + item.frame.height;
		if (x_end > content_size.width) content_size.width = x_end;
		if (y_end > content_size.height) content_size.height = y_end;
	}

	return LayoutResult::Success(content_size);
}

} // namespace Anchor
} // namespace KosmOS
