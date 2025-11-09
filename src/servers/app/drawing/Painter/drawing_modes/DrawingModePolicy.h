/*
 * Copyright 2025, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Template-based drawing mode policies extracted from DrawingModeXXX.h files.
 * Each policy defines Blend() and Assign() operations for a specific drawing mode.
 *
 * This consolidates ~4500 lines of duplicated code across 41 files into
 * a compact set of policy classes that can be instantiated via templates.
 */

#ifndef DRAWING_MODE_POLICY_H
#define DRAWING_MODE_POLICY_H

#include "DrawingMode.h"

// Policy for B_OP_COPY mode (special - uses low/high color blending)
// Note: Copy mode is handled separately, not via template
struct CopyPolicy {
	// Not used in template - Copy mode has custom implementation
};

// Policy for B_OP_ADD mode
struct AddPolicy {
	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint8 alpha)
	{
		// BLEND_ADD: add and clamp to 255
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		uint8 rt = min_c(255, _p.data8[2] + r);
		uint8 gt = min_c(255, _p.data8[1] + g);
		uint8 bt = min_c(255, _p.data8[0] + b);
		BLEND(d, rt, gt, bt, alpha);
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint8 a1, uint8 a2, uint8 a3)
	{
		// BLEND_ADD_SUBPIX: add with separate alpha for RGB
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		uint8 rt = min_c(255, _p.data8[2] + r);
		uint8 gt = min_c(255, _p.data8[1] + g);
		uint8 bt = min_c(255, _p.data8[0] + b);
		BLEND_SUBPIX(d, rt, gt, bt, a1, a2, a3);
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// ASSIGN_ADD: add without alpha blending
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		d[0] = min_c(255, _p.data8[0] + b);
		d[1] = min_c(255, _p.data8[1] + g);
		d[2] = min_c(255, _p.data8[2] + r);
		d[3] = 255;
	}
};

// Policy for B_OP_SUBTRACT mode
struct SubtractPolicy {
	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint8 alpha)
	{
		// BLEND_SUBTRACT: subtract and clamp to 0
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		uint8 rt = max_c(0, _p.data8[2] - r);
		uint8 gt = max_c(0, _p.data8[1] - g);
		uint8 bt = max_c(0, _p.data8[0] - b);
		BLEND(d, rt, gt, bt, alpha);
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint8 a1, uint8 a2, uint8 a3)
	{
		// BLEND_SUBTRACT_SUBPIX: subtract with separate alpha for RGB
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		uint8 rt = max_c(0, _p.data8[2] - r);
		uint8 gt = max_c(0, _p.data8[1] - g);
		uint8 bt = max_c(0, _p.data8[0] - b);
		BLEND_SUBPIX(d, rt, gt, bt, a1, a2, a3);
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// ASSIGN_SUBTRACT: subtract without alpha blending
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		d[0] = max_c(0, _p.data8[0] - b);
		d[1] = max_c(0, _p.data8[1] - g);
		d[2] = max_c(0, _p.data8[2] - r);
		d[3] = 255;
	}
};

// Policy for B_OP_BLEND mode
struct BlendPolicy {
	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint8 alpha)
	{
		// BLEND_BLEND: average with destination
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		uint8 bt = (_p.data8[0] + b) >> 1;
		uint8 gt = (_p.data8[1] + g) >> 1;
		uint8 rt = (_p.data8[2] + r) >> 1;
		BLEND(d, rt, gt, bt, alpha);
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint8 a1, uint8 a2, uint8 a3)
	{
		// BLEND_BLEND_SUBPIX: average with separate alpha for RGB
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		uint8 bt = (_p.data8[0] + b) >> 1;
		uint8 gt = (_p.data8[1] + g) >> 1;
		uint8 rt = (_p.data8[2] + r) >> 1;
		BLEND_SUBPIX(d, rt, gt, bt, a1, a2, a3);
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// ASSIGN_BLEND: average without alpha blending
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		d[0] = (_p.data8[0] + b) >> 1;
		d[1] = (_p.data8[1] + g) >> 1;
		d[2] = (_p.data8[2] + r) >> 1;
		d[3] = 255;
	}
};

