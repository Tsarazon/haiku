/*
 * Copyright 2006-2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Support for i915 chipset and up based on the X driver,
 * Copyright 2006-2007 Intel Corporation.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Refactored 2025: Removed support for Gen < 9 (i830-Broadwell)
 * Minimum supported: Skylake (Gen 9), Apollo Lake
 */


#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Debug.h>

#include <create_display_modes.h>
#include <ddc.h>
#include <edid.h>
#include <validate_display_mode.h>

#include "accelerant_protos.h"
#include "accelerant.h"
#include "pll.h"
#include "Ports.h"

#undef TRACE
#define TRACE(x...) _sPrintf("intel_extreme: " x)
#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


static void
get_color_space_format(const display_mode &mode, uint32 &colorMode,
	uint32 &bytesPerRow, uint32 &bitsPerPixel)
{
	uint32 bytesPerPixel;

	// Gen 9+ always uses SKY/LAKE format registers
	switch (mode.space) {
		case B_RGB32_LITTLE:
			colorMode = DISPLAY_CONTROL_RGB32_SKY;
			bytesPerPixel = 4;
			bitsPerPixel = 32;
			break;
		case B_RGB16_LITTLE:
			colorMode = DISPLAY_CONTROL_RGB16_SKY;
			bytesPerPixel = 2;
			bitsPerPixel = 16;
			break;
		case B_RGB15_LITTLE:
			colorMode = DISPLAY_CONTROL_RGB15_SKY;
			bytesPerPixel = 2;
			bitsPerPixel = 15;
			break;
		case B_CMAP8:
		default:
			colorMode = DISPLAY_CONTROL_CMAP8_SKY;
			bytesPerPixel = 1;
			bitsPerPixel = 8;
			break;
	}

	bytesPerRow = mode.virtual_width * bytesPerPixel;

	// Stride must be 64-byte aligned for Gen 9+
	// PRM Vol 2c: PLANE_STRIDE - bits 9:0 contain stride/64
	if ((bytesPerRow & 63) != 0)
		bytesPerRow = (bytesPerRow + 63) & ~63;
}


static bool
sanitize_display_mode(display_mode& mode)
{
	// Gen 9+ does not require pixel count alignment
	display_constraints constraints = {
		// resolution
		320, 4096, 200, 4096,
		// pixel clock
		gInfo->shared_info->pll_info.min_frequency,
		gInfo->shared_info->pll_info.max_frequency,
		// horizontal timing constraints
		{1, 0, 8160, 32, 8192, 0, 8192},
		// vertical timing constraints
		{1, 1, 8190, 2, 8192, 1, 8192}
	};

	return sanitize_display_mode(mode, constraints,
		gInfo->has_edid ? &gInfo->edid_info : NULL);
}


// #pragma mark -


static void
set_frame_buffer_registers(uint32 offset)
{
	intel_shared_info &sharedInfo = *gInfo->shared_info;
	display_mode &mode = sharedInfo.current_mode;

	// Gen 9+ uses PLANE_OFFSET for pan/scroll and PLANE_SURF for base address
	// PRM Vol 2c: PLANE_OFFSET - bits 28:16 = Y offset, bits 12:0 = X offset
	write32(INTEL_DISPLAY_A_OFFSET_HAS + offset,
		((uint32)mode.v_display_start << 16) | (uint32)mode.h_display_start);
	read32(INTEL_DISPLAY_A_OFFSET_HAS + offset);

	// PRM Vol 2c: PLANE_SURF - bits 31:12 = surface base address (4K aligned)
	write32(INTEL_DISPLAY_A_SURFACE + offset, sharedInfo.frame_buffer_offset);
	read32(INTEL_DISPLAY_A_SURFACE + offset);
}


void
set_frame_buffer_base()
{
	// TODO: support multiple framebuffers with different addresses
	set_frame_buffer_registers(0);
	set_frame_buffer_registers(INTEL_DISPLAY_OFFSET);
}


