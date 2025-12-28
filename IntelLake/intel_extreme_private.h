/*
 * Copyright 2006-2018, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Refactored for Gen9+ only support (Mobile Haiku)
 *
 * REMOVED (Gen < 9):
 *   - Legacy interrupt register lookup (INTEL_INTERRUPT_*, PCH_INTERRUPT_*)
 *   - find_reg() function - not needed for Gen9+ PCH-only architecture
 *
 * Gen9+ uses direct register access via read32/write32 with
 * fixed offsets defined in intel_extreme.h
 */
#ifndef INTEL_EXTREME_PRIVATE_H
#define INTEL_EXTREME_PRIVATE_H


#include <AGP.h>
#include <KernelExport.h>
#include <PCI.h>

#include "intel_extreme.h"
#include "lock.h"


/*
 * Gen9+ register access
 *
 * Gen9+ always uses PCH split architecture with fixed register bases.
 * No register lookup needed - all offsets are constant.
 *
 * Register access is done via read32/write32 defined in driver.h
 * using the register base + offset from intel_extreme.h
 */


/*
 * Gen9+ PCI Configuration
 *
 * ✅ Verified against Intel PRM Vol 2c
 */
#define INTEL_GEN9_MMIO_BAR			0	/* BAR0 for MMIO */
#define INTEL_GEN9_GTTMMADR_BAR		0	/* Same as MMIO */
#define INTEL_GEN9_APERTURE_BAR		2	/* BAR2 for graphics aperture */


/*
 * Gen9+ GTT (Graphics Translation Table)
 *
 * ✅ PRM: GTT is part of MMIO space for Gen9+
 */
#define INTEL_GEN9_GTT_OFFSET		0x800000
#define INTEL_GEN9_GTT_SIZE			0x800000	/* 8MB max */


/*
 * Gen9+ Forcewake domains
 *
 * ⚠️ Required for proper register access in some power states
 * TODO: Implement forcewake handling
 */
#define INTEL_GEN9_FORCEWAKE_RENDER		(1 << 0)
#define INTEL_GEN9_FORCEWAKE_BLITTER	(1 << 1)
#define INTEL_GEN9_FORCEWAKE_MEDIA		(1 << 2)


/*
 * Gen9+ Display power well domains
 *
 * ✅ PRM: Power well control for display engine
 */
#define INTEL_GEN9_PW_CTL_IDX_DDI_A		0
#define INTEL_GEN9_PW_CTL_IDX_DDI_B		1
#define INTEL_GEN9_PW_CTL_IDX_DDI_C		2
#define INTEL_GEN9_PW_CTL_IDX_DDI_D		3
#define INTEL_GEN9_PW_CTL_IDX_DDI_E		4
#define INTEL_GEN9_PW_CTL_IDX_DDI_F		5


/*
 * Gen9+ DPCD (DisplayPort Configuration Data) helpers
 */
#define INTEL_DP_LINK_RATE_HBR3		810000	/* 8.1 Gbps */
#define INTEL_DP_LINK_RATE_HBR2		540000	/* 5.4 Gbps */
#define INTEL_DP_LINK_RATE_HBR		270000	/* 2.7 Gbps */
#define INTEL_DP_LINK_RATE_RBR		162000	/* 1.62 Gbps */


/*
 * Gen9+ DSC (Display Stream Compression)
 *
 * Gen11+ only
 */
#define INTEL_DSC_ENABLE			(1 << 0)
#define INTEL_DSC_DUAL_LINK			(1 << 1)


/*
 * Inline helper for checking generation
 */
static inline bool
intel_is_gen11_plus(const intel_info& info)
{
	return info.device_type.Generation() >= 11;
}

static inline bool
intel_is_gen12_plus(const intel_info& info)
{
	return info.device_type.Generation() >= 12;
}

static inline bool
intel_has_pipe_d(const intel_info& info)
{
	return info.device_type.Generation() >= 12;
}


/*
 * Gen9+ PCH type helpers
 */
static inline bool
intel_has_icp_plus_pch(const intel_info& info)
{
	return info.pch_info >= INTEL_PCH_ICP;
}

static inline bool
intel_has_tgp_plus_pch(const intel_info& info)
{
	return info.pch_info >= INTEL_PCH_TGP;
}


/*
 * Gen9+ DDI port helpers
 */
#define DDI_PORT_COUNT_GEN9		4	/* DDI A-D */
#define DDI_PORT_COUNT_GEN11	6	/* DDI A-F + TC1-6 */
#define DDI_PORT_COUNT_GEN12	9	/* DDI A + TC1-6 + USBC1-4 */


#endif /* INTEL_EXTREME_PRIVATE_H */
