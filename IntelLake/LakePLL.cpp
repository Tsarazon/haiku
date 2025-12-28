/*
 * Copyright 2006-2024, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 *		Adrien Destugues, pulkomandy@pulkomandy.tk
 *
 * Mobile Haiku: Unified PLL support for Gen 9+ Intel GPUs.
 *
 * This file contains PLL calculation and programming for all supported
 * Intel GPU generations:
 *
 *   Gen 9 / 9.5: Skylake, Kaby Lake, Coffee Lake
 *                Apollo Lake, Gemini Lake (Atom)
 *
 *   Gen 11:      Ice Lake, Elkhart Lake, Jasper Lake (Atom)
 *
 *   Gen 12+:     Tiger Lake, Alder Lake, Raptor Lake
 *                Alder Lake-N (Atom)
 *
 * The "Lake" naming reflects Intel's product naming convention for these
 * generations, all of which share similar PLL architectures based on
 * DCO (Digitally Controlled Oscillator) with programmable dividers.
 */


#include "LakePLL.h"

#include "accelerant.h"

#include <math.h>
#include <string.h>


#undef TRACE
#define TRACE(...) _sPrintf("intel_extreme: " __VA_ARGS__)
#define ERROR(...) _sPrintf("intel_extreme: " __VA_ARGS__)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)



// Common Utilities


static inline uint64
abs_diff64(uint64 a, uint64 b)
{
	return (a >= b) ? (a - b) : (b - a);
}


/*
 * Get effective reference clock for PLL calculations.
 *
 * For ICL+, the hardware automatically divides 38.4 MHz by 2.
 * Reference: IHD-OS-ICL-Vol 12-1.20, "Display PLLs"
 */
int
get_effective_ref_clock()
{
	int ref_khz = gInfo->shared_info->pll_info.reference_frequency;
	int generation = gInfo->shared_info->device_type.Generation();

	// ICL+ auto-divides 38.4 MHz reference to 19.2 MHz
	if (generation >= 11 && ref_khz == 38400)
		ref_khz = 19200;

	return ref_khz;
}



// Skylake WRPLL (Gen 9 / 9.5)

//
// The WRPLL (Wrapped PLL) on Skylake uses a DCO that can be tuned to
// frequencies around three "central" frequencies (8.4, 9.0, 9.6 GHz).
// The DCO output is divided by P0 * P1 * P2 to produce the AFE clock,
// which is then divided by 5 to get the symbol/pixel clock.
//
// Reference: Intel IHD-OS-SKL-Vol 12-05.16, "Display PLLs"
//
// Portions from Linux i915 intel_dpll_mgr.c:
// Copyright © 2006-2016 Intel Corporation (MIT License)


struct skl_wrpll_context {
	uint64		min_deviation;
	uint64		central_freq;
	uint64		dco_freq;
	unsigned	p;
};


// DCO deviation limits: +1% / -6% from central frequency
#define SKL_DCO_MAX_PDEVIATION	100		// +1.00% in 0.01% units
#define SKL_DCO_MAX_NDEVIATION	600		// -6.00% in 0.01% units


static void
skl_wrpll_try_divider(struct skl_wrpll_context* ctx, uint64 central_freq,
	uint64 dco_freq, unsigned divider)
{
	uint64 deviation = 10000 * abs_diff64(dco_freq, central_freq) / central_freq;

	if (dco_freq >= central_freq) {
		if (deviation < SKL_DCO_MAX_PDEVIATION
			&& deviation < ctx->min_deviation) {
			ctx->min_deviation = deviation;
			ctx->central_freq = central_freq;
			ctx->dco_freq = dco_freq;
			ctx->p = divider;
		}
	} else {
		if (deviation < SKL_DCO_MAX_NDEVIATION
			&& deviation < ctx->min_deviation) {
			ctx->min_deviation = deviation;
			ctx->central_freq = central_freq;
			ctx->dco_freq = dco_freq;
			ctx->p = divider;
		}
	}
}


static void
skl_wrpll_get_multipliers(unsigned p, unsigned* p0, unsigned* p1, unsigned* p2)
{
	// Convert total divider P into P0 (pdiv), P1 (qdiv), P2 (kdiv)
	// Reference: IHD-OS-SKL-Vol 12-05.16, page 171

	if (p % 2 == 0) {
		unsigned half = p / 2;
		if (half == 1 || half == 2 || half == 3 || half == 5) {
			*p0 = 2; *p1 = 1; *p2 = half;
		} else if (half % 2 == 0) {
			*p0 = 2; *p1 = half / 2; *p2 = 2;
		} else if (half % 3 == 0) {
			*p0 = 3; *p1 = half / 3; *p2 = 2;
		} else if (half % 7 == 0) {
			*p0 = 7; *p1 = half / 7; *p2 = 2;
		}
	} else if (p == 3 || p == 9) {
		*p0 = 3; *p1 = 1; *p2 = p / 3;
	} else if (p == 5 || p == 7) {
		*p0 = p; *p1 = 1; *p2 = 1;
	} else if (p == 15) {
		*p0 = 3; *p1 = 1; *p2 = 5;
	} else if (p == 21) {
		*p0 = 7; *p1 = 1; *p2 = 3;
	} else if (p == 35) {
		*p0 = 7; *p1 = 1; *p2 = 5;
	}
}


