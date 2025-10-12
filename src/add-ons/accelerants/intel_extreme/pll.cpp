/*
 * Copyright 2006-2025, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 *		Adrien Destugues, pulkomandy@pulkomandy.tk
 */


#include "pll.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Debug.h>

#include <create_display_modes.h>
#include <ddc.h>
#include <edid.h>
#include <validate_display_mode.h>

#include "accelerant_protos.h"
#include "accelerant.h"
#include "utility.h"


#undef TRACE
#define TRACE_MODE
#ifdef TRACE_MODE
#	define TRACE(x...) _sPrintf("intel_extreme: " x)
#else
#	define TRACE(x...)
#endif

#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED(x...) TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


// ============================================================================
// Reference Clock Activation (Gen 6+)
// ============================================================================
// This function configures the Platform Controller Hub (PCH) reference clocks
// for Gen 6 and later platforms. The PCH was introduced with Sandy Bridge.


void
refclk_activate_ilk(bool hasPanel)
{
	CALLED();

	// Sanity check: this function is only for Gen 6+
	if (gInfo->shared_info->device_type.Generation() < 6) {
		ERROR("%s: Called on unsupported generation %d\n", __func__,
			  gInfo->shared_info->device_type.Generation());
		return;
	}

	bool wantsSSC;  // Spread Spectrum Clocking
	bool hasCK505;  // CK505 clock chip

	// Determine clock configuration based on PCH type
	if (gInfo->shared_info->pch_info == INTEL_PCH_IBX) {
		// Ibex Peak PCH - First generation PCH for Sandy Bridge (Gen 6)
		TRACE("%s: Ibex Peak PCH (Sandy Bridge Gen 6)\n", __func__);
		// TODO: This should match VBT display_clock_mode when VBT parsing is complete
		hasCK505 = false;
		wantsSSC = hasCK505;
	} else {
		// Cougar Point and later PCHs (Gen 6+ Ivy Bridge and newer)
		TRACE("%s: Cougar Point or later PCH (Gen %d)\n", __func__,
			  gInfo->shared_info->device_type.Generation());
		hasCK505 = false;
		wantsSSC = true;  // Most modern platforms want SSC
	}

	uint32 clkRef = read32(PCH_DREF_CONTROL);
	uint32 newRef = clkRef;
	TRACE("%s: PCH_DREF_CONTROL initial value: 0x%08" B_PRIx32 "\n",
		  __func__, clkRef);

	// Configure non-spread spectrum source
	newRef &= ~DREF_NONSPREAD_SOURCE_MASK;
	newRef |= hasCK505 ? DREF_NONSPREAD_CK505_ENABLE
	: DREF_NONSPREAD_SOURCE_ENABLE;

	// Clear SSC and CPU source bits
	newRef &= ~(DREF_SSC_SOURCE_MASK | DREF_CPU_SOURCE_OUTPUT_MASK | DREF_SSC1_ENABLE);

	// Early exit if no changes needed
	if (newRef == clkRef) {
		TRACE("%s: No reference clock changes required\n", __func__);
		return;
	}

	// Configure clocks based on panel presence
	if (hasPanel) {
		// Panel detected - configure SSC appropriately
		newRef &= ~DREF_SSC_SOURCE_MASK;
		newRef |= DREF_SSC_SOURCE_ENABLE;
		newRef = wantsSSC ? (newRef | DREF_SSC1_ENABLE)
		: (newRef & ~DREF_SSC1_ENABLE);

		// Power up SSC before enabling outputs
		write32(PCH_DREF_CONTROL, newRef);
		read32(PCH_DREF_CONTROL);
		TRACE("%s: SSC configured, DREF_CONTROL: 0x%08" B_PRIx32 "\n",
			  __func__, read32(PCH_DREF_CONTROL));
		spin(200);

		// Configure CPU source output
		newRef &= ~DREF_CPU_SOURCE_OUTPUT_MASK;

		// TODO: Properly detect eDP vs other panel types
		bool hasEDP = true;
		if (hasEDP) {
			newRef |= wantsSSC ? DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD
			: DREF_CPU_SOURCE_OUTPUT_NONSPREAD;
		} else {
			newRef |= DREF_CPU_SOURCE_OUTPUT_DISABLE;
		}

		write32(PCH_DREF_CONTROL, newRef);
		read32(PCH_DREF_CONTROL);
		TRACE("%s: CPU source configured, DREF_CONTROL: 0x%08" B_PRIx32 "\n",
			  __func__, read32(PCH_DREF_CONTROL));
		spin(200);
	} else {
		// No panel - disable CPU output
		newRef &= ~DREF_CPU_SOURCE_OUTPUT_MASK;
		newRef |= DREF_CPU_SOURCE_OUTPUT_DISABLE;

		write32(PCH_DREF_CONTROL, newRef);
		read32(PCH_DREF_CONTROL);
		TRACE("%s: CPU output disabled, DREF_CONTROL: 0x%08" B_PRIx32 "\n",
			  __func__, read32(PCH_DREF_CONTROL));
		spin(200);

		// Disable SSC if not wanted
		if (!wantsSSC) {
			newRef &= ~(DREF_SSC_SOURCE_MASK | DREF_SSC1_ENABLE);
			newRef |= DREF_SSC_SOURCE_DISABLE;

			write32(PCH_DREF_CONTROL, newRef);
			read32(PCH_DREF_CONTROL);
			TRACE("%s: SSC disabled, DREF_CONTROL: 0x%08" B_PRIx32 "\n",
				  __func__, read32(PCH_DREF_CONTROL));
			spin(200);
		}
	}

	TRACE("%s: Reference clock configuration complete\n", __func__);
}


