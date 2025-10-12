/*
 * Copyright 2006-2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Support for i915 chipset and up based on the X driver,
 * Copyright 2006-2007 Intel Corporation.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
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
#include "utility.h"


#undef TRACE
#define TRACE_MODE
#ifdef TRACE_MODE
#	define TRACE(x...) _sPrintf("intel_extreme: " x)
#else
#	define TRACE(x...)
#endif

#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED(x...) TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


static void
get_color_space_format(const display_mode &mode, uint32 &colorMode,
	uint32 &bytesPerRow, uint32 &bitsPerPixel)
{
	uint32 bytesPerPixel;

	switch (mode.space) {
		case B_RGB32_LITTLE:
			colorMode = gInfo->shared_info->device_type.InFamily(INTEL_FAMILY_LAKE)
				? DISPLAY_CONTROL_RGB32_SKY : DISPLAY_CONTROL_RGB32;
			bytesPerPixel = 4;
			bitsPerPixel = 32;
			break;
		case B_RGB16_LITTLE:
			colorMode = gInfo->shared_info->device_type.InFamily(INTEL_FAMILY_LAKE)
				? DISPLAY_CONTROL_RGB16_SKY : DISPLAY_CONTROL_RGB16;
			bytesPerPixel = 2;
			bitsPerPixel = 16;
			break;
		case B_RGB15_LITTLE:
			colorMode = gInfo->shared_info->device_type.InFamily(INTEL_FAMILY_LAKE)
				? DISPLAY_CONTROL_RGB15_SKY : DISPLAY_CONTROL_RGB15;
			bytesPerPixel = 2;
			bitsPerPixel = 15;
			break;
		case B_CMAP8:
		default:
			colorMode = gInfo->shared_info->device_type.InFamily(INTEL_FAMILY_LAKE)
				? DISPLAY_CONTROL_CMAP8_SKY : DISPLAY_CONTROL_CMAP8;
			bytesPerPixel = 1;
			bitsPerPixel = 8;
			break;
	}

	bytesPerRow = mode.virtual_width * bytesPerPixel;

	// Make sure bytesPerRow is a multiple of 64
	if ((bytesPerRow & 63) != 0)
		bytesPerRow = (bytesPerRow + 63) & ~63;
}


static bool
sanitize_display_mode(display_mode& mode)
{
	display_constraints constraints = {
		// resolution
		320, 4096, 200, 4096,
		// pixel clock
		gInfo->shared_info->pll_info.min_frequency,
		gInfo->shared_info->pll_info.max_frequency,
		// horizontal
		{1, 0, 8160, 32, 8192, 0, 8192},
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
	uint32 bytes_per_pixel = (sharedInfo.bits_per_pixel + 7) / 8;

	// Gen 6+ display offset handling - all supported GPUs fall into this category
	if (sharedInfo.device_type.InFamily(INTEL_FAMILY_SER5)
		|| sharedInfo.device_type.InFamily(INTEL_FAMILY_LAKE)
		|| sharedInfo.device_type.InFamily(INTEL_FAMILY_SOC0)) {
		if (sharedInfo.device_type.InGroup(INTEL_GROUP_HAS)) {
			write32(INTEL_DISPLAY_A_OFFSET_HAS + offset,
				((uint32)mode.v_display_start << 16)
					| (uint32)mode.h_display_start);
			read32(INTEL_DISPLAY_A_OFFSET_HAS + offset);
		} else {
			write32(INTEL_DISPLAY_A_BASE + offset,
				mode.v_display_start * sharedInfo.bytes_per_row
				+ mode.h_display_start * bytes_per_pixel);
			read32(INTEL_DISPLAY_A_BASE + offset);
		}
		write32(INTEL_DISPLAY_A_SURFACE + offset, sharedInfo.frame_buffer_offset);
		read32(INTEL_DISPLAY_A_SURFACE + offset);
	} else {
		ERROR("%s: Unsupported device family for framebuffer setup! "
			"Device type: 0x%x\n", __func__, sharedInfo.device_type.Type());
	}
}


void
set_frame_buffer_base()
{
	// TODO we always set both displays to the same address. When we support
	// multiple framebuffers, they should get different addresses here.
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

	// Gen 6+ doesn't support B_RGB15, use custom colorspace list
	const color_space kSupportedSpaces[] = {B_RGB32_LITTLE, B_RGB16_LITTLE,
		B_CMAP8};
	const color_space* supportedSpaces = kSupportedSpaces;
	int colorSpaceCount = B_COUNT_OF(kSupportedSpaces);

	// If no EDID, but have vbt from driver, use that mode
	if (!gInfo->has_edid && gInfo->shared_info->got_vbt) {
		// We could not read any EDID info. Fallback to creating a list with
		// only the mode set up by the BIOS.
		display_mode mode;
		mode.timing = gInfo->shared_info->panel_timing;
		mode.space = B_RGB32;
		mode.virtual_width = mode.timing.h_display;
		mode.virtual_height = mode.timing.v_display;
		mode.h_display_start = 0;
		mode.v_display_start = 0;
		mode.flags = 0;

		// TODO: support lower modes via scaling and windowing
		gInfo->mode_list_area = create_display_modes("intel extreme modes", NULL, &mode, 1,
			supportedSpaces, colorSpaceCount, NULL, &list, &count);
	} else {
		// Otherwise return the 'real' list of modes
		gInfo->mode_list_area = create_display_modes("intel extreme modes",
			gInfo->has_edid ? &gInfo->edid_info : NULL, NULL, 0,
			supportedSpaces, colorSpaceCount, NULL, &list, &count);
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
		// With the output turned off via DPMS, we might not get any interrupts
		// anymore that's why we don't wait forever for it. At 50Hz, we're sure
		// to get a vblank in at most 20ms, so there is no need to wait longer
		// than that.
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
	
	// Modeflags are outputs from us (the driver). Set them depending on
	// the mode and the current hardware config
	target->flags |= B_SCROLL;

	return is_display_mode_within_bounds(*target, *low, *high)
		? B_OK : B_BAD_VALUE;
}


status_t
intel_set_display_mode(display_mode* mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;

	TRACE("%s(%" B_PRIu16 "x%" B_PRIu16 ", virtual: %" B_PRIu16 "x%" B_PRIu16 ")\n", __func__,
		mode->timing.h_display, mode->timing.v_display, mode->virtual_width, mode->virtual_height);

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
		// Oh, how did that happen? Unfortunately, there is no really good way
		// back. Try to restore a framebuffer for the previous mode, at least.
		if (intel_allocate_memory(sharedInfo.current_mode.virtual_height
				* sharedInfo.bytes_per_row, 0, base) == B_OK) {
			sharedInfo.frame_buffer = base;
			sharedInfo.frame_buffer_offset = base
				- (addr_t)sharedInfo.graphics_memory;
			set_frame_buffer_base();
		}

		ERROR("%s: Failed to allocate framebuffer !\n", __func__);
		return B_NO_MEMORY;
	}

	// Clear frame buffer before using it
	memset((uint8*)base, 0, bytesPerRow * target.virtual_height);
	sharedInfo.frame_buffer = base;
	sharedInfo.frame_buffer_offset = base - (addr_t)sharedInfo.graphics_memory;

	// Make sure VGA display is disabled
	write32(INTEL_VGA_DISPLAY_CONTROL, VGA_DISPLAY_DISABLED);
	read32(INTEL_VGA_DISPLAY_CONTROL);

	// Go over each port and set the display mode
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

	// We set the same color mode across all pipes
	program_pipe_color_modes(colorMode);

	set_display_power_mode(sharedInfo.dpms_mode);

	// Always set both pipes, just in case
	// TODO rework this when we get multiple head support with different
	// resolutions
	if (sharedInfo.device_type.InFamily(INTEL_FAMILY_LAKE)) {
		write32(INTEL_DISPLAY_A_BYTES_PER_ROW, bytesPerRow >> 6);
		write32(INTEL_DISPLAY_B_BYTES_PER_ROW, bytesPerRow >> 6);
	} else {
		write32(INTEL_DISPLAY_A_BYTES_PER_ROW, bytesPerRow);
		write32(INTEL_DISPLAY_B_BYTES_PER_ROW, bytesPerRow);
	}

	// Update shared info
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


// Get the backlight registers. We need the backlight frequency (we never write it, but we need to
// know its value as the duty cycle/brightness level is proportional to it), and the duty cycle
// register (read to get the current backlight value, written to set it). On older generations,
// the two values are in the same register (16 bits each), on newer ones there are two separate
// registers.
static int32_t
intel_get_backlight_register(bool period)
{
	if (gInfo->shared_info->pch_info >= INTEL_PCH_CNP) {
		return period ? PCH_SOUTH_BLC_PWM_PERIOD : PCH_SOUTH_BLC_PWM_DUTY_CYCLE;
	} else if (gInfo->shared_info->pch_info >= INTEL_PCH_SPT) {
		return BLC_PWM_PCH_CTL2;
	}

	if (gInfo->shared_info->pch_info == INTEL_PCH_NONE)
		return MCH_BLC_PWM_CTL;

	return period ? PCH_SOUTH_BLC_PWM_PERIOD : PCH_BLC_PWM_CTL;
}


status_t
intel_set_brightness(float brightness)
{
	CALLED();

	if (brightness < 0 || brightness > 1)
		return B_BAD_VALUE;

	// The "duty cycle" is a proportion of the period (0 = backlight off,
	// period = maximum brightness).
	// Additionally we don't want it to be completely 0 here, because then
	// it becomes hard to turn the display on again (at least until we get
	// working ACPI keyboard shortcuts for this). So always keep the backlight
	// at least a little bit on for now.

	if (gInfo->shared_info->pch_info >= INTEL_PCH_CNP) {
		uint32_t period = read32(intel_get_backlight_register(true));
		uint32_t duty = (uint32_t)(period * brightness);
		duty = std::max(duty, (uint32_t)gInfo->shared_info->min_brightness);

		write32(intel_get_backlight_register(false), duty);
	} else if (gInfo->shared_info->pch_info >= INTEL_PCH_SPT) {
		uint32_t period = read32(intel_get_backlight_register(true)) >> 16;
		uint32_t duty = (uint32_t)(period * brightness) & 0xffff;
		duty = std::max(duty, (uint32_t)gInfo->shared_info->min_brightness);

		write32(intel_get_backlight_register(false), duty | (period << 16));
	} else {
		// For older Gen 6+ PCH (IBX, CPT, LPT, etc.) or no PCH
		uint32_t tmp = read32(intel_get_backlight_register(false));
		uint32_t period = tmp >> 16;
		uint32_t duty = (uint32_t)(period * brightness) & 0xffff;
		duty = std::max(duty, (uint32_t)gInfo->shared_info->min_brightness);

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

	uint32_t duty;
	uint32_t period;

	if (gInfo->shared_info->pch_info >= INTEL_PCH_CNP) {
		period = read32(intel_get_backlight_register(true));
		duty = read32(intel_get_backlight_register(false));
	} else if (gInfo->shared_info->pch_info >= INTEL_PCH_SPT) {
		uint32 tmp = read32(intel_get_backlight_register(true));
		period = tmp >> 16;
		duty = tmp & 0xffff;
	} else {
		// For older Gen 6+ PCH (IBX, CPT, LPT, etc.) or no PCH
		uint32 tmp = read32(intel_get_backlight_register(false));
		period = tmp >> 16;
		duty = tmp & 0xffff;
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
