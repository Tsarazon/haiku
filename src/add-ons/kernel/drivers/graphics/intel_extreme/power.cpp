/*
 * Copyright 2012-2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *	Alexander von Gluck IV, kallisti5@unixzen.com
 */


#include "power.h"

#undef TRACE
#define TRACE(x...) dprintf("intel_extreme: " x)
#define ERROR(x...) dprintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


status_t
intel_en_gating(intel_info &info)
{
	CALLED();
	// Gen 6+ clock gating
	if (info.device_type.InGroup(INTEL_GROUP_SNB)) {
		TRACE("SandyBridge clock gating\n");
		write32(info, 0x42020, (1L << 28) | (1L << 7) | (1L << 5));
	} else if (info.device_type.InGroup(INTEL_GROUP_IVB)) {
		TRACE("IvyBridge clock gating\n");
		write32(info, 0x42020, (1L << 28));
	} else if (info.device_type.InGroup(INTEL_GROUP_VLV)) {
		TRACE("ValleyView clock gating\n");
		write32(info, VLV_DISPLAY_BASE + 0x6200, (1L << 28));
	}
	write32(info, 0x7408, 0x10);

	return B_OK;
}


status_t
intel_en_downclock(intel_info &info)
{
	CALLED();

	if (!info.device_type.InGroup(INTEL_GROUP_SNB)
		&& !info.device_type.InGroup(INTEL_GROUP_IVB)) {
		TRACE("%s: Downclocking not supported on this chipset.\n", __func__);
		return B_NOT_ALLOWED;
	}

	if((info.device_type.type & INTEL_TYPE_MOBILE) == 0) {
		// I don't see a point enabling auto-downclocking on non-mobile devices.
		TRACE("%s: Skip GPU downclocking on non-mobile device.\n", __func__);
		return B_NOT_ALLOWED;
	}
	// TODO: Check for deep RC6
	// IvyBridge, SandyBridge, and Haswell can do depth 1 atm
	// Some chipsets can go deeper... but this is safe for now
	// Haswell should *NOT* do over depth 1;
	int depth = 1;

	// Lets always print this for now incase it causes regressions for someone.
	ERROR("%s: Enabling Intel GPU auto downclocking depth %d\n", __func__,
		depth);

	/* Magical sequence of register writes to enable
	 * downclocking from the fine folks at Xorg
	 */
	write32(info, INTEL6_RC_STATE, 0);

	uint32 rpStateCapacity = read32(info, INTEL6_RP_STATE_CAP);
	uint32 gtPerfStatus = read32(info, INTEL6_GT_PERF_STATUS);
	uint8 maxDelay = rpStateCapacity & 0xff;
	uint8 minDelay = (rpStateCapacity & 0xff0000) >> 16;

	write32(info, INTEL6_RC_CONTROL, 0);

	write32(info, INTEL6_RC1_WAKE_RATE_LIMIT, 1000 << 16);
	write32(info, INTEL6_RC6_WAKE_RATE_LIMIT, 40 << 16 | 30);
	write32(info, INTEL6_RC6pp_WAKE_RATE_LIMIT, 30);
	write32(info, INTEL6_RC_EVALUATION_INTERVAL, 125000);
	write32(info, INTEL6_RC_IDLE_HYSTERSIS, 25);

	// TODO: Idle each ring

	write32(info, INTEL6_RC_SLEEP, 0);
	write32(info, INTEL6_RC1e_THRESHOLD, 1000);
	write32(info, INTEL6_RC6_THRESHOLD, 50000);
	write32(info, INTEL6_RC6p_THRESHOLD, 100000);
	write32(info, INTEL6_RC6pp_THRESHOLD, 64000);

	uint32 rc6Mask = INTEL6_RC_CTL_RC6_ENABLE;

	if (depth > 1)
		rc6Mask |= INTEL6_RC_CTL_RC6p_ENABLE;
	if (depth > 2)
		rc6Mask |= INTEL6_RC_CTL_RC6pp_ENABLE;

	write32(info, INTEL6_RC_CONTROL, rc6Mask | INTEL6_RC_CTL_EI_MODE(1)
		| INTEL6_RC_CTL_HW_ENABLE);
	write32(info, INTEL6_RPNSWREQ, INTEL6_FREQUENCY(10) | INTEL6_OFFSET(0)
		| INTEL6_AGGRESSIVE_TURBO);
	write32(info, INTEL6_RC_VIDEO_FREQ, INTEL6_FREQUENCY(12));

	write32(info, INTEL6_RP_DOWN_TIMEOUT, 1000000);
	write32(info, INTEL6_RP_INTERRUPT_LIMITS, maxDelay << 24 | minDelay << 16);

	write32(info, INTEL6_RP_UP_THRESHOLD, 59400);
	write32(info, INTEL6_RP_DOWN_THRESHOLD, 245000);
	write32(info, INTEL6_RP_UP_EI, 66000);
	write32(info, INTEL6_RP_DOWN_EI, 350000);

	write32(info, INTEL6_RP_IDLE_HYSTERSIS, 10);
	write32(info, INTEL6_RP_CONTROL, INTEL6_RP_MEDIA_TURBO
		| INTEL6_RP_MEDIA_HW_NORMAL_MODE | INTEL6_RP_MEDIA_IS_GFX
		| INTEL6_RP_ENABLE | INTEL6_RP_UP_BUSY_AVG
		| INTEL6_RP_DOWN_IDLE_CONT);
		// TODO: | (HASWELL ? GEN7_RP_DOWN_IDLE_AVG : INTEL6_RP_DOWN_IDLE_CONT));

	// TODO: wait for (read32(INTEL6_PCODE_MAILBOX) & INTEL6_PCODE_READY)
	write32(info, INTEL6_PCODE_DATA, 0);
	write32(info, INTEL6_PCODE_MAILBOX, INTEL6_PCODE_READY
		| INTEL6_PCODE_WRITE_MIN_FREQ_TABLE);
	// TODO: wait for (read32(INTEL6_PCODE_MAILBOX) & INTEL6_PCODE_READY)

	// TODO: check for overclock support and set.

	// Calculate limits and enforce them
	uint8 gtPerfShift = (gtPerfStatus & 0xff00) >> 8;
	if (gtPerfShift >= maxDelay)
		gtPerfShift = maxDelay;
	uint32 limits = maxDelay << 24;
	if (gtPerfShift <= minDelay) {
		gtPerfShift = minDelay;
		limits |= minDelay << 16;
	}
	write32(info, INTEL6_RP_INTERRUPT_LIMITS, limits);

	write32(info, INTEL6_RPNSWREQ, INTEL6_FREQUENCY(gtPerfShift)
		| INTEL6_OFFSET(0) | INTEL6_AGGRESSIVE_TURBO);

	// Requires MSI to be enabled.
	write32(info, INTEL6_PMIER, INTEL6_PM_DEFERRED_EVENTS);
	// TODO: Review need for spin lock irq rps here?
	write32(info, INTEL6_PMIMR, 0);
	// TODO: Review need for spin unlock irq rps here?
	write32(info, INTEL6_PMINTRMSK, 0);

	return B_OK;
}