// ============================================================================
// Haswell/Broadwell WRPLL Algorithm (Gen 7-8)
// ============================================================================
// The following code is adapted from Intel's i915 DRM driver
// Copyright © 2006-2016 Intel Corporation (MIT License)


#define LC_FREQ 2700
#define LC_FREQ_2K (uint64)(LC_FREQ * 2000)

#define P_MIN 2
#define P_MAX 64
#define P_INC 2

// Constraints for PLL good behavior
#define REF_MIN 48
#define REF_MAX 400
#define VCO_MIN 2400
#define VCO_MAX 4800


static uint64
AbsSubtr64(uint64 nr1, uint64 nr2)
{
	return (nr1 >= nr2) ? (nr1 - nr2) : (nr2 - nr1);
}


struct hsw_wrpll_rnp {
	unsigned p, n2, r2;
};


static unsigned
hsw_wrpll_get_budget_for_freq(int clock)
{
	// Budget values determined empirically for common display frequencies
	// Higher budget = more tolerance for frequency deviation
	switch (clock) {
		case 25175000:
		case 25200000:
		case 27000000:
		case 27027000:
		case 37762500:
		case 37800000:
		case 40500000:
		case 40541000:
		case 54000000:
		case 54054000:
		case 59341000:
		case 59400000:
		case 72000000:
		case 74176000:
		case 74250000:
		case 81000000:
		case 81081000:
		case 89012000:
		case 89100000:
		case 108000000:
		case 108108000:
		case 111264000:
		case 111375000:
		case 148352000:
		case 148500000:
		case 162000000:
		case 162162000:
		case 222525000:
		case 222750000:
		case 296703000:
		case 297000000:
			return 0;

		case 233500000:
		case 245250000:
		case 247750000:
		case 253250000:
		case 298000000:
			return 1500;

		case 169128000:
		case 169500000:
		case 179500000:
		case 202000000:
			return 2000;

		case 256250000:
		case 262500000:
		case 270000000:
		case 272500000:
		case 273750000:
		case 280750000:
		case 281250000:
		case 286000000:
		case 291750000:
			return 4000;

		case 267250000:
		case 268500000:
			return 5000;

		default:
			return 1000;
	}
}


static void
hsw_wrpll_update_rnp(uint64 freq2k, unsigned int budget,
					 unsigned int r2, unsigned int n2, unsigned int p,
					 struct hsw_wrpll_rnp *best)
{
	uint64 a, b, c, d, diff, diff_best;

	// Initialize best values if this is the first iteration
	if (best->p == 0) {
		best->p = p;
		best->n2 = n2;
		best->r2 = r2;
		return;
	}

