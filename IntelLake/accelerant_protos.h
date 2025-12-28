/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Gen 9+ only - removed legacy 2D blitter, overlay, ring buffer engine.
 */
#ifndef ACCELERANT_PROTOS_H
#define ACCELERANT_PROTOS_H


#include <Accelerant.h>


#ifdef __cplusplus
extern "C" {
#endif

void spin(bigtime_t delay);

// general
status_t intel_init_accelerant(int fd);
ssize_t intel_accelerant_clone_info_size(void);
void intel_get_accelerant_clone_info(void* data);
status_t intel_clone_accelerant(void* data);
void intel_uninit_accelerant(void);
status_t intel_get_accelerant_device_info(accelerant_device_info* info);
sem_id intel_accelerant_retrace_semaphore(void);

// modes & constraints
uint32 intel_accelerant_mode_count(void);
status_t intel_get_mode_list(display_mode* dm);
status_t intel_propose_display_mode(display_mode* target,
	const display_mode* low, const display_mode* high);
status_t intel_get_preferred_mode(display_mode* preferredMode);
status_t intel_set_display_mode(display_mode* mode);
status_t intel_get_display_mode(display_mode* currentMode);
status_t intel_get_edid_info(void* info, size_t size, uint32* _version);
status_t intel_set_brightness(float brightness);
status_t intel_get_brightness(float* brightness);
status_t intel_get_frame_buffer_config(frame_buffer_config* config);
status_t intel_get_pixel_clock_limits(display_mode* mode, uint32* low,
	uint32* high);
status_t intel_move_display(uint16 hDisplayStart, uint16 vDisplayStart);
status_t intel_get_timing_constraints(display_timing_constraints* constraints);
void intel_set_indexed_colors(uint count, uint8 first, uint8* colorData,
	uint32 flags);

// DPMS
uint32 intel_dpms_capabilities(void);
uint32 intel_dpms_mode(void);
status_t intel_set_dpms_mode(uint32 flags);

// cursor
status_t intel_set_cursor_shape(uint16 width, uint16 height, uint16 hotX,
	uint16 hotY, uint8* andMask, uint8* xorMask);
void intel_move_cursor(uint16 x, uint16 y);
void intel_show_cursor(bool isVisible);

// Gen 9+: No 2D blitter acceleration (removed in hardware)
// Gen 9+: No legacy overlay (replaced by Universal Planes)
// Gen 9+: No legacy ring buffer engine API (uses Execlist)

#ifdef __cplusplus
}
#endif

#endif	/* ACCELERANT_PROTOS_H */