/*!	Creates the initial mode list of the primary accelerant.
	It's called from intel_init_accelerant().
*/
status_t
create_mode_list(void)
{
	CALLED();

	for (uint32 i = 0; i < gInfo->port_count; i++) {
		if (gInfo->ports[i] == NULL)
			continue;

		status_t status = gInfo->ports[i]->GetEDID(&gInfo->edid_info);
		if (status == B_OK) {
			gInfo->has_edid = true;
			break;
		}
	}

	// Use EDID found at boot time if we don't have any ourselves
	if (!gInfo->has_edid && gInfo->shared_info->has_vesa_edid_info) {
		TRACE("%s: Using VESA edid info\n", __func__);
		memcpy(&gInfo->edid_info, &gInfo->shared_info->vesa_edid_info,
			sizeof(edid1_info));
		edid_dump(&gInfo->edid_info);
		gInfo->has_edid = true;
	}

	display_mode* list;
	uint32 count = 0;

	// Gen 9+ does not support B_RGB15, use custom colorspace list
	const color_space kSupportedSpaces[] = {
		B_RGB32_LITTLE,
		B_RGB16_LITTLE,
		B_CMAP8
	};

	// If no EDID but have VBT from driver, use that mode
	if (!gInfo->has_edid && gInfo->shared_info->got_vbt) {
		display_mode mode;
		mode.timing = gInfo->shared_info->panel_timing;
		mode.space = B_RGB32;
		mode.virtual_width = mode.timing.h_display;
		mode.virtual_height = mode.timing.v_display;
		mode.h_display_start = 0;
		mode.v_display_start = 0;
		mode.flags = 0;

		// TODO: support lower modes via panel fitter scaling
		gInfo->mode_list_area = create_display_modes("intel extreme modes",
			NULL, &mode, 1, kSupportedSpaces, B_COUNT_OF(kSupportedSpaces),
			NULL, &list, &count);
	} else {
		gInfo->mode_list_area = create_display_modes("intel extreme modes",
			gInfo->has_edid ? &gInfo->edid_info : NULL, NULL, 0,
			kSupportedSpaces, B_COUNT_OF(kSupportedSpaces),
			NULL, &list, &count);
	}

	if (gInfo->mode_list_area < B_OK)
		return gInfo->mode_list_area;

	gInfo->mode_list = list;
	gInfo->shared_info->mode_list_area = gInfo->mode_list_area;
	gInfo->shared_info->mode_count = count;

	return B_OK;
}


void
wait_for_vblank(void)
{
	acquire_sem_etc(gInfo->shared_info->vblank_sem, 1, B_RELATIVE_TIMEOUT,
		21000);
		// With output turned off via DPMS, we might not get interrupts.
		// At 50Hz, vblank occurs within 20ms max.
}


//	#pragma mark -


uint32
intel_accelerant_mode_count(void)
{
	CALLED();
	return gInfo->shared_info->mode_count;
}


status_t
intel_get_mode_list(display_mode* modeList)
{
	CALLED();
	memcpy(modeList, gInfo->mode_list,
		gInfo->shared_info->mode_count * sizeof(display_mode));
	return B_OK;
}


status_t
intel_propose_display_mode(display_mode* target, const display_mode* low,
	const display_mode* high)
{
	CALLED();

	display_mode mode = *target;

	if (sanitize_display_mode(*target)) {
		TRACE("Video mode was adjusted by sanitize_display_mode\n");
		TRACE("Initial mode: Hd %d Hs %d He %d Ht %d Vd %d Vs %d Ve %d Vt %d\n",
			mode.timing.h_display, mode.timing.h_sync_start,
			mode.timing.h_sync_end, mode.timing.h_total,
			mode.timing.v_display, mode.timing.v_sync_start,
			mode.timing.v_sync_end, mode.timing.v_total);
		TRACE("Sanitized: Hd %d Hs %d He %d Ht %d Vd %d Vs %d Ve %d Vt %d\n",
			target->timing.h_display, target->timing.h_sync_start,
			target->timing.h_sync_end, target->timing.h_total,
			target->timing.v_display, target->timing.v_sync_start,
			target->timing.v_sync_end, target->timing.v_total);
	}

	target->flags |= B_SCROLL;

	return is_display_mode_within_bounds(*target, *low, *high)
		? B_OK : B_BAD_VALUE;
}