	/*
	 * Output clock is (LC_FREQ_2K / 2000) * N / (P * R)
	 * We want to minimize: |freq2k - (LC_FREQ_2K * n2/(p * r2))|
	 *
	 * delta = 1e6 * abs(freq2k - (LC_FREQ_2K * n2/(p * r2))) / freq2k
	 *
	 * We prefer solutions where delta <= budget.
	 * Within budget, prefer higher N / (P * R^2) for better VCO stability.
	 */

	a = freq2k * budget * p * r2;
	b = freq2k * budget * best->p * best->r2;
	diff = AbsSubtr64((uint64)freq2k * p * r2, LC_FREQ_2K * n2);
	diff_best = AbsSubtr64((uint64)freq2k * best->p * best->r2,
						   LC_FREQ_2K * best->n2);
	c = 1000000 * diff;
	d = 1000000 * diff_best;

	if (a < c && b < d) {
		// Both above budget - pick the closer one
		if (best->p * best->r2 * diff < p * r2 * diff_best) {
			best->p = p;
			best->n2 = n2;
			best->r2 = r2;
		}
	} else if (a >= c && b < d) {
		// New solution within budget, old one not - update
		best->p = p;
		best->n2 = n2;
		best->r2 = r2;
	} else if (a >= c && b >= d) {
		// Both within budget - prefer higher N/(R^2) ratio
		if (n2 * best->r2 * best->r2 > best->n2 * r2 * r2) {
			best->p = p;
			best->n2 = n2;
			best->r2 = r2;
		}
	}
	// else: a < c && b >= d - keep current best
}


void
hsw_ddi_calculate_wrpll(int clock /* in Hz */,
						unsigned *r2_out, unsigned *n2_out, unsigned *p_out)
{
	CALLED();

	uint64 freq2k = clock / 100;
	unsigned p, n2, r2;
	struct hsw_wrpll_rnp best = { 0, 0, 0 };
	unsigned budget = hsw_wrpll_get_budget_for_freq(clock);

	TRACE("%s: Calculating WRPLL for %d Hz (budget: %u)\n",
		  __func__, clock, budget);

	// Special case: 540 MHz pixel clock - bypass WRPLL
	if (freq2k == 5400000) {
		*n2_out = 2;
		*p_out = 1;
		*r2_out = 2;
		TRACE("%s: 540 MHz special case - bypassing WRPLL\n", __func__);
		return;
	}

	/*
	 * Find optimal R, N, P values:
	 * - Ref = LC_FREQ / R (REF_MIN <= Ref <= REF_MAX)
	 * - VCO = N * Ref (VCO_MIN <= VCO <= VCO_MAX)
	 * - Output = VCO / P
	 */

	// Iterate through valid R2 values (R2 = 2*R)
	for (r2 = LC_FREQ * 2 / REF_MAX + 1;
		 r2 <= LC_FREQ * 2 / REF_MIN;
	r2++) {

		// Iterate through valid N2 values (N2 = 2*N)
		for (n2 = VCO_MIN * r2 / LC_FREQ + 1;
			 n2 <= VCO_MAX * r2 / LC_FREQ;
		n2++) {

			// Iterate through valid P values
			for (p = P_MIN; p <= P_MAX; p += P_INC) {
				hsw_wrpll_update_rnp(freq2k, budget, r2, n2, p, &best);
			}
		}
	}

	*n2_out = best.n2;
	*p_out = best.p;
	*r2_out = best.r2;

	TRACE("%s: Best WRPLL params - N2: %u, P: %u, R2: %u\n",
		  __func__, best.n2, best.p, best.r2);
}


// ============================================================================
// Skylake+ WRPLL Algorithm (Gen 9+)
// ============================================================================


struct skl_wrpll_context {
	uint64 min_deviation;
	uint64 central_freq;
	uint64 dco_freq;
	unsigned int p;
};


// DCO frequency must be within +1%/-6% of the DCO central frequency
#define SKL_DCO_MAX_PDEVIATION	100
#define SKL_DCO_MAX_NDEVIATION	600


static void
skl_wrpll_context_init(struct skl_wrpll_context *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->min_deviation = UINT64_MAX;
}