static void
skl_wrpll_params_populate(struct skl_wrpll_params* params, uint64 afe_clock,
	int ref_clock, uint64 central_freq, uint32 p0, uint32 p1, uint32 p2)
{
	uint64 dco_freq = (uint64)p0 * p1 * p2 * afe_clock;

	// Central frequency encoding for DPLL_CFGCR2[1:0]
	switch (central_freq) {
		case 9600000000ULL: params->central_freq = 0; break;
		case 9000000000ULL: params->central_freq = 1; break;
		case 8400000000ULL: params->central_freq = 3; break;
	}

	// P0 (pdiv) encoding for DPLL_CFGCR2[4:2]
	switch (p0) {
		case 1: params->pdiv = 0; break;
		case 2: params->pdiv = 1; break;
		case 3: params->pdiv = 2; break;
		case 7: params->pdiv = 4; break;
		default: ERROR("%s: Invalid pdiv %u\n", __func__, p0);
	}

	// P2 (kdiv) encoding for DPLL_CFGCR2[8:6]
	switch (p2) {
		case 5: params->kdiv = 0; break;
		case 2: params->kdiv = 1; break;
		case 3: params->kdiv = 2; break;
		case 1: params->kdiv = 3; break;
		default: ERROR("%s: Invalid kdiv %u\n", __func__, p2);
	}

	// P1 (qdiv) for DPLL_CFGCR2[15:9] and mode bit [5]
	params->qdiv_ratio = p1;
	params->qdiv_mode = (p1 == 1) ? 0 : 1;

	// DCO multiplier = dco_integer + dco_fraction/32768
	// ref_clock is in kHz, convert to Hz for calculation
	uint64 ref_hz = (uint64)ref_clock * 1000;
	params->dco_integer = (uint32)(dco_freq / ref_hz);
	params->dco_fraction = (uint32)(
		((dco_freq % ref_hz) * 0x8000 + ref_hz / 2) / ref_hz
	);

	TRACE("%s: ref=%d kHz, dco_int=%u, dco_frac=0x%x, p0=%u, p1=%u, p2=%u\n",
		__func__, ref_clock, params->dco_integer, params->dco_fraction,
		p0, p1, p2);
}


bool
skl_ddi_calculate_wrpll(int clock, int ref_clock,
	struct skl_wrpll_params* wrpll_params)
{
	// clock is in kHz, AFE clock is 5x pixel clock
	// Use uint64 to avoid overflow for high frequencies
	uint64 afe_clock = (uint64)clock * 1000 * 5;

	static const uint64 dco_central_freq[] = {
		8400000000ULL, 9000000000ULL, 9600000000ULL
	};

	static const int even_dividers[] = {
		4, 6, 8, 10, 12, 14, 16, 18, 20, 24, 28, 30, 32, 36, 40, 42, 44,
		48, 52, 54, 56, 60, 64, 66, 68, 70, 72, 76, 78, 80, 84, 88, 90,
		92, 96, 98
	};
	static const int odd_dividers[] = { 3, 5, 7, 9, 15, 21, 35 };

	struct skl_wrpll_context ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.min_deviation = UINT64_MAX;

	// Try even dividers first (preferred for jitter)
	for (size_t dco = 0; dco < B_COUNT_OF(dco_central_freq); dco++) {
		for (size_t i = 0; i < B_COUNT_OF(even_dividers); i++) {
			unsigned p = even_dividers[i];
			skl_wrpll_try_divider(&ctx, dco_central_freq[dco],
				(uint64)p * afe_clock, p);
			if (ctx.min_deviation == 0)
				goto found;
		}
	}

	if (ctx.p != 0)
		goto found;

	// Fallback to odd dividers
	for (size_t dco = 0; dco < B_COUNT_OF(dco_central_freq); dco++) {
		for (size_t i = 0; i < B_COUNT_OF(odd_dividers); i++) {
			unsigned p = odd_dividers[i];
			skl_wrpll_try_divider(&ctx, dco_central_freq[dco],
				(uint64)p * afe_clock, p);
			if (ctx.min_deviation == 0)
				goto found;
		}
	}

found:
	if (ctx.p == 0) {
		ERROR("%s: No valid divider for %d kHz\n", __func__, clock);
		return false;
	}

	unsigned p0 = 0, p1 = 0, p2 = 0;
	skl_wrpll_get_multipliers(ctx.p, &p0, &p1, &p2);
	skl_wrpll_params_populate(wrpll_params, afe_clock, ref_clock,
		ctx.central_freq, p0, p1, p2);

	return true;
}



// Skylake DPLL Programming (Gen 9 / 9.5)

//
// SKL uses DPLL_CTRL1 for link rate / HDMI mode selection, and
// DPLL_CFGCR1/CFGCR2 for DCO frequency and divider configuration.
//
// Reference: Intel IHD-OS-SKL-Vol 12-05.16, pages 170-175