// Policy for B_OP_MIN mode
struct MinPolicy {
	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint8 alpha)
	{
		// BLEND_MIN: keep darker color based on brightness
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		if (brightness_for(r, g, b)
			< brightness_for(_p.data8[2], _p.data8[1], _p.data8[0])) {
			BLEND(d, r, g, b, alpha);
		}
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint8 a1, uint8 a2, uint8 a3)
	{
		// BLEND_MIN_SUBPIX: keep darker color with separate alpha for RGB
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		if (brightness_for(r, g, b)
			< brightness_for(_p.data8[2], _p.data8[1], _p.data8[0])) {
			BLEND_SUBPIX(d, r, g, b, a1, a2, a3);
		}
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// ASSIGN_MIN: keep darker color
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		if (brightness_for(r, g, b)
			< brightness_for(_p.data8[2], _p.data8[1], _p.data8[0])) {
			d[0] = b;
			d[1] = g;
			d[2] = r;
			d[3] = 255;
		}
	}
};

// Policy for B_OP_MAX mode
struct MaxPolicy {
	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint8 alpha)
	{
		// BLEND_MAX: keep brighter color based on brightness
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		if (brightness_for(r, g, b)
			> brightness_for(_p.data8[2], _p.data8[1], _p.data8[0])) {
			BLEND(d, r, g, b, alpha);
		}
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint8 a1, uint8 a2, uint8 a3)
	{
		// BLEND_MAX_SUBPIX: keep brighter color with separate alpha for RGB
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		if (brightness_for(r, g, b)
			> brightness_for(_p.data8[2], _p.data8[1], _p.data8[0])) {
			BLEND_SUBPIX(d, r, g, b, a1, a2, a3);
		}
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// ASSIGN_MAX: keep brighter color
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		if (brightness_for(r, g, b)
			> brightness_for(_p.data8[2], _p.data8[1], _p.data8[0])) {
			d[0] = b;
			d[1] = g;
			d[2] = r;
			d[3] = 255;
		}
	}
};

// Policy for B_OP_ALPHA with B_CONSTANT_ALPHA and B_ALPHA_COMPOSITE (AlphaCC)
struct AlphaCCPolicy {
	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint16 alpha)
	{
		// BLEND_ALPHA_CC: composite blending with 16-bit alpha
		BLEND_COMPOSITE16(d, r, g, b, alpha);
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint16 a1, uint16 a2, uint16 a3)
	{
		// BLEND_ALPHA_CC_SUBPIX: composite with separate alpha for RGB
		BLEND_COMPOSITE16_SUBPIX(d, r, g, b, a1, a2, a3);
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// ASSIGN_ALPHA_CC: direct assignment
		d[0] = b;
		d[1] = g;
		d[2] = r;
		d[3] = 255;
	}
};

// Policy for B_OP_ALPHA with B_CONSTANT_ALPHA and B_ALPHA_OVERLAY (AlphaCO)
struct AlphaCOPolicy {
	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint16 alpha)
	{
		// BLEND_ALPHA_CO: overlay blending with 16-bit alpha
		BLEND16(d, r, g, b, alpha);
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint16 a1, uint16 a2, uint16 a3)
	{
		// BLEND_ALPHA_CO_SUBPIX: overlay with separate alpha for RGB
		BLEND16_SUBPIX(d, r, g, b, a1, a2, a3);
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// ASSIGN_ALPHA_CO: direct assignment
		d[0] = b;
		d[1] = g;
		d[2] = r;
		d[3] = 255;
	}
};

// Policy for B_OP_ALPHA with B_PIXEL_ALPHA and B_ALPHA_COMPOSITE (AlphaPC)
struct AlphaPCPolicy {
	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint16 alpha)
	{
		// BLEND_ALPHA_PC: composite blending with 16-bit alpha
		BLEND_COMPOSITE16(d, r, g, b, alpha);
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint16 a1, uint16 a2, uint16 a3)
	{
		// BLEND_ALPHA_PC_SUBPIX: composite with separate alpha for RGB
		BLEND_COMPOSITE16_SUBPIX(d, r, g, b, a1, a2, a3);
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// ASSIGN_ALPHA_PC: direct assignment
		d[0] = b;
		d[1] = g;
		d[2] = r;
		d[3] = 255;
	}
};

