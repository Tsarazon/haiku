/*
 * Copyright 2005-2006, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2008, Andrej Spielmann <andrej.spielmann@seh.ox.ac.uk>.
 * Copyright 2025, Haiku Blend2D Migration
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Global definitions for the Painter framework - Blend2D backend
 */

#ifndef DEFINES_H
#define DEFINES_H

#include <blend2d.h>
#include <GraphicsDefs.h>

// Forward declarations
class PatternHandler;

// ============================================================================
// Pixel Format
// ============================================================================

// Note: We keep using our custom PixelFormat wrapper class which now
// uses Blend2D internally instead of directly typedef'ing BL types
#include "drawing_modes/PixelFormat.h"

typedef PixelFormat pixfmt;

// ============================================================================
// Renderer Base (deprecated, but kept for compatibility during migration)
// ============================================================================

// In Blend2D we don't have a separate "renderer_base" concept.
// The BLContext handles both rendering and base operations.
// This is a stub for compatibility during migration.
struct renderer_base {
	renderer_base(pixfmt& format)
		: fPixelFormat(&format)
	{
	}

	// Compatibility stubs - these should not be used in new code
	unsigned width() const { return fPixelFormat->width(); }
	unsigned height() const { return fPixelFormat->height(); }

	pixfmt* fPixelFormat;
};

// ============================================================================
// Scanline Types (deprecated - Blend2D doesn't use scanlines)
// ============================================================================

// These are kept as empty stubs for compatibility during migration.
// Blend2D handles rasterization internally via BLContext.
// DO NOT USE THESE IN NEW CODE!

struct scanline_unpacked_type {
	// Empty stub - Blend2D doesn't expose scanlines
};

struct scanline_packed_type {
	// Empty stub - Blend2D doesn't expose scanlines
};

struct scanline_packed_subpix_type {
	// Empty stub - not needed with Blend2D
};

struct scanline_unpacked_subpix_type {
	// Empty stub - not needed with Blend2D
};

struct scanline_unpacked_masked_type {
	// Empty stub - masking handled differently in Blend2D
};

// ============================================================================
// Rasterizer Types (deprecated - Blend2D uses BLContext)
// ============================================================================

// These are kept as empty stubs for compatibility during migration.
// In Blend2D, all rasterization is done through BLContext methods.
// DO NOT USE THESE IN NEW CODE!

struct rasterizer_type {
	void reset() {}
	void add_path(const BLPath&) {}
	void clip_box(int, int, int, int) {}
	void filling_rule(int) {}
	void gamma(float) {}
};

struct rasterizer_subpix_type {
	void reset() {}
	void add_path(const BLPath&) {}
	void clip_box(int, int, int, int) {}
	void filling_rule(int) {}
};

// ============================================================================
// Renderer Types (deprecated - Blend2D uses BLContext)
// ============================================================================

// These are kept as empty stubs for compatibility during migration.
// DO NOT USE THESE IN NEW CODE!

struct renderer_type {
	renderer_type(renderer_base&) {}
	void color(const BLRgba32&) {}
};

struct renderer_bin_type {
	renderer_bin_type(renderer_base&) {}
	void color(const BLRgba32&) {}
};

struct renderer_subpix_type {
	renderer_subpix_type(renderer_base&) {}
	void color(const BLRgba32&) {}
};

// ============================================================================
// Drawing Mode Constants
// ============================================================================

#define ALIASED_DRAWING 0

// ============================================================================
// Helper Functions for Migration
// ============================================================================

// These can be used during migration to convert between old AGG patterns
// and new Blend2D patterns

inline BLCompOp blend2d_comp_op_for_drawing_mode(drawing_mode mode,
	source_alpha srcAlpha, alpha_function alphaFunc)
{
	switch (mode) {
		case B_OP_COPY:
			return BL_COMP_OP_SRC_COPY;
		case B_OP_OVER:
			return BL_COMP_OP_SRC_OVER;
		case B_OP_ADD:
			return BL_COMP_OP_PLUS;
		case B_OP_SUBTRACT:
			return BL_COMP_OP_MINUS;
		case B_OP_MIN:
			return BL_COMP_OP_DARKEN;
		case B_OP_MAX:
			return BL_COMP_OP_LIGHTEN;
		case B_OP_ALPHA:
			// Handle different alpha modes
			if (alphaFunc == B_ALPHA_OVERLAY)
				return BL_COMP_OP_SRC_OVER;
			// Add other alpha modes as needed
			return BL_COMP_OP_SRC_OVER;
		default:
			return BL_COMP_OP_SRC_OVER;
	}
}

inline BLStrokeCap blend2d_stroke_cap_for(cap_mode mode)
{
	switch (mode) {
		case B_BUTT_CAP:
			return BL_STROKE_CAP_BUTT;
		case B_SQUARE_CAP:
			return BL_STROKE_CAP_SQUARE;
		case B_ROUND_CAP:
			return BL_STROKE_CAP_ROUND;
		default:
			return BL_STROKE_CAP_BUTT;
	}
}

inline BLStrokeJoin blend2d_stroke_join_for(join_mode mode)
{
	switch (mode) {
		case B_MITER_JOIN:
			return BL_STROKE_JOIN_MITER_CLIP;
		case B_ROUND_JOIN:
			return BL_STROKE_JOIN_ROUND;
		case B_BEVEL_JOIN:
		case B_BUTT_JOIN:
		case B_SQUARE_JOIN:
			return BL_STROKE_JOIN_BEVEL;
		default:
			return BL_STROKE_JOIN_MITER_CLIP;
	}
}

// ============================================================================
// Migration Notes
// ============================================================================

/*
 * IMPORTANT MIGRATION NOTES:
 *
 * 1. Scanline-based rendering is DEPRECATED
 *    - Old AGG way: rasterizer.add_path() + render_scanlines()
 *    - New Blend2D way: context.fillPath() or context.strokePath()
 *
 * 2. Separate renderer objects are DEPRECATED
 *    - Old AGG way: multiple renderer objects (fRenderer, fRendererBin, etc.)
 *    - New Blend2D way: single BLContext does everything
 *
 * 3. Manual rasterization is DEPRECATED
 *    - Old AGG way: fRasterizer.reset(), add_path(), render
 *    - New Blend2D way: context handles this internally
 *
 * 4. Pattern handling is DIFFERENT
 *    - Old AGG way: PatternHandler integrated with scanline rendering
 *    - New Blend2D way: Use BLPattern or custom drawing via PixelFormat
 *
 * 5. The stub types (renderer_base, rasterizer_type, etc.) are ONLY for
 *    compatibility during migration. They should be removed once migration
 *    is complete!
 */

#endif // DEFINES_H
