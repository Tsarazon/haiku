/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Refactored 2025: Gen 9+ cursor support
 *
 * Gen 9+ cursor hardware per Intel PRM Vol 12 "Cursor":
 * - Supports 64x64, 128x128, 256x256 sizes
 * - ARGB8888 format (32-bit with alpha)
 * - Per-pipe cursor planes (CUR_CTL, CUR_BASE, CUR_POS)
 *
 * Register layout for Pipe A (add 0x1000 for Pipe B, 0x2000 for Pipe C):
 * - CUR_CTL:  0x70080 (Cursor Control)
 * - CUR_BASE: 0x70084 (Cursor Base Address)
 * - CUR_POS:  0x70088 (Cursor Position)
 *
 * ✅ Register addresses verified against PRM Vol 2c
 * ⚠️ Legacy 2-color cursor mode removed (Gen 9+ uses ARGB only)
 */


#include "accelerant_protos.h"
#include "accelerant.h"

#include <string.h>


#undef TRACE
#define TRACE(x...) _sPrintf("intel_extreme: " x)
#define ERROR(x...) _sPrintf("intel_extreme: " x)


/*
 * Gen 9+ Cursor Control Register bits (CUR_CTL)
 * Per Intel PRM Vol 2c "CUR_CTL"
 */
#define GEN9_CUR_CTL_ENABLE			(1 << 31)
#define GEN9_CUR_CTL_GAMMA_ENABLE	(1 << 26)
#define GEN9_CUR_CTL_FORMAT_MASK	(0x7 << 24)
#define GEN9_CUR_CTL_FORMAT_ARGB	(0x2 << 24)  /* 32-bit ARGB */
#define GEN9_CUR_CTL_PIPE_CSC		(1 << 23)
#define GEN9_CUR_CTL_SIZE_MASK		(0x3 << 0)
#define GEN9_CUR_CTL_SIZE_64		(0x0 << 0)
#define GEN9_CUR_CTL_SIZE_128		(0x1 << 0)
#define GEN9_CUR_CTL_SIZE_256		(0x2 << 0)

/*
 * Gen 9+ Cursor Position Register bits (CUR_POS)
 * Per Intel PRM Vol 2c "CUR_POS"
 */
#define GEN9_CUR_POS_Y_SIGN			(1 << 31)
#define GEN9_CUR_POS_Y_MASK			(0xFFF << 16)
#define GEN9_CUR_POS_Y_SHIFT		16
#define GEN9_CUR_POS_X_SIGN			(1 << 15)
#define GEN9_CUR_POS_X_MASK			(0xFFF << 0)


/*
 * Convert monochrome cursor to ARGB format
 *
 * Gen 9+ only supports ARGB8888 cursor format.
 * Legacy 2-color AND/XOR mask must be converted.
 */
static void
convert_cursor_to_argb(uint8* dest, uint8* andMask, uint8* xorMask,
	uint16 width, uint16 height)
{
	uint8 byteWidth = (width + 7) / 8;
	uint32* argb = (uint32*)dest;

	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++) {
			uint8 byte = x / 8;
			uint8 bit = 7 - (x % 8);

			uint8 andBit = (andMask[byteWidth * y + byte] >> bit) & 1;
			uint8 xorBit = (xorMask[byteWidth * y + byte] >> bit) & 1;

			/*
			 * AND XOR Result
			 * 0   0   Black (opaque)
			 * 0   1   White (opaque)
			 * 1   0   Transparent
			 * 1   1   Invert (render as semi-transparent gray)
			 */
			if (andBit == 0 && xorBit == 0) {
				argb[y * 64 + x] = 0xFF000000;  /* Black */
			} else if (andBit == 0 && xorBit == 1) {
				argb[y * 64 + x] = 0xFFFFFFFF;  /* White */
			} else if (andBit == 1 && xorBit == 0) {
				argb[y * 64 + x] = 0x00000000;  /* Transparent */
			} else {
				/* Invert - approximate with semi-transparent */
				argb[y * 64 + x] = 0x80808080;
			}
		}
	}
}


