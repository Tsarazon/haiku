/*
 * Copyright 2012-2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *	Alexander von Gluck IV, kallisti5@unixzen.com
 *
 * Refactored for Gen9+ only support (Mobile Haiku)
 *
 * TODO: Implement proper Gen9+ power management following PRM
 */


#include "power.h"

#undef TRACE
#define TRACE(x...) dprintf("intel_extreme: " x)
#define ERROR(x...) dprintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


/*
 * Clock gating for Gen9+
 *
 * ⚠️ Gen9+ clock gating is mostly handled by hardware/firmware
 * These are minimal workarounds if needed
 */
status_t
intel_en_gating(intel_info &info)
{
	CALLED();

	uint32 generation = info.device_type.Generation();

	if (generation < 9) {
		ERROR("%s: Generation %d not supported (Gen9+ only)\n",
			__func__, generation);
		return B_NOT_SUPPORTED;
	}

	/*
	 * ⚠️ Gen9+ clock gating notes from PRM:
	 *
	 * Skylake (Gen9):
	 *   - Most clock gating is automatic
	 *   - Some workarounds may be needed for specific issues
	 *
	 * Gen11+:
	 *   - Clock gating handled by Display Engine
	 *   - GuC firmware may handle additional gating
	 *
	 * Current implementation is minimal - extend as needed
	 * following Intel PRM workaround lists.
	 */

	if (generation == 9) {
		/*
		 * Gen9 minimal clock gating setup
		 * Most gating is automatic on Gen9+
		 */
		write32(info, 0x7408, 0x10);
		TRACE("Gen9 minimal clock gating enabled\n");
	}

	return B_OK;
}


/*
 * Power state downclocking for Gen9+
 *
 * ⚠️ Gen9+ RC6 is very different from Gen6-8
 * It's largely handled by GuC firmware when available
 *
 * TODO: Implement proper Gen9+ RC6 following PRM sequences
 */
status_t
intel_en_downclock(intel_info &info)
{
	CALLED();

	uint32 generation = info.device_type.Generation();

	if (generation < 9) {
		ERROR("%s: Generation %d not supported (Gen9+ only)\n",
			__func__, generation);
		return B_NOT_SUPPORTED;
	}

	if ((info.device_type.type & INTEL_TYPE_MOBILE) == 0) {
		/* Skip auto-downclocking on non-mobile devices */
		TRACE("%s: Skip GPU downclocking on non-mobile device.\n", __func__);
		return B_NOT_ALLOWED;
	}

	/*
	 * ⚠️ Gen9+ RC6 implementation notes from PRM:
	 *
	 * Skylake/Kaby Lake (Gen9/9.5):
	 *   - RC6 supported but requires different register sequence
	 *   - GuC firmware preferred for power management
	 *   - SLPC (Single Loop Power Controller) in GuC
	 *
	 * Ice Lake+ (Gen11+):
	 *   - Power management primarily via GuC SLPC
	 *   - Host-based fallback available but not recommended
	 *
	 * Current implementation is a stub - RC6 for Gen9+ requires:
	 *   1. Proper forcewake handling
	 *   2. GuC firmware loading (preferred)
	 *   3. Or host-based RC6 with correct PRM sequences
	 *
	 * For now, we rely on BIOS/UEFI default power settings.
	 */

	TRACE("%s: Gen9+ power management - using BIOS defaults\n", __func__);

	/*
	 * TODO: Implement one of:
	 *   a) GuC-based SLPC for automatic power management
	 *   b) Host-based RC6 following PRM for Gen9+
	 *
	 * For mobile devices, this could significantly improve
	 * battery life. Implementation would need:
	 *   - Forcewake domain handling
	 *   - GT frequency management
	 *   - RC6 residency monitoring
	 */

	return B_OK;
}


/*
 * Future: Gen9+ specific power management functions
 *
 * These would be needed for proper power management:
 *
 * status_t intel_gen9_setup_rc6(intel_info &info);
 * status_t intel_gen11_setup_rc6(intel_info &info);
 * status_t intel_setup_guc_slpc(intel_info &info);
 * status_t intel_forcewake_get(intel_info &info, uint32 domains);
 * status_t intel_forcewake_put(intel_info &info, uint32 domains);
 */