// SKL DPLL register addresses
#define SKL_DPLL_CTRL1			0x6C058
#define SKL_DPLL1_CFGCR1		0x6C040
#define SKL_DPLL1_CFGCR2		0x6C044
#define SKL_DPLL2_CFGCR1		0x6C048
#define SKL_DPLL2_CFGCR2		0x6C04C
#define SKL_DPLL3_CFGCR1		0x6C050
#define SKL_DPLL3_CFGCR2		0x6C054

// DPLL_CTRL1 bits (6 bits per DPLL, starting at bit 0 for DPLL0)
#define SKL_DPLL_CTRL1_OVERRIDE(id)		(1 << ((id) * 6))
#define SKL_DPLL_CTRL1_HDMI_MODE(id)	(1 << ((id) * 6 + 1))
#define SKL_DPLL_CTRL1_SSC(id)			(1 << ((id) * 6 + 2))
#define SKL_DPLL_CTRL1_LINK_RATE_MASK(id)	(7 << ((id) * 6 + 3))
#define SKL_DPLL_CTRL1_LINK_RATE(rate, id)	((rate) << ((id) * 6 + 3))

// Link rate encodings for DPLL_CTRL1
#define SKL_DPLL_LINK_RATE_2700		0
#define SKL_DPLL_LINK_RATE_1350		1
#define SKL_DPLL_LINK_RATE_810		2
#define SKL_DPLL_LINK_RATE_1620		3
#define SKL_DPLL_LINK_RATE_1080		4
#define SKL_DPLL_LINK_RATE_2160		5

// DPLL enable register
#define SKL_LCPLL1_CTL			0x46010
#define SKL_LCPLL2_CTL			0x46014
#define SKL_DPLL_ENABLE			(1 << 31)
#define SKL_DPLL_LOCK			(1 << 30)

// CFGCR1 bits
#define SKL_CFGCR1_FREQ_ENABLE		(1 << 31)
#define SKL_CFGCR1_DCO_FRACTION_SHIFT	9
#define SKL_CFGCR1_DCO_FRACTION_MASK	(0x7FFF << 9)
#define SKL_CFGCR1_DCO_INTEGER_MASK	0x1FF

// CFGCR2 bits  
#define SKL_CFGCR2_QDIV_RATIO_SHIFT		8
#define SKL_CFGCR2_QDIV_MODE			(1 << 7)
#define SKL_CFGCR2_KDIV_SHIFT			5
#define SKL_CFGCR2_PDIV_SHIFT			2
#define SKL_CFGCR2_CENTRAL_FREQ_MASK	0x3


status_t
skl_program_dpll(int pll_id, const struct skl_wrpll_params* params, bool is_hdmi)
{
	uint32 cfgcr1_reg, cfgcr2_reg, enable_reg;
	
	// Select registers based on PLL ID
	// Note: DPLL0 is used for eDP and has different handling
	switch (pll_id) {
		case 1:
			cfgcr1_reg = SKL_DPLL1_CFGCR1;
			cfgcr2_reg = SKL_DPLL1_CFGCR2;
			enable_reg = SKL_LCPLL1_CTL;
			break;
		case 2:
			cfgcr1_reg = SKL_DPLL2_CFGCR1;
			cfgcr2_reg = SKL_DPLL2_CFGCR2;
			enable_reg = SKL_LCPLL2_CTL;
			break;
		case 3:
			cfgcr1_reg = SKL_DPLL3_CFGCR1;
			cfgcr2_reg = SKL_DPLL3_CFGCR2;
			enable_reg = SKL_LCPLL2_CTL + 4;  // 0x46018
			break;
		default:
			ERROR("%s: Invalid PLL ID %d\n", __func__, pll_id);
			return B_BAD_VALUE;
	}

	// Build CFGCR1: DCO integer + fraction
	uint32 cfgcr1 = SKL_CFGCR1_FREQ_ENABLE
		| (params->dco_integer & SKL_CFGCR1_DCO_INTEGER_MASK)
		| ((params->dco_fraction << SKL_CFGCR1_DCO_FRACTION_SHIFT) 
			& SKL_CFGCR1_DCO_FRACTION_MASK);

	// Build CFGCR2: dividers + central frequency
	uint32 cfgcr2 = (params->qdiv_ratio << SKL_CFGCR2_QDIV_RATIO_SHIFT)
		| (params->kdiv << SKL_CFGCR2_KDIV_SHIFT)
		| (params->pdiv << SKL_CFGCR2_PDIV_SHIFT)
		| (params->central_freq & SKL_CFGCR2_CENTRAL_FREQ_MASK);
	
	if (params->qdiv_mode)
		cfgcr2 |= SKL_CFGCR2_QDIV_MODE;

	// Build CTRL1 entry for this DPLL
	uint32 ctrl1 = read32(SKL_DPLL_CTRL1);
	ctrl1 &= ~(SKL_DPLL_CTRL1_HDMI_MODE(pll_id) 
		| SKL_DPLL_CTRL1_SSC(pll_id)
		| SKL_DPLL_CTRL1_LINK_RATE_MASK(pll_id));
	ctrl1 |= SKL_DPLL_CTRL1_OVERRIDE(pll_id);
	
	if (is_hdmi)
		ctrl1 |= SKL_DPLL_CTRL1_HDMI_MODE(pll_id);

	// Disable PLL first
	write32(enable_reg, read32(enable_reg) & ~SKL_DPLL_ENABLE);
	
	// Wait for unlock
	int timeout = 1000;
	while ((read32(enable_reg) & SKL_DPLL_LOCK) && timeout-- > 0)
		spin(1);

	// Program CTRL1
	write32(SKL_DPLL_CTRL1, ctrl1);
	read32(SKL_DPLL_CTRL1);  // Posting read

	// Program CFGCR1 and CFGCR2
	write32(cfgcr1_reg, cfgcr1);
	write32(cfgcr2_reg, cfgcr2);
	read32(cfgcr2_reg);  // Posting read

	// Enable PLL
	write32(enable_reg, read32(enable_reg) | SKL_DPLL_ENABLE);

	// Wait for lock
	timeout = 5000;
	while (!(read32(enable_reg) & SKL_DPLL_LOCK) && timeout-- > 0)
		spin(1);

	if (timeout <= 0) {
		ERROR("%s: DPLL %d failed to lock\n", __func__, pll_id);
		return B_TIMED_OUT;
	}

	TRACE("%s: DPLL %d locked, cfgcr1=0x%08x, cfgcr2=0x%08x, ctrl1=0x%08x\n",
		__func__, pll_id, cfgcr1, cfgcr2, ctrl1);

	return B_OK;
}