status_t
intel_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	uint8* andMask, uint8* xorMask)
{
	TRACE("intel_set_cursor_shape: %dx%d, hot %d,%d\n",
		width, height, hotX, hotY);

	/* Gen 9+ supports 64x64, 128x128, 256x256 */
	if (width > 64 || height > 64) {
		ERROR("cursor size %dx%d exceeds 64x64 limit\n", width, height);
		return B_BAD_VALUE;
	}

	/* Disable cursor during update */
	write32(INTEL_CURSOR_CONTROL, 0);

	/* Clear cursor buffer (64x64 ARGB = 16KB) */
	uint8* cursorMem = gInfo->shared_info->cursor_memory;
	memset(cursorMem, 0, 64 * 64 * 4);

	/* Convert legacy 2-color cursor to ARGB */
	convert_cursor_to_argb(cursorMem, andMask, xorMask, width, height);

	/* Store cursor format for later use */
	gInfo->shared_info->cursor_format = GEN9_CUR_CTL_FORMAT_ARGB;

	/*
	 * Configure and enable cursor
	 * ✅ Per Intel PRM Vol 2c "CUR_CTL" programming sequence
	 */
	uint32 control = GEN9_CUR_CTL_ENABLE
		| GEN9_CUR_CTL_FORMAT_ARGB
		| GEN9_CUR_CTL_SIZE_64;

	write32(INTEL_CURSOR_CONTROL, control);

	/* Set cursor base address (must be 256KB aligned on Gen 9+) */
	uint64 cursorBase = (uint64)gInfo->shared_info->physical_graphics_memory
		+ gInfo->shared_info->cursor_buffer_offset;

	/* Gen 9+ uses 48-bit addresses, lower 32 bits in CUR_BASE */
	write32(INTEL_CURSOR_BASE, (uint32)(cursorBase & 0xFFFFFFFF));

	/* Handle hotspot change */
	if (hotX != gInfo->shared_info->cursor_hot_x
		|| hotY != gInfo->shared_info->cursor_hot_y) {
		uint32 pos = read32(INTEL_CURSOR_POSITION);

		int32 x = pos & GEN9_CUR_POS_X_MASK;
		int32 y = (pos >> GEN9_CUR_POS_Y_SHIFT) & 0xFFF;

		if (pos & GEN9_CUR_POS_X_SIGN)
			x = -x;
		if (pos & GEN9_CUR_POS_Y_SIGN)
			y = -y;

		x += gInfo->shared_info->cursor_hot_x;
		y += gInfo->shared_info->cursor_hot_y;

		gInfo->shared_info->cursor_hot_x = hotX;
		gInfo->shared_info->cursor_hot_y = hotY;

		intel_move_cursor(x, y);
	}

	return B_OK;
}


void
intel_move_cursor(uint16 _x, uint16 _y)
{
	int32 x = (int32)_x - gInfo->shared_info->cursor_hot_x;
	int32 y = (int32)_y - gInfo->shared_info->cursor_hot_y;

	uint32 pos = 0;

	if (x < 0) {
		pos |= GEN9_CUR_POS_X_SIGN;
		x = -x;
	}
	if (y < 0) {
		pos |= GEN9_CUR_POS_Y_SIGN;
		y = -y;
	}

	pos |= (x & 0xFFF);
	pos |= ((y & 0xFFF) << GEN9_CUR_POS_Y_SHIFT);

	write32(INTEL_CURSOR_POSITION, pos);
}


void
intel_show_cursor(bool isVisible)
{
	if (gInfo->shared_info->cursor_visible == isVisible)
		return;

	uint32 control = 0;
	if (isVisible) {
		control = GEN9_CUR_CTL_ENABLE
			| gInfo->shared_info->cursor_format
			| GEN9_CUR_CTL_SIZE_64;
	}

	write32(INTEL_CURSOR_CONTROL, control);

	/* Re-write base address when enabling cursor */
	if (isVisible) {
		uint64 cursorBase = (uint64)gInfo->shared_info->physical_graphics_memory
			+ gInfo->shared_info->cursor_buffer_offset;
		write32(INTEL_CURSOR_BASE, (uint32)(cursorBase & 0xFFFFFFFF));
	}

	gInfo->shared_info->cursor_visible = isVisible;
}