static void
skl_wrpll_try_divider(struct skl_wrpll_context *ctx,
					  uint64 central_freq, uint64 dco_freq, unsigned int divider)
{
	uint64 deviation = ((uint64)10000 * AbsSubtr64(dco_freq, central_freq))
	/ central_freq;

	// Positive deviation (DCO > central)
	if (dco_freq >= central_freq) {
		if (deviation < SKL_DCO_MAX_PDEVIATION && deviation < ctx->min_deviation) {
			ctx->min_deviation = deviation;
			ctx->central_freq = central_freq;
			ctx->dco_freq = dco_freq;
			ctx->p = divider;

			TRACE("%s: Positive deviation %" B_PRIu64 " accepted "
			"(DCO: %" B_PRIu64 " Hz, Central: %" B_PRIu64 " Hz)\n",
				  __func__, deviation, dco_freq, central_freq);
		}
		// Negative deviation (DCO < central)
	} else if (deviation < SKL_DCO_MAX_NDEVIATION && deviation < ctx->min_deviation) {
		ctx->min_deviation = deviation;
		ctx->central_freq = central_freq;
		ctx->dco_freq = dco_freq;
		ctx->p = divider;

		TRACE("%s: Negative deviation %" B_PRIu64 " accepted "
		"(DCO: %" B_PRIu64 " Hz, Central: %" B_PRIu64 " Hz)\n",
			  __func__, deviation, dco_freq, central_freq);
	}
}


static void
skl_wrpll_get_multipliers(unsigned int p,
						  unsigned int *p0, unsigned int *p1, unsigned int *p2)
{
	// Decompose P into P0 * P1 * P2 factors
	// Valid P0 values: 1, 2, 3, 7
	// Valid P2 values: 1, 2, 3, 5

	if (p % 2 == 0) {
		// Even dividers
		unsigned int half = p / 2;

		if (half == 1 || half == 2 || half == 3 || half == 5) {
			*p0 = 2;
			*p1 = 1;
			*p2 = half;
		} else if (half % 2 == 0) {
			*p0 = 2;
			*p1 = half / 2;
			*p2 = 2;
		} else if (half % 3 == 0) {
			*p0 = 3;
			*p1 = half / 3;
			*p2 = 2;
		} else if (half % 7 == 0) {
			*p0 = 7;
			*p1 = half / 7;
			*p2 = 2;
		}
	} else {
		// Odd dividers: 3, 5, 7, 9, 15, 21, 35
		switch (p) {
			case 3:
			case 9:
				*p0 = 3;
				*p1 = 1;
				*p2 = p / 3;
				break;
			case 5:
			case 7:
				*p0 = p;
				*p1 = 1;
				*p2 = 1;
				break;
			case 15:
				*p0 = 3;
				*p1 = 1;
				*p2 = 5;
				break;
			case 21:
				*p0 = 7;
				*p1 = 1;
				*p2 = 3;
				break;
			case 35:
				*p0 = 7;
				*p1 = 1;
				*p2 = 5;
				break;
		}
	}
}


static void
skl_wrpll_params_populate(struct skl_wrpll_params *params,
						  uint64 afe_clock, int ref_clock, uint64 central_freq,
						  uint32 p0, uint32 p1, uint32 p2)
{
	uint64 dco_freq = p0 * p1 * p2 * afe_clock;

	TRACE("%s: AFE clock: %" B_PRIu64 " Hz, P0: %" B_PRIu32
	", P1: %" B_PRIu32 ", P2: %" B_PRIu32 "\n",
	__func__, afe_clock, p0, p1, p2);
	TRACE("%s: DCO frequency: %" B_PRIu64 " Hz\n", __func__, dco_freq);
	TRACE("%s: Reference clock: %g MHz\n", __func__, ref_clock / 1000.0f);

	// Encode central frequency
	switch (central_freq) {
		case 9600000000ULL:
			params->central_freq = 0;
			break;
		case 9000000000ULL:
			params->central_freq = 1;
			break;
		case 8400000000ULL:
			params->central_freq = 3;
			break;
	}

	// Encode P divider
	switch (p0) {
		case 1:
			params->pdiv = 0;
			break;
		case 2:
			params->pdiv = 1;
			break;
		case 3:
			params->pdiv = 2;
			break;
		case 7:
			params->pdiv = 4;
			break;
		default:
			ERROR("%s: Invalid P0 divider: %" B_PRIu32 "\n", __func__, p0);
			params->pdiv = 0;
	}

	// Encode K divider
	switch (p2) {
		case 5:
			params->kdiv = 0;
			break;
		case 2:
			params->kdiv = 1;
			break;
		case 3:
			params->kdiv = 2;
			break;
		case 1:
			params->kdiv = 3;
			break;
		default:
			ERROR("%s: Invalid P2/K divider: %" B_PRIu32 "\n", __func__, p2);
			params->kdiv = 0;
	}

	// Encode Q divider
	params->qdiv_ratio = p1;
	params->qdiv_mode = (p1 == 1) ? 0 : 1;

	// Calculate DCO integer and fractional parts
	// Convert Hz to MHz for calculation precision
	params->dco_integer = (uint64)dco_freq / ((uint64)ref_clock * 1000);
	params->dco_fraction = (
		((uint64)dco_freq / ((uint64)ref_clock / 1000) -
		(uint64)params->dco_integer * 1000000) * 0x8000) / 1000000;

		TRACE("%s: DCO integer: %" B_PRIu32 ", fraction: 0x%04" B_PRIx32 "\n",
			  __func__, params->dco_integer, params->dco_fraction);
}


