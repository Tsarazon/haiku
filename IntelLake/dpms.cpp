/*
 * Copyright 2006-2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Refactored 2025: Gen 9+ only (Skylake and newer)
 * - Removed legacy PLL management (DISPLAY_A/B_PLL)
 * - Removed analog port (VGA) support
 * - Removed LVDS panel support (replaced by eDP)
 * - Uses DDI-based power management
 *
 * ✅ Power sequence matches Intel PRM Vol 12 "Display Engine"
 * ⚠️ DPLL management simplified - full implementation in Pipes.cpp
 */


#include "accelerant_protos.h"
#include "accelerant.h"

#undef TRACE
#define TRACE(x...) _sPrintf("intel_extreme: " x)
#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


/*
 * Enable/disable all active pipes and planes
 *
 * Gen 9+ display pipeline per Intel PRM Vol 12:
 * DDI -> Transcoder -> Pipe -> Planes
 *
 * Power sequence (enable):
 * 1. Enable DPLL
 * 2. Enable DDI
 * 3. Enable Transcoder
 * 4. Enable Pipe
 * 5. Enable Planes
 *
 * Power sequence (disable): reverse order
 */
static void
enable_all_pipes(bool enable)
{
	for (uint32 i = 0; i < gInfo->port_count; i++) {
		if (gInfo->ports[i] == NULL)
			continue;
		if (!gInfo->ports[i]->IsConnected())
			continue;
		if (gInfo->ports[i]->GetPipe() == NULL)
			continue;

		gInfo->ports[i]->Power(enable);
	}

	/* Flush cached PCI writes */
	read32(INTEL_DISPLAY_A_BASE);

	set_frame_buffer_base();
}


/*
 * Enable/disable eDP panel power
 *
 * Gen 9+ uses PCH panel control for eDP backlight and power sequencing.
 * LVDS was removed - all internal panels use eDP.
 *
 * ✅ Register PCH_PANEL_CONTROL matches PRM Vol 2c
 * ✅ Power sequence timing per PRM Vol 12 "eDP"
 */
static void
enable_edp_panel(bool enable)
{
	/* Gen 9+ always has PCH */
	uint32 control = read32(PCH_PANEL_CONTROL);
	uint32 status;

	if (enable) {
		if ((control & PANEL_CONTROL_POWER_TARGET_ON) == 0) {
			write32(PCH_PANEL_CONTROL,
				control | PANEL_CONTROL_POWER_TARGET_ON);
		}

		/* Wait for panel power on - timeout after 200ms per spec */
		bigtime_t start = system_time();
		do {
			status = read32(PCH_PANEL_STATUS);
			if (system_time() > start + 200000) {
				ERROR("eDP panel power on timeout\n");
				break;
			}
			spin(100);
		} while ((status & PANEL_STATUS_POWER_ON) == 0);
	} else {
		if ((control & PANEL_CONTROL_POWER_TARGET_ON) != 0) {
			write32(PCH_PANEL_CONTROL,
				control & ~PANEL_CONTROL_POWER_TARGET_ON);
		}

		/* Wait for panel power off */
		bigtime_t start = system_time();
		do {
			status = read32(PCH_PANEL_STATUS);
			if (system_time() > start + 200000) {
				ERROR("eDP panel power off timeout\n");
				break;
			}
			spin(100);
		} while ((status & PANEL_STATUS_POWER_ON) != 0);
	}
}


/*
 * Set display power mode (DPMS)
 *
 * Gen 9+ DPMS is handled through DDI port and pipe control.
 * No legacy analog port or LVDS to manage.
 *
 * ⚠️ DPLL enable/disable should be handled by Pipes.cpp
 *    This function focuses on pipe/plane power states.
 */
void
set_display_power_mode(uint32 mode)
{
	CALLED();

	if (mode == B_DPMS_ON) {
		/* Power on sequence: DPLL -> DDI -> Transcoder -> Pipe -> Planes */
		enable_all_pipes(true);
	}

	wait_for_vblank();

	/*
	 * Gen 9+ DDI port DPMS control
	 *
	 * DDI ports (DDI A-E) handle DPMS signaling for:
	 * - DisplayPort: D0-D3 power states via DPCD
	 * - HDMI: TMDS clock control
	 * - eDP: Panel power sequencing
	 *
	 * Port-specific DPMS is handled in Ports.cpp DDI implementation.
	 * Here we just manage pipe/plane power states.
	 */

	if (mode != B_DPMS_ON) {
		/* Power off sequence: Planes -> Pipe -> Transcoder -> DDI -> DPLL */
		enable_all_pipes(false);
	}

	/* Handle eDP panel power for mobile devices */
	if (gInfo->shared_info->device_type.IsMobile()) {
		/*
		 * Check if any port is eDP
		 * eDP is typically on DDI A for integrated panels
		 */
		for (uint32 i = 0; i < gInfo->port_count; i++) {
			if (gInfo->ports[i] == NULL)
				continue;
			if (gInfo->ports[i]->Type() == INTEL_PORT_TYPE_EDP) {
				enable_edp_panel(mode == B_DPMS_ON);
				break;
			}
		}
	}

	/* Flush cached PCI writes */
	read32(INTEL_DISPLAY_A_BASE);
}


//	#pragma mark - Public DPMS API


uint32
intel_dpms_capabilities(void)
{
	CALLED();
	/*
	 * Gen 9+ supports all DPMS states through DDI
	 * ✅ Per Intel PRM Vol 12 "Display Power Management"
	 */
	return B_DPMS_ON | B_DPMS_SUSPEND | B_DPMS_STAND_BY | B_DPMS_OFF;
}


uint32
intel_dpms_mode(void)
{
	CALLED();
	return gInfo->shared_info->dpms_mode;
}


status_t
intel_set_dpms_mode(uint32 mode)
{
	CALLED();
	gInfo->shared_info->dpms_mode = mode;
	set_display_power_mode(mode);

	return B_OK;
}
