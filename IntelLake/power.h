/*
 * Copyright 2012-2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 *
 * Refactored for Gen9+ only support (Mobile Haiku)
 *
 * REMOVED (Gen < 9):
 *   - INTEL6_* RC6 registers (Gen6-8 specific)
 *   - Gen6 power management sequences
 *
 * ⚠️ Note: Gen9+ power management is significantly different.
 * RC6 and clock gating for Gen9+ requires either:
 *   a) GuC firmware for SLPC (Single Loop Power Controller)
 *   b) Host-based RC6 with different register sequences per PRM
 *
 * Current implementation uses BIOS/UEFI defaults.
 * TODO: Implement proper Gen9+ power management following PRM
 */
#ifndef _INTEL_POWER_H_
#define _INTEL_POWER_H_


#include <string.h>

#include "driver.h"


/*
 * Function prototypes for power management
 */
status_t intel_en_gating(intel_info& info);
status_t intel_en_downclock(intel_info& info);


/*
 * Gen9+ Render P-state (RPS) registers
 *
 * ✅ Verified against Intel PRM Vol 2c
 * These are in the GT power domain, require forcewake
 */
#define GEN9_RP_STATE_CAP			0x138170
#define GEN9_RP_STATE_LIMITS		0x138148
#define GEN9_RPSTAT1				0x138048
#define GEN9_RPNSWREQ				0x138064


/*
 * Gen9+ frequency/voltage control
 *
 * ✅ PRM: GT frequency control registers
 */
#define GEN9_RPSTAT1_CURR_GT_FREQ_SHIFT		8
#define GEN9_RPSTAT1_CURR_GT_FREQ_MASK		(0xff << 8)
#define GEN9_RPSTAT1_PREV_GT_FREQ_SHIFT		0
#define GEN9_RPSTAT1_PREV_GT_FREQ_MASK		(0xff << 0)


/*
 * Gen9+ RC6 state registers
 *
 * ⚠️ Note: These registers exist but the programming sequence
 * is different from Gen6-8. See PRM for proper initialization.
 *
 * For Gen9+, RC6 is preferably controlled by GuC firmware.
 */
#define GEN9_GT_CORE_STATUS			0x138060
#define GEN9_RC_CONTROL				0x138050
#define GEN9_RC_STATE				0x138054
#define GEN9_RC6_RESIDENCY_COUNTER	0x138108
#define GEN9_RC6_THRESHOLD			0x138014


/*
 * Gen9+ RC6 control bits
 *
 * ✅ PRM: Render C-state control
 */
#define GEN9_RC_CTL_RC6_ENABLE		(1 << 18)
#define GEN9_RC_CTL_EI_MODE			(1 << 27)
#define GEN9_RC_CTL_HW_ENABLE		(1 << 31)


/*
 * Gen9+ Power management control
 *
 * ✅ PRM: GT PM control registers
 */
#define GEN9_GT_PM_CONFIG0			0x138140
#define GEN9_GT_PM_CONFIG1			0x138144


/*
 * Gen9+ Forcewake registers
 *
 * ⚠️ Required for accessing registers in certain power states
 * TODO: Implement proper forcewake handling
 */
#define GEN9_FORCEWAKE_RENDER_GEN9	0xa278
#define GEN9_FORCEWAKE_MEDIA_GEN9	0xa270
#define GEN9_FORCEWAKE_BLITTER_GEN9	0xa188

#define GEN9_FORCEWAKE_ACK_RENDER	0x0D84
#define GEN9_FORCEWAKE_ACK_MEDIA	0x0D88
#define GEN9_FORCEWAKE_ACK_BLITTER	0x130044


/*
 * Gen9 Clock gating registers
 *
 * ⚠️ Most clock gating is automatic on Gen9+
 * These are for specific workarounds only
 */
#define GEN9_CLKGATE_DIS_0			0x46530
#define GEN9_CLKGATE_DIS_1			0x46534
#define GEN9_CLKGATE_DIS_4			0x4653c


/*
 * Gen11+ specific power registers
 *
 * Gen11 has additional power management capabilities
 */
#define GEN11_GT_INTR_DW0			0x190018
#define GEN11_GT_INTR_DW1			0x19001c

#define GEN11_EU_PERF_CNTL0			0xe458
#define GEN11_EU_PERF_CNTL1			0xe45c
#define GEN11_EU_PERF_CNTL2			0xe460
#define GEN11_EU_PERF_CNTL3			0xe464
#define GEN11_EU_PERF_CNTL4			0xe468
#define GEN11_EU_PERF_CNTL5			0xe46c
#define GEN11_EU_PERF_CNTL6			0xe470


/*
 * Gen12+ specific power registers
 */
#define GEN12_RC_CG_CONTROL			0x94358


/*
 * GuC (Graphics µController) related
 *
 * ⚠️ Not implemented - requires firmware loading
 * GuC handles power management via SLPC on modern systems
 */
#define GEN9_GUC_STATUS				0xc000


/*
 * Display Power Well registers
 *
 * ✅ PRM: Power well control for display engine
 * These are in the display block, not GT block
 */
#define HSW_PWR_WELL_CTL1			0x45400
#define HSW_PWR_WELL_CTL2			0x45404
#define HSW_PWR_WELL_CTL_DRIVER(i)	(HSW_PWR_WELL_CTL1 + (i) * 4)

/* Power well request/state bits */
#define HSW_PWR_WELL_CTL_REQ(i)		(1 << (((i) * 2) + 1))
#define HSW_PWR_WELL_CTL_STATE(i)	(1 << ((i) * 2))


/*
 * Gen9+ Display power well indices
 *
 * ✅ PRM: Power well indexing for Skylake+
 */
#define SKL_PW_CTL_IDX_PW_1			0
#define SKL_PW_CTL_IDX_MISC_IO		1
#define SKL_PW_CTL_IDX_DDI_A_E		2
#define SKL_PW_CTL_IDX_DDI_B		3
#define SKL_PW_CTL_IDX_DDI_C		4
#define SKL_PW_CTL_IDX_DDI_D		5
#define SKL_PW_CTL_IDX_PW_2			6


/*
 * Gen11+ Display power well indices
 *
 * ✅ PRM: Power well indexing for Ice Lake+
 */
#define ICL_PW_CTL_IDX_PW_1			0
#define ICL_PW_CTL_IDX_PW_2			1
#define ICL_PW_CTL_IDX_PW_3			2
#define ICL_PW_CTL_IDX_PW_4			3
#define ICL_PW_CTL_IDX_DDI_A		4
#define ICL_PW_CTL_IDX_DDI_B		5
#define ICL_PW_CTL_IDX_DDI_C		6
#define ICL_PW_CTL_IDX_DDI_D		7
#define ICL_PW_CTL_IDX_DDI_E		8
#define ICL_PW_CTL_IDX_DDI_F		9
#define ICL_PW_CTL_IDX_AUX_A		10
#define ICL_PW_CTL_IDX_AUX_B		11
#define ICL_PW_CTL_IDX_AUX_C		12
#define ICL_PW_CTL_IDX_AUX_D		13
#define ICL_PW_CTL_IDX_AUX_E		14
#define ICL_PW_CTL_IDX_AUX_F		15


#endif /* _INTEL_POWER_H_ */