#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))


bool
skl_ddi_calculate_wrpll(int clock /* in Hz */,
						int ref_clock, struct skl_wrpll_params *wrpll_params)
{
	CALLED();

	// AFE Clock is 5x pixel clock
	uint64 afe_clock = (uint64)clock * 5;

	// Three possible DCO central frequencies
	uint64 dco_central_freq[3] = {
		8400000000ULL,
		9000000000ULL,
		9600000000ULL
	};

	// Allowed divider values
	static const int even_dividers[] = {
		4,  6,  8, 10, 12, 14, 16, 18, 20, 24, 28, 30, 32, 36, 40, 42, 44,
		48, 52, 54, 56, 60, 64, 66, 68, 70, 72, 76, 78, 80, 84, 88, 90,
		92, 96, 98
	};
	static const int odd_dividers[] = { 3, 5, 7, 9, 15, 21, 35 };
	static const struct {
		const int *list;
		unsigned int n_dividers;
	} dividers[] = {
		{ even_dividers, ARRAY_SIZE(even_dividers) },
		{ odd_dividers, ARRAY_SIZE(odd_dividers) }
	};

	struct skl_wrpll_context ctx;
	unsigned int dco, d, i;
	unsigned int p0, p1, p2;

	TRACE("%s: Calculating WRPLL for %d Hz (AFE: %" B_PRIu64 " Hz, "
	"Ref: %d kHz)\n", __func__, clock, afe_clock, ref_clock);

	skl_wrpll_context_init(&ctx);

	// Try all combinations of DCO frequencies and dividers
	for (d = 0; d < ARRAY_SIZE(dividers); d++) {
		for (dco = 0; dco < ARRAY_SIZE(dco_central_freq); dco++) {
			for (i = 0; i < dividers[d].n_dividers; i++) {
				unsigned int p = dividers[d].list[i];
				uint64 dco_freq = p * afe_clock;

				skl_wrpll_try_divider(&ctx, dco_central_freq[dco],
									  dco_freq, p);

				// Early exit if we found perfect match (0 deviation)
				if (ctx.min_deviation == 0)
					goto skip_remaining_dividers;
			}
		}

		skip_remaining_dividers:
		// Prefer even dividers if solution found
		if (d == 0 && ctx.p)
			break;
	}

	if (!ctx.p) {
		ERROR("%s: No valid divider found for %d Hz\n", __func__, clock);
		return false;
	}

	TRACE("%s: Best divider P = %u (deviation: %" B_PRIu64 ")\n",
		  __func__, ctx.p, ctx.min_deviation);

	// Decompose P into P0, P1, P2 factors
	p0 = p1 = p2 = 0;
	skl_wrpll_get_multipliers(ctx.p, &p0, &p1, &p2);

	// Populate output parameters
	skl_wrpll_params_populate(wrpll_params, afe_clock, ref_clock,
							  ctx.central_freq, p0, p1, p2);

	TRACE("%s: WRPLL calculation successful\n", __func__);
	return true;
}
