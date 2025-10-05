/*
 * Copyright 2006-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *      Axel Dörfler, axeld@pinc-software.de
 *      Alexander von Gluck IV, kallisti5@unixzen.com
 */
#ifndef INTEL_EXTREME_PLL_H
#define INTEL_EXTREME_PLL_H


#include "intel_extreme.h"


// Gen 6+ PLL structures (legacy pll_divisors and pll_limits removed)

struct skl_wrpll_params {
	uint32 dco_fraction;
	uint32 dco_integer;
	uint32 qdiv_ratio;
	uint32 qdiv_mode;
	uint32 kdiv;
	uint32 pdiv;
	uint32 central_freq;
};

void refclk_activate_ilk(bool hasPanel);

void hsw_ddi_calculate_wrpll(int clock /* in Hz */,
			unsigned *r2_out, unsigned *n2_out, unsigned *p_out);
bool skl_ddi_calculate_wrpll(int clock /* in Hz */,
			int ref_clock, struct skl_wrpll_params *wrpll_params);

#endif /* INTEL_EXTREME_PLL_H */