status_t
intel_set_display_mode(display_mode* mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;

	TRACE("%s(%" B_PRIu16 "x%" B_PRIu16 ", virtual: %" B_PRIu16 "x%" B_PRIu16 ")\n",
		__func__, mode->timing.h_display, mode->timing.v_display,
		mode->virtual_width, mode->virtual_height);

	display_mode target = *mode;

	if (intel_propose_display_mode(&target, &target, &target) != B_OK)
		return B_BAD_VALUE;

	uint32 colorMode, bytesPerRow, bitsPerPixel;
	get_color_space_format(target, colorMode, bytesPerRow, bitsPerPixel);

	intel_shared_info &sharedInfo = *gInfo->shared_info;
	Autolock locker(sharedInfo.accelerant_lock);

	set_display_power_mode(B_DPMS_OFF);

	// Free old and allocate new frame buffer in graphics memory
	intel_free_memory(sharedInfo.frame_buffer);

	addr_t base;
	if (intel_allocate_memory(bytesPerRow * target.virtual_height, 0,
			base) < B_OK) {
		// Try to restore framebuffer for previous mode
		if (intel_allocate_memory(sharedInfo.current_mode.virtual_height
				* sharedInfo.bytes_per_row, 0, base) == B_OK) {
			sharedInfo.frame_buffer = base;
			sharedInfo.frame_buffer_offset = base
				- (addr_t)sharedInfo.graphics_memory;
			set_frame_buffer_base();
		}

		ERROR("%s: Failed to allocate framebuffer!\n", __func__);
		return B_NO_MEMORY;
	}

	memset((uint8*)base, 0, bytesPerRow * target.virtual_height);
	sharedInfo.frame_buffer = base;
	sharedInfo.frame_buffer_offset = base - (addr_t)sharedInfo.graphics_memory;

	// Disable VGA display
	write32(INTEL_VGA_DISPLAY_CONTROL, VGA_DISPLAY_DISABLED);
	read32(INTEL_VGA_DISPLAY_CONTROL);

	// Configure each connected port
	for (uint32 i = 0; i < gInfo->port_count; i++) {
		if (gInfo->ports[i] == NULL)
			continue;
		if (!gInfo->ports[i]->IsConnected())
			continue;

		status_t status = gInfo->ports[i]->SetDisplayMode(&target, colorMode);
		if (status != B_OK)
			ERROR("%s: Unable to set display mode!\n", __func__);
	}

	TRACE("%s: Port configuration completed successfully!\n", __func__);

	program_pipe_color_modes(colorMode);

	set_display_power_mode(sharedInfo.dpms_mode);

	// Gen 9+ PLANE_STRIDE register: value is stride/64
	// PRM Vol 2c: PLANE_STRIDE bits 9:0
	write32(INTEL_DISPLAY_A_BYTES_PER_ROW, bytesPerRow >> 6);
	write32(INTEL_DISPLAY_B_BYTES_PER_ROW, bytesPerRow >> 6);

	sharedInfo.current_mode = target;
	sharedInfo.bytes_per_row = bytesPerRow;
	sharedInfo.bits_per_pixel = bitsPerPixel;

	set_frame_buffer_base();

	return B_OK;
}


status_t
intel_get_display_mode(display_mode* _currentMode)
{
	CALLED();
	*_currentMode = gInfo->shared_info->current_mode;
	return B_OK;
}


status_t
intel_get_preferred_mode(display_mode* preferredMode)
{
	TRACE("%s\n", __func__);

	if (gInfo->has_edid || !gInfo->shared_info->got_vbt
			|| !gInfo->shared_info->device_type.IsMobile()) {
		return B_ERROR;
	}

	display_mode mode;
	mode.timing = gInfo->shared_info->panel_timing;
	mode.space = B_RGB32;
	mode.virtual_width = mode.timing.h_display;
	mode.virtual_height = mode.timing.v_display;
	mode.h_display_start = 0;
	mode.v_display_start = 0;
	mode.flags = 0;

	memcpy(preferredMode, &mode, sizeof(mode));
	return B_OK;
}


status_t
intel_get_edid_info(void* info, size_t size, uint32* _version)
{
	if (!gInfo->has_edid)
		return B_ERROR;
	if (size < sizeof(struct edid1_info))
		return B_BUFFER_OVERFLOW;

	memcpy(info, &gInfo->edid_info, sizeof(struct edid1_info));
	*_version = EDID_VERSION_1;
	return B_OK;
}


// Get backlight registers for Gen 9+ (CNP/SPT and newer PCH)
// PRM Vol 2c: BLC_PWM_CTL, BLC_PWM_DUTY_CYCLE
static uint32
intel_get_backlight_register(bool period)
{
	if (gInfo->shared_info->pch_info >= INTEL_PCH_CNP) {
		// Cannon Lake PCH and newer: separate period and duty cycle registers
		if (period)
			return PCH_SOUTH_BLC_PWM_PERIOD;
		else
			return PCH_SOUTH_BLC_PWM_DUTY_CYCLE;
	}

	// Sunrise Point PCH (Skylake/Kaby Lake): combined register
	return BLC_PWM_PCH_CTL2;
}