// Policy for B_OP_ALPHA with B_PIXEL_ALPHA and B_ALPHA_OVERLAY (AlphaPO)
struct AlphaPOPolicy {
	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint16 alpha)
	{
		// BLEND_ALPHA_PO: overlay blending with 16-bit alpha
		BLEND16(d, r, g, b, alpha);
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint16 a1, uint16 a2, uint16 a3)
	{
		// BLEND_ALPHA_PO_SUBPIX: overlay with separate alpha for RGB
		BLEND16_SUBPIX(d, r, g, b, a1, a2, a3);
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// ASSIGN_ALPHA_PO: direct assignment
		d[0] = b;
		d[1] = g;
		d[2] = r;
		d[3] = 255;
	}
};

// OverPolicy: Simple BLEND operation with HighColor from pattern
struct OverPolicy {
	static inline rgb_color GetColor(const PatternHandler* pattern)
	{
		return pattern->HighColor();
	}

	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// BLEND_OVER: simple BLEND
		BLEND(d, r, g, b, a);
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint8 a1, uint8 a2, uint8 a3)
	{
		// BLEND_OVER_SUBPIX: simple BLEND_SUBPIX
		BLEND_SUBPIX(d, r, g, b, a1, a2, a3);
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a = 255)
	{
		// ASSIGN_OVER: direct assignment (ignores alpha for compatibility)
		d[0] = b;
		d[1] = g;
		d[2] = r;
		d[3] = 255;
		(void)a; // Suppress unused warning
	}
};

// ErasePolicy: Same as Over but uses LowColor
struct ErasePolicy {
	static inline rgb_color GetColor(const PatternHandler* pattern)
	{
		return pattern->LowColor();
	}

	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// BLEND_ERASE: simple BLEND
		BLEND(d, r, g, b, a);
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint8 a1, uint8 a2, uint8 a3)
	{
		// BLEND_ERASE_SUBPIX: simple BLEND_SUBPIX
		BLEND_SUBPIX(d, r, g, b, a1, a2, a3);
	}

	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a = 255)
	{
		// ASSIGN_ERASE: direct assignment (ignores alpha for compatibility)
		d[0] = b;
		d[1] = g;
		d[2] = r;
		d[3] = 255;
		(void)a; // Suppress unused warning
	}
};

// InvertPolicy: Inverts destination pixel (ignores input RGB)
struct InvertPolicy {
	// Dummy GetColor - Invert doesn't use pattern color, but needed for template
	static inline rgb_color GetColor(const PatternHandler* pattern)
	{
		rgb_color dummy = {0, 0, 0, 255};
		return dummy;
	}

	// Standard signature but ignores r, g, b - reads destination and inverts
	static inline void Blend(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a)
	{
		// BLEND_INVERT: read dest, invert, then blend (ignores r, g, b params)
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		BLEND(d, 255 - _p.data8[2], 255 - _p.data8[1], 255 - _p.data8[0], a);
		(void)r; (void)g; (void)b; // Suppress unused warnings
	}

	static inline void BlendSubpix(uint8* d, uint8 r, uint8 g, uint8 b,
								   uint8 a1, uint8 a2, uint8 a3)
	{
		// BLEND_INVERT_SUBPIX: read dest, invert, then blend (ignores r, g, b params)
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		BLEND_SUBPIX(d, 255 - _p.data8[2], 255 - _p.data8[1], 255 - _p.data8[0],
					 a1, a2, a3);
		(void)r; (void)g; (void)b; // Suppress unused warnings
	}

	// Standard signature but ignores r, g, b, a - reads destination and inverts
	static inline void Assign(uint8* d, uint8 r, uint8 g, uint8 b, uint8 a = 255)
	{
		// ASSIGN_INVERT: read dest, invert, assign (ignores r, g, b, a params)
		pixel32 _p;
		_p.data32 = *(uint32*)d;
		d[0] = 255 - _p.data8[0];
		d[1] = 255 - _p.data8[1];
		d[2] = 255 - _p.data8[2];
		d[3] = 255;
		(void)r; (void)g; (void)b; (void)a; // Suppress unused warnings
	}
};

#endif // DRAWING_MODE_POLICY_H
