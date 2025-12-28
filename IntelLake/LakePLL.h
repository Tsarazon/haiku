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
 * Covers all "Lake" generations: Skylake through Alder Lake.
 */
#ifndef INTEL_EXTREME_LAKE_PLL_H
#define INTEL_EXTREME_LAKE_PLL_H


#include "intel_extreme.h"

#include <SupportDefs.h>



// Skylake / Kaby Lake / Coffee Lake (Gen 9 / 9.5)
// Also: Apollo Lake, Gemini Lake (Atom)


/*
 * WRPLL parameters for Skylake-era GPUs.
 *
 * The WRPLL uses a DCO with three divider stages (P0, P1, P2).
 * Output = DCO_freq / (P0 * P1 * P2)
 * DCO_freq = (dco_integer + dco_fraction/32768) * ref_clock
 *
 * Reference: Intel IHD-OS-SKL-Vol 12-05.16, page 170
 */
struct skl_wrpll_params {
	uint32	dco_fraction;	// 15-bit fractional multiplier
	uint32	dco_integer;	// Integer multiplier
	uint32	qdiv_ratio;		// Q divider ratio (P1), 1-255
	uint32	qdiv_mode;		// Q divider enable: 0=bypass, 1=divide
	uint32	kdiv;			// K divider (P2): 0=5, 1=2, 2=3, 3=1
	uint32	pdiv;			// P divider (P0): 0=1, 1=2, 2=3, 4=7
	uint32	central_freq;	// DCO central: 0=9600, 1=9000, 3=8400 MHz
};


bool skl_ddi_calculate_wrpll(int clock, int ref_clock,
	struct skl_wrpll_params* wrpll_params);

status_t skl_program_dpll(int pll_id, const struct skl_wrpll_params* params,
	bool is_hdmi);



// Ice Lake / Elkhart Lake / Jasper Lake (Gen 11)


/*
 * Ice Lake uses the CNL-style WRPLL architecture with combo PHY.
 *
 * For HDMI: Uses cnl_ddi_calculate_wrpll (same algorithm as SKL/CNL)
 * For DP: Uses predefined PLL values for standard link rates
 *
 * The DCO range is the same as SKL: 8400, 9000, 9600 MHz central frequencies.
 *
 * Important: ref_clock of 38400 kHz is auto-divided by 2 to 19200 kHz.
 *
 * Reference: Intel IHD-OS-ICL-Vol 12-1.20, "Display PLLs"
 */

// ICL DP link rate indices for predefined PLL tables
enum icl_dp_link_rate {
	ICL_DP_LINK_RATE_5400,		// 5.4 Gbps (HBR2)
	ICL_DP_LINK_RATE_2700,		// 2.7 Gbps (HBR)
	ICL_DP_LINK_RATE_1620,		// 1.62 Gbps (RBR)
	ICL_DP_LINK_RATE_3240,		// 3.24 Gbps
	ICL_DP_LINK_RATE_2160,		// 2.16 Gbps
	ICL_DP_LINK_RATE_4320,		// 4.32 Gbps
	ICL_DP_LINK_RATE_8100,		// 8.1 Gbps (HBR3)
	ICL_DP_LINK_RATE_COUNT
};

bool icl_compute_combo_pll_hdmi(int clock, int ref_clock,
	struct skl_wrpll_params* pll_params);

bool icl_compute_combo_pll_dp(int port_clock, int ref_clock,
	struct skl_wrpll_params* pll_params);

status_t icl_program_combo_pll(int pll_id, const struct skl_wrpll_params* params,
	bool is_hdmi);

/*
 * NOTE: ICL Type-C ports (C, D, E, F) use MG PHY with a different PLL
 * architecture. The MG PHY PLL (icl_calc_mg_pll_state in i915) requires:
 *
 *   - Different register set (MG_PLL_DIV0, MG_PLL_DIV1, MG_PLL_LF, etc.)
 *   - Different divider constraints (m1div, m2div with fractional support)
 *   - HSDIV + DIV2 clock division stages
 *
 * For Mobile Haiku MVP, we focus on combo PHY ports (A, B) which cover
 * HDMI and eDP. Type-C/Thunderbolt support can be added later.
 *
 * Reference: IHD-OS-ICL-Vol 12-1.20, "MG PLL Programming"
 */



// Tiger Lake / Alder Lake / Raptor Lake (Gen 12+)
// Also: Jasper Lake, Elkhart Lake, Alder Lake-N (Atom)


/*
 * Tiger Lake uses a simplified PLL architecture compared to Skylake.
 *
 * The DCO directly outputs to three dividers (P, Q, K) followed by
 * a fixed divide-by-5 to produce the symbol clock.
 *
 * Symbol clock = DCO_freq / (5 * Pdiv * Qdiv * Kdiv)
 * DCO_freq = (dco_int + dco_frac/32768) * ref_clock
 *
 * DCO valid range: 7998 - 10000 MHz
 *
 * Reference: Intel IHD-OS-TGL-Vol 12-12.21, page 178-182
 */

bool tgl_compute_hdmi_dpll(int freq, int* Pdiv, int* Qdiv, int* Kdiv,
	float* bestdco);

bool tgl_compute_dp_dpll(int port_clock, int* Pdiv, int* Qdiv, int* Kdiv,
	float* bestdco);

status_t tgl_program_pll(int which, int Pdiv, int Qdiv, int Kdiv, float dco);



// Unified interface


/*
 * Port types for PLL selection.
 */
enum pll_port_type {
	PLL_PORT_HDMI,
	PLL_PORT_DVI,
	PLL_PORT_DP,
	PLL_PORT_EDP
};


/*
 * Compute PLL parameters for the current GPU generation.
 *
 * Automatically selects the appropriate algorithm based on GPU generation:
 *   - Gen 9/9.5 (Skylake, Kaby Lake, etc.): WRPLL
 *   - Gen 11 (Ice Lake, Elkhart Lake, etc.): CNL-style combo PLL
 *   - Gen 12+ (Tiger Lake, Alder Lake, etc.): Simplified DCO
 *
 * @param pixel_clock  Desired pixel clock in kHz
 * @param port_type    Type of display port
 * @param pll_index    Which PLL to use (0, 1, or 4 for TGL)
 * @return B_OK on success
 */
status_t compute_display_pll(uint32 pixel_clock, pll_port_type port_type,
	int pll_index);


/*
 * Get effective reference clock, handling auto-divide for Gen11+.
 *
 * For ICL+, if the hardware reports 38400 kHz, the PLL hardware
 * automatically divides it by 2 to get 19200 kHz.
 *
 * @return Reference clock in kHz to use for PLL calculations
 */
int get_effective_ref_clock();


#endif /* INTEL_EXTREME_LAKE_PLL_H */