status_t
intel_set_brightness(float brightness)
{
	CALLED();

	if (brightness < 0 || brightness > 1)
		return B_BAD_VALUE;

	if (gInfo->shared_info->pch_info >= INTEL_PCH_CNP) {
		// Cannon Lake+: separate registers
		uint32 period = read32(intel_get_backlight_register(true));
		uint32 duty = (uint32)(period * brightness);
		duty = std::max(duty, (uint32)gInfo->shared_info->min_brightness);
		write32(intel_get_backlight_register(false), duty);
	} else {
		// Sunrise Point: combined register (period in upper 16 bits)
		uint32 tmp = read32(intel_get_backlight_register(true));
		uint32 period = tmp >> 16;
		uint32 duty = (uint32)(period * brightness) & 0xffff;
		duty = std::max(duty, (uint32)gInfo->shared_info->min_brightness);
		write32(intel_get_backlight_register(false), duty | (period << 16));
	}

	return B_OK;
}


status_t
intel_get_brightness(float* brightness)
{
	CALLED();

	if (brightness == NULL)
		return B_BAD_VALUE;

	uint32 duty;
	uint32 period;

	if (gInfo->shared_info->pch_info >= INTEL_PCH_CNP) {
		// Cannon Lake+: separate registers
		period = read32(intel_get_backlight_register(true));
		duty = read32(intel_get_backlight_register(false));
	} else {
		// Sunrise Point: combined register
		uint32 tmp = read32(intel_get_backlight_register(true));
		period = tmp >> 16;
		duty = read32(intel_get_backlight_register(false)) & 0xffff;
	}

	*brightness = (float)duty / period;
	return B_OK;
}


status_t
intel_get_frame_buffer_config(frame_buffer_config* config)
{
	CALLED();

	uint32 offset = gInfo->shared_info->frame_buffer_offset;

	config->frame_buffer = gInfo->shared_info->graphics_memory + offset;
	config->frame_buffer_dma
		= (uint8*)gInfo->shared_info->physical_graphics_memory + offset;
	config->bytes_per_row = gInfo->shared_info->bytes_per_row;

	return B_OK;
}


status_t
intel_get_pixel_clock_limits(display_mode* mode, uint32* _low, uint32* _high)
{
	CALLED();

	if (_low != NULL) {
		// Lower limit of about 48Hz vertical refresh
		uint32 totalClocks = (uint32)mode->timing.h_total
			* (uint32)mode->timing.v_total;
		uint32 low = (totalClocks * 48L) / 1000L;

		if (low < gInfo->shared_info->pll_info.min_frequency)
			low = gInfo->shared_info->pll_info.min_frequency;
		else if (low > gInfo->shared_info->pll_info.max_frequency)
			return B_ERROR;

		*_low = low;
	}

	if (_high != NULL)
		*_high = gInfo->shared_info->pll_info.max_frequency;

	return B_OK;
}


status_t
intel_move_display(uint16 horizontalStart, uint16 verticalStart)
{
	intel_shared_info &sharedInfo = *gInfo->shared_info;
	Autolock locker(sharedInfo.accelerant_lock);

	display_mode &mode = sharedInfo.current_mode;

	if (horizontalStart + mode.timing.h_display > mode.virtual_width
		|| verticalStart + mode.timing.v_display > mode.virtual_height)
		return B_BAD_VALUE;

	mode.h_display_start = horizontalStart;
	mode.v_display_start = verticalStart;

	set_frame_buffer_base();

	return B_OK;
}


status_t
intel_get_timing_constraints(display_timing_constraints* constraints)
{
	CALLED();
	return B_ERROR;
}


void
intel_set_indexed_colors(uint count, uint8 first, uint8* colors, uint32 flags)
{
	TRACE("%s(colors = %p, first = %u)\n", __func__, colors, first);

	if (colors == NULL)
		return;

	Autolock locker(gInfo->shared_info->accelerant_lock);

	for (; count-- > 0; first++) {
		uint32 color = colors[0] << 16 | colors[1] << 8 | colors[2];
		colors += 3;

		write32(INTEL_DISPLAY_A_PALETTE + first * sizeof(uint32), color);
		write32(INTEL_DISPLAY_B_PALETTE + first * sizeof(uint32), color);
	}
}