// Ice Lake Combo PLL (Gen 11)

//
// Ice Lake uses CNL-style PLL for combo PHY ports (A, B).
// HDMI uses dynamic WRPLL calculation (same algorithm as SKL).
// DP uses predefined PLL values for standard link rates.
//
// Reference: Intel IHD-OS-ICL-Vol 12-1.20, "Display PLLs"


/*
 * Predefined PLL values for ICL DP link rates.
 * These values are pre-calculated and taken from i915 driver.
 * Indexed by link rate: 5.4G, 2.7G, 1.62G, 3.24G, 2.16G, 4.32G, 8.1G
 *
 * Reference: i915 intel_dpll_mgr.c icl_dp_combo_pll_24MHz_values[]
 */
static const struct skl_wrpll_params icl_dp_combo_pll_24MHz_values[] = {
	// 5.4 Gbps (HBR2)
	{ .dco_integer = 0x151, .dco_fraction = 0x4000,
	  .pdiv = 0x2 /* 3 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
	// 2.7 Gbps (HBR)
	{ .dco_integer = 0x151, .dco_fraction = 0x4000,
	  .pdiv = 0x2 /* 3 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
	// 1.62 Gbps (RBR)
	{ .dco_integer = 0x151, .dco_fraction = 0x4000,
	  .pdiv = 0x4 /* 5 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
	// 3.24 Gbps
	{ .dco_integer = 0x151, .dco_fraction = 0x4000,
	  .pdiv = 0x4 /* 5 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
	// 2.16 Gbps
	{ .dco_integer = 0x168, .dco_fraction = 0x0000,
	  .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 1, .qdiv_ratio = 2,
	  .central_freq = 0 },
	// 4.32 Gbps
	{ .dco_integer = 0x168, .dco_fraction = 0x0000,
	  .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
	// 8.1 Gbps (HBR3)
	{ .dco_integer = 0x151, .dco_fraction = 0x4000,
	  .pdiv = 0x1 /* 2 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
};

static const struct skl_wrpll_params icl_dp_combo_pll_19_2MHz_values[] = {
	// 5.4 Gbps (HBR2)
	{ .dco_integer = 0x1A5, .dco_fraction = 0x7000,
	  .pdiv = 0x2 /* 3 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
	// 2.7 Gbps (HBR)
	{ .dco_integer = 0x1A5, .dco_fraction = 0x7000,
	  .pdiv = 0x2 /* 3 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
	// 1.62 Gbps (RBR)
	{ .dco_integer = 0x1A5, .dco_fraction = 0x7000,
	  .pdiv = 0x4 /* 5 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
	// 3.24 Gbps
	{ .dco_integer = 0x1A5, .dco_fraction = 0x7000,
	  .pdiv = 0x4 /* 5 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
	// 2.16 Gbps
	{ .dco_integer = 0x1C2, .dco_fraction = 0x0000,
	  .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 1, .qdiv_ratio = 2,
	  .central_freq = 0 },
	// 4.32 Gbps
	{ .dco_integer = 0x1C2, .dco_fraction = 0x0000,
	  .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
	// 8.1 Gbps (HBR3)
	{ .dco_integer = 0x1A5, .dco_fraction = 0x7000,
	  .pdiv = 0x1 /* 2 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0,
	  .central_freq = 0 },
};


/*
 * Get index into ICL DP PLL tables based on port clock.
 * Returns -1 if port_clock doesn't match any standard rate.
 */
static int
icl_dp_rate_to_index(int port_clock)
{
	switch (port_clock) {
		case 540000: return ICL_DP_LINK_RATE_5400;	// 5.4 Gbps
		case 270000: return ICL_DP_LINK_RATE_2700;	// 2.7 Gbps
		case 162000: return ICL_DP_LINK_RATE_1620;	// 1.62 Gbps
		case 324000: return ICL_DP_LINK_RATE_3240;	// 3.24 Gbps
		case 216000: return ICL_DP_LINK_RATE_2160;	// 2.16 Gbps
		case 432000: return ICL_DP_LINK_RATE_4320;	// 4.32 Gbps
		case 810000: return ICL_DP_LINK_RATE_8100;	// 8.1 Gbps
		default:
			ERROR("%s: Unknown DP link rate %d kHz\n", __func__, port_clock);
			return -1;
	}
}


// ICL DPLL register addresses
#define ICL_DPLL_ENABLE(id)		(0x46010 + (id) * 4)
#define ICL_DPLL0_CFGCR0		0x164000
#define ICL_DPLL0_CFGCR1		0x164004
#define ICL_DPLL1_CFGCR0		0x164080
#define ICL_DPLL1_CFGCR1		0x164084

// ICL DPLL enable bits
#define ICL_DPLL_ENABLE_BIT		(1 << 31)
#define ICL_DPLL_LOCK			(1 << 30)
#define ICL_DPLL_POWER_ENABLE	(1 << 27)
#define ICL_DPLL_POWER_STATE	(1 << 26)

// ICL CFGCR0 bits
#define ICL_CFGCR0_DCO_INTEGER_MASK		0x3FF
#define ICL_CFGCR0_DCO_FRACTION_SHIFT	10
#define ICL_CFGCR0_DCO_FRACTION_MASK	(0x7FFF << 10)

// ICL CFGCR1 bits
#define ICL_CFGCR1_QDIV_RATIO_SHIFT		10
#define ICL_CFGCR1_QDIV_MODE			(1 << 9)
#define ICL_CFGCR1_KDIV_SHIFT			6
#define ICL_CFGCR1_KDIV_MASK			(7 << 6)
#define ICL_CFGCR1_PDIV_SHIFT			2
#define ICL_CFGCR1_PDIV_MASK			(7 << 2)
#define ICL_CFGCR1_CENTRAL_FREQ_8400	(3 << 0)


status_t
icl_program_combo_pll(int pll_id, const struct skl_wrpll_params* params, bool is_hdmi)
{
	uint32 cfgcr0_reg, cfgcr1_reg, enable_reg;
	
	// ICL has DPLL0 and DPLL1 for combo PHY
	switch (pll_id) {
		case 0:
			cfgcr0_reg = ICL_DPLL0_CFGCR0;
			cfgcr1_reg = ICL_DPLL0_CFGCR1;
			enable_reg = ICL_DPLL_ENABLE(0);
			break;
		case 1:
			cfgcr0_reg = ICL_DPLL1_CFGCR0;
			cfgcr1_reg = ICL_DPLL1_CFGCR1;
			enable_reg = ICL_DPLL_ENABLE(1);
			break;
		default:
			ERROR("%s: Invalid combo PLL ID %d (ICL supports 0-1)\n", 
				__func__, pll_id);
			return B_BAD_VALUE;
	}

	// Build CFGCR0: DCO integer + fraction + HDMI mode
	uint32 cfgcr0 = (params->dco_integer & ICL_CFGCR0_DCO_INTEGER_MASK)
		| ((params->dco_fraction << ICL_CFGCR0_DCO_FRACTION_SHIFT) 
			& ICL_CFGCR0_DCO_FRACTION_MASK);
	
	if (is_hdmi)
		cfgcr0 |= (1 << 25);  // HDMI mode bit

	// Build CFGCR1: dividers + central frequency
	uint32 cfgcr1 = (params->qdiv_ratio << ICL_CFGCR1_QDIV_RATIO_SHIFT)
		| ((params->kdiv << ICL_CFGCR1_KDIV_SHIFT) & ICL_CFGCR1_KDIV_MASK)
		| ((params->pdiv << ICL_CFGCR1_PDIV_SHIFT) & ICL_CFGCR1_PDIV_MASK)
		| ICL_CFGCR1_CENTRAL_FREQ_8400;  // Always use 8400 MHz central
	
	if (params->qdiv_mode)
		cfgcr1 |= ICL_CFGCR1_QDIV_MODE;

	// Check if already configured correctly
	uint32 enable_val = read32(enable_reg);
	if (enable_val & ICL_DPLL_LOCK) {
		uint32 old_cfgcr0 = read32(cfgcr0_reg);
		uint32 old_cfgcr1 = read32(cfgcr1_reg);
		
		if (old_cfgcr0 == cfgcr0 && old_cfgcr1 == cfgcr1) {
			TRACE("%s: Combo PLL %d already configured correctly\n", 
				__func__, pll_id);
			return B_OK;
		}
	}

	// Disable PLL
	write32(enable_reg, read32(enable_reg) & ~ICL_DPLL_ENABLE_BIT);
	
	int timeout = 1000;
	while ((read32(enable_reg) & ICL_DPLL_LOCK) && timeout-- > 0)
		spin(1);

	// Enable PLL power
	write32(enable_reg, read32(enable_reg) | ICL_DPLL_POWER_ENABLE);
	
	timeout = 1000;
	while (!(read32(enable_reg) & ICL_DPLL_POWER_STATE) && timeout-- > 0)
		spin(1);

	// Program CFGCR0 and CFGCR1
	write32(cfgcr0_reg, cfgcr0);
	write32(cfgcr1_reg, cfgcr1);
	read32(cfgcr1_reg);  // Posting read

	// Enable PLL and wait for lock
	write32(enable_reg, read32(enable_reg) | ICL_DPLL_ENABLE_BIT);

	timeout = 5000;
	while (!(read32(enable_reg) & ICL_DPLL_LOCK) && timeout-- > 0)
		spin(1);

	if (timeout <= 0) {
		ERROR("%s: Combo PLL %d failed to lock\n", __func__, pll_id);
		return B_TIMED_OUT;
	}

	TRACE("%s: Combo PLL %d locked, cfgcr0=0x%08x, cfgcr1=0x%08x\n",
		__func__, pll_id, cfgcr0, cfgcr1);

	return B_OK;
}


bool
icl_compute_combo_pll_hdmi(int clock, int ref_clock,
	struct skl_wrpll_params* pll_params)
{
	// ICL HDMI uses same WRPLL algorithm as SKL
	return skl_ddi_calculate_wrpll(clock, ref_clock, pll_params);
}


bool
icl_compute_combo_pll_dp(int port_clock, int ref_clock,
	struct skl_wrpll_params* pll_params)
{
	const struct skl_wrpll_params* table;

	// Select table based on reference clock
	if (ref_clock == 24000)
		table = icl_dp_combo_pll_24MHz_values;
	else
		table = icl_dp_combo_pll_19_2MHz_values;

	int index = icl_dp_rate_to_index(port_clock);
	if (index < 0) {
		// Fallback to HBR (2.7 Gbps) for unknown rates
		TRACE("%s: Unknown port_clock %d, falling back to HBR\n",
			__func__, port_clock);
		index = ICL_DP_LINK_RATE_2700;
	}

	*pll_params = table[index];

	TRACE("%s: port_clock=%d kHz, ref=%d kHz, dco_int=0x%x, dco_frac=0x%x\n",
		__func__, port_clock, ref_clock,
		pll_params->dco_integer, pll_params->dco_fraction);

	return true;
}



// Tiger Lake PLL (Gen 12+)

//
// Tiger Lake simplifies the PLL architecture. The DCO generates a frequency
// in the 7998-10000 MHz range, which is divided by P, Q, K and then by
// a fixed factor of 5 to produce the symbol clock.
//
// Symbol clock = DCO / (5 * P * Q * K)
//
// The algorithm finds the divider combination that places DCO closest
// to the midpoint (8999 MHz) of its valid range.
//
// Reference: Intel IHD-OS-TGL-Vol 12-12.21, page 178-182


// DCO valid range (MHz)
#define TGL_DCO_MIN		7998.0f
#define TGL_DCO_MAX		10000.0f
#define TGL_DCO_MID		((TGL_DCO_MIN + TGL_DCO_MAX) / 2.0f)


bool
tgl_compute_hdmi_dpll(int freq, int* Pdiv, int* Qdiv, int* Kdiv, float* bestdco)
{
	int bestdiv = 0;
	float bestdcocentrality = 999999.0f;

	// Valid divider values based on P, Q, K constraints:
	// P: 2, 3, 5, 7
	// K: 1, 2, 3
	// Q: 1-255 if K=2, else Q=1
	static const int dividerlist[] = {
		2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 24, 28, 30, 32, 36,
		40, 42, 44, 48, 50, 52, 54, 56, 60, 64, 66, 68, 70, 72,
		76, 78, 80, 84, 88, 90, 92, 96, 98, 100, 102,
		3, 5, 7, 9, 15, 21
	};

	// AFE clock = 5 * symbol clock (in MHz)
	float afeclk = (5.0f * freq) / 1000.0f;

	for (size_t i = 0; i < B_COUNT_OF(dividerlist); i++) {
		int div = dividerlist[i];
		float dco = afeclk * div;

		if (dco >= TGL_DCO_MIN && dco <= TGL_DCO_MAX) {
			float centrality = fabsf(dco - TGL_DCO_MID);
			if (centrality < bestdcocentrality) {
				bestdcocentrality = centrality;
				bestdiv = div;
				*bestdco = dco;
			}
		}
	}

	if (bestdiv == 0)
		return false;

	// Decompose divider into P, Q, K
	if (bestdiv % 2 == 0) {
		if (bestdiv == 2) {
			*Pdiv = 2; *Qdiv = 1; *Kdiv = 1;
		} else if (bestdiv % 4 == 0) {
			*Pdiv = 2; *Qdiv = bestdiv / 4; *Kdiv = 2;
		} else if (bestdiv % 6 == 0) {
			*Pdiv = 3; *Qdiv = bestdiv / 6; *Kdiv = 2;
		} else if (bestdiv % 5 == 0) {
			*Pdiv = 5; *Qdiv = bestdiv / 10; *Kdiv = 2;
		} else if (bestdiv % 14 == 0) {
			*Pdiv = 7; *Qdiv = bestdiv / 14; *Kdiv = 2;
		}
	} else {
		if (bestdiv == 3 || bestdiv == 5 || bestdiv == 7) {
			*Pdiv = bestdiv; *Qdiv = 1; *Kdiv = 1;
		} else {
			// 9, 15, 21
			*Pdiv = bestdiv / 3; *Qdiv = 1; *Kdiv = 3;
		}
	}

	TRACE("%s: freq=%d kHz, dco=%.1f MHz, P=%d Q=%d K=%d\n",
		__func__, freq, *bestdco, *Pdiv, *Qdiv, *Kdiv);

	return true;
}


/*
 * TGL DP link rates with predefined PLL settings.
 * Reference: IHD-OS-TGL-Vol 12-12.21, page 178 "DisplayPort Mode PLL values"
 */
bool
tgl_compute_dp_dpll(int port_clock, int* Pdiv, int* Qdiv, int* Kdiv, float* bestdco)
{
	// TGL DP uses fixed PLL configurations for standard link rates
	// DCO is calculated based on AFE clock = 5 * link_rate / 10
	// (link_rate in Gbps, so 2700 kHz = 2.7 Gbps)

	switch (port_clock) {
		case 162000:	// 1.62 Gbps (RBR)
			*Pdiv = 2; *Qdiv = 1; *Kdiv = 2;
			*bestdco = 8100.0f;
			break;
		case 270000:	// 2.7 Gbps (HBR)
			*Pdiv = 2; *Qdiv = 1; *Kdiv = 2;
			*bestdco = 8100.0f;
			break;
		case 540000:	// 5.4 Gbps (HBR2)
			*Pdiv = 3; *Qdiv = 1; *Kdiv = 1;
			*bestdco = 8100.0f;
			break;
		case 810000:	// 8.1 Gbps (HBR3)
			*Pdiv = 2; *Qdiv = 1; *Kdiv = 1;
			*bestdco = 8100.0f;
			break;
		default:
			// Default to HBR (2.7 Gbps)
			TRACE("%s: Unknown port_clock %d, defaulting to HBR\n",
				__func__, port_clock);
			*Pdiv = 2; *Qdiv = 1; *Kdiv = 2;
			*bestdco = 8100.0f;
			break;
	}

	TRACE("%s: port_clock=%d kHz, dco=%.1f MHz, P=%d Q=%d K=%d\n",
		__func__, port_clock, *bestdco, *Pdiv, *Qdiv, *Kdiv);

	return true;
}


status_t
tgl_program_pll(int which, int Pdiv, int Qdiv, int Kdiv, float dco)
{
	uint32 DPLL_CFGCR0;
	uint32 DPLL_CFGCR1;
	uint32 DPLL_ENABLE;
	uint32 DPLL_SPREAD_SPECTRUM;

	switch (which) {
		case 0:
			DPLL_ENABLE = TGL_DPLL0_ENABLE;
			DPLL_SPREAD_SPECTRUM = TGL_DPLL0_SPREAD_SPECTRUM;
			DPLL_CFGCR0 = TGL_DPLL0_CFGCR0;
			DPLL_CFGCR1 = TGL_DPLL0_CFGCR1;
			break;
		case 1:
			DPLL_ENABLE = TGL_DPLL1_ENABLE;
			DPLL_SPREAD_SPECTRUM = TGL_DPLL1_SPREAD_SPECTRUM;
			DPLL_CFGCR0 = TGL_DPLL1_CFGCR0;
			DPLL_CFGCR1 = TGL_DPLL1_CFGCR1;
			break;
		case 4:
			DPLL_ENABLE = TGL_DPLL4_ENABLE;
			DPLL_SPREAD_SPECTRUM = TGL_DPLL4_SPREAD_SPECTRUM;
			DPLL_CFGCR0 = TGL_DPLL4_CFGCR0;
			DPLL_CFGCR1 = TGL_DPLL4_CFGCR1;
			break;
		default:
			return B_BAD_VALUE;
	}

	// Get effective reference frequency
	int ref_khz = get_effective_ref_clock();
	float ref = ref_khz / 1000.0f;

	// Compute DCO integer and fractional parts
	uint32 dco_int = (uint32)floorf(dco / ref);
	uint32 dco_frac = (uint32)ceilf((dco / ref - dco_int) * (1 << 15));
	int32 dco_reg = dco_int | (dco_frac << TGL_DPLL_DCO_FRACTION_SHIFT);

	// Encode dividers
	int32 dividers = 0;

	switch (Pdiv) {
		case 2: dividers |= TGL_DPLL_PDIV_2; break;
		case 3: dividers |= TGL_DPLL_PDIV_3; break;
		case 5: dividers |= TGL_DPLL_PDIV_5; break;
		case 7: dividers |= TGL_DPLL_PDIV_7; break;
		default: return B_BAD_VALUE;
	}

	switch (Kdiv) {
		case 1: dividers |= TGL_DPLL_KDIV_1; break;
		case 2: dividers |= TGL_DPLL_KDIV_2; break;
		case 3: dividers |= TGL_DPLL_KDIV_3; break;
		default: return B_BAD_VALUE;
	}

	if (Qdiv != 1)
		dividers |= (Qdiv << TGL_DPLL_QDIV_RATIO_SHIFT) | TGL_DPLL_QDIV_ENABLE;

	// Check if PLL already configured correctly
	int32 initialState = read32(DPLL_ENABLE);
	if (initialState & TGL_DPLL_LOCK) {
		int32 oldDCO = read32(DPLL_CFGCR0);
		int32 oldDividers = read32(DPLL_CFGCR1);

		if (oldDCO == dco_reg && oldDividers == dividers) {
			TRACE("%s: PLL %d already configured correctly\n", __func__, which);
			return B_OK;
		}
	}

	// Disable PLL
	write32(DPLL_ENABLE, read32(DPLL_ENABLE) & ~TGL_DPLL_ENABLE);
	while ((read32(DPLL_ENABLE) & TGL_DPLL_LOCK) != 0)
		;

	// Enable PLL power
	write32(DPLL_ENABLE, read32(DPLL_ENABLE) | TGL_DPLL_POWER_ENABLE);
	while ((read32(DPLL_ENABLE) & TGL_DPLL_POWER_STATE) == 0)
		;

	// Disable spread spectrum
	write32(DPLL_SPREAD_SPECTRUM,
		read32(DPLL_SPREAD_SPECTRUM) & ~TGL_DPLL_SSC_ENABLE);

	// Program DCO and dividers
	write32(DPLL_CFGCR0, dco_reg);
	write32(DPLL_CFGCR1, dividers);
	read32(DPLL_CFGCR1);  // Flush writes

	// Enable PLL and wait for lock
	write32(DPLL_ENABLE, read32(DPLL_ENABLE) | TGL_DPLL_ENABLE);
	while ((read32(DPLL_ENABLE) & TGL_DPLL_LOCK) == 0)
		;

	TRACE("%s: PLL %d locked, dco_reg=0x%x, dividers=0x%x\n",
		__func__, which, dco_reg, dividers);

	return B_OK;
}



// Unified Interface


status_t
compute_display_pll(uint32 pixel_clock, pll_port_type port_type, int pll_index)
{
	int generation = gInfo->shared_info->device_type.Generation();
	int ref_clock = get_effective_ref_clock();
	bool is_hdmi = (port_type == PLL_PORT_HDMI || port_type == PLL_PORT_DVI);

	TRACE("%s: pixel_clock=%u kHz, port=%d, pll=%d, gen=%d, ref=%d kHz\n",
		__func__, pixel_clock, port_type, pll_index, generation, ref_clock);

	if (generation >= 12) {
		// Tiger Lake and later
		int Pdiv, Qdiv, Kdiv;
		float dco;
		bool ok;

		if (port_type == PLL_PORT_DP || port_type == PLL_PORT_EDP)
			ok = tgl_compute_dp_dpll(pixel_clock, &Pdiv, &Qdiv, &Kdiv, &dco);
		else
			ok = tgl_compute_hdmi_dpll(pixel_clock, &Pdiv, &Qdiv, &Kdiv, &dco);

		if (!ok) {
			ERROR("%s: Failed to compute TGL PLL for %u kHz\n",
				__func__, pixel_clock);
			return B_ERROR;
		}

		return tgl_program_pll(pll_index, Pdiv, Qdiv, Kdiv, dco);

	} else if (generation >= 11) {
		// Ice Lake, Elkhart Lake, Jasper Lake
		struct skl_wrpll_params params;
		bool ok;

		if (port_type == PLL_PORT_DP || port_type == PLL_PORT_EDP)
			ok = icl_compute_combo_pll_dp(pixel_clock, ref_clock, &params);
		else
			ok = icl_compute_combo_pll_hdmi(pixel_clock, ref_clock, &params);

		if (!ok) {
			ERROR("%s: Failed to compute ICL PLL for %u kHz\n",
				__func__, pixel_clock);
			return B_ERROR;
		}

		return icl_program_combo_pll(pll_index, &params, is_hdmi);

	} else if (generation >= 9) {
		// Skylake, Kaby Lake, Coffee Lake, Apollo Lake, Gemini Lake
		struct skl_wrpll_params params;

		if (!skl_ddi_calculate_wrpll(pixel_clock, ref_clock, &params)) {
			ERROR("%s: Failed to compute SKL WRPLL for %u kHz\n",
				__func__, pixel_clock);
			return B_ERROR;
		}

		return skl_program_dpll(pll_index, &params, is_hdmi);
	}

	ERROR("%s: Unsupported GPU generation %d\n", __func__, generation);
	return B_UNSUPPORTED;
}
