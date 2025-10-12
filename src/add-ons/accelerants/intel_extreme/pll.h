/*
 * Copyright 2006-2025, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *      Axel Dörfler, axeld@pinc-software.de
 *      Alexander von Gluck IV, kallisti5@unixzen.com
 */
#ifndef INTEL_EXTREME_PLL_H
#define INTEL_EXTREME_PLL_H


#include "intel_extreme.h"


// ============================================================================
// Gen 6+ PLL Structures and Functions
// ============================================================================
// Note: All legacy Gen 2-5 divisor-based PLL code has been removed.
// Gen 6+ uses completely different PLL algorithms:
//   - Gen 6 (Sandy Bridge): Uses PCH reference clock activation
//   - Gen 7-8 (Haswell/Broadwell): WRPLL algorithm
//   - Gen 9+ (Skylake and later): Enhanced WRPLL algorithm


// Skylake+ (Gen 9+) WRPLL parameters
struct skl_wrpll_params {
	uint32 dco_fraction;
	uint32 dco_integer;
	uint32 qdiv_ratio;
	uint32 qdiv_mode;
	uint32 kdiv;
	uint32 pdiv;
	uint32 central_freq;
};


// ============================================================================
// Reference Clock Management (Gen 6+)
// ============================================================================

// Activate reference clocks for IronLake PCH and later (Gen 6+)
// Used with Sandy Bridge (IBX PCH) and newer platforms
// hasPanel: true if LVDS/eDP panel is present
void refclk_activate_ilk(bool hasPanel);


// ============================================================================
// Display PLL Calculations
// ============================================================================

// Calculate WRPLL parameters for Haswell/Broadwell (Gen 7-8)
// clock: target pixel clock in Hz
// r2_out, n2_out, p_out: calculated PLL divisor parameters
void hsw_ddi_calculate_wrpll(int clock /* in Hz */,
							 unsigned *r2_out, unsigned *n2_out, unsigned *p_out);

// Calculate WRPLL parameters for Skylake and later (Gen 9+)
// clock: target pixel clock in Hz
// ref_clock: reference clock frequency
// wrpll_params: output structure for calculated parameters
// Returns: true if valid parameters found, false otherwise
bool skl_ddi_calculate_wrpll(int clock /* in Hz */,
							 int ref_clock, struct skl_wrpll_params *wrpll_params);


#endif /* INTEL_EXTREME_PLL_H */
