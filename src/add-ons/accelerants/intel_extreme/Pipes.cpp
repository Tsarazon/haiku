/*
 * Copyright 2011-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz, mmlr@mlotz.ch
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 */
#include "Pipes.h"

#include "accelerant.h"
#include "accelerant_protos.h"
#include "intel_extreme.h"

#include <stdlib.h>
#include <string.h>

#include <new>


#undef TRACE
#define TRACE_PIPE
#ifdef TRACE_PIPE
#	define TRACE(x...) _sPrintf("intel_extreme: " x)
#else
#	define TRACE(x...) ;
#endif

#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED(x...) TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


void
program_pipe_color_modes(uint32 colorMode)
{
	// All pipes get the same color mode (Gen 6+ unified approach)
	if (gInfo->shared_info->device_type.InFamily(INTEL_FAMILY_LAKE)) {
		write32(INTEL_DISPLAY_A_CONTROL, (read32(INTEL_DISPLAY_A_CONTROL)
			& ~(DISPLAY_CONTROL_COLOR_MASK_SKY | DISPLAY_CONTROL_GAMMA))
			| colorMode);
		write32(INTEL_DISPLAY_B_CONTROL, (read32(INTEL_DISPLAY_B_CONTROL)
			& ~(DISPLAY_CONTROL_COLOR_MASK_SKY | DISPLAY_CONTROL_GAMMA))
			| colorMode);
	} else {
		write32(INTEL_DISPLAY_A_CONTROL, (read32(INTEL_DISPLAY_A_CONTROL)
			& ~(DISPLAY_CONTROL_COLOR_MASK | DISPLAY_CONTROL_GAMMA))
			| colorMode);
		write32(INTEL_DISPLAY_B_CONTROL, (read32(INTEL_DISPLAY_B_CONTROL)
			& ~(DISPLAY_CONTROL_COLOR_MASK | DISPLAY_CONTROL_GAMMA))
			| colorMode);
	}
}


// #pragma mark - Pipe


Pipe::Pipe(pipe_index pipeIndex)
	:
	fHasTranscoder(false),
	fFDILink(NULL),
	fPanelFitter(NULL),
	fPipeIndex(pipeIndex),
	fPipeOffset(0),
	fPlaneOffset(0)
{
	switch (pipeIndex) {
		case INTEL_PIPE_B:
			TRACE("Pipe B.\n");
			fPipeOffset = 0x1000;
			fPlaneOffset = INTEL_PLANE_OFFSET;
			break;
		case INTEL_PIPE_C:
			TRACE("Pipe C.\n");
			fPipeOffset = 0x2000;
			fPlaneOffset = INTEL_PLANE_OFFSET * 2;
			break;
		case INTEL_PIPE_D:
			TRACE("Pipe D.\n");
			fPipeOffset = 0xf000;
			break;
		default:
			TRACE("Pipe A.\n");
			break;
	}

	// Gen 6+: Check for FDI and DDI requirements
	if (gInfo->shared_info->pch_info != INTEL_PCH_NONE
		&& gInfo->shared_info->device_type.Generation() <= 8) {
		TRACE("%s: Pipe is routed through FDI\n", __func__);
		fFDILink = new(std::nothrow) FDILink(pipeIndex);
	}

	if (gInfo->shared_info->pch_info != INTEL_PCH_NONE) {
		fHasTranscoder = true;
		fPanelFitter = new(std::nothrow) PanelFitter(pipeIndex);
	}

	TRACE("Pipe Base: 0x%" B_PRIxADDR " Plane Base: 0x%" B_PRIxADDR "\n",
			fPipeOffset, fPlaneOffset);
}


Pipe::~Pipe()
{
}


bool
Pipe::IsEnabled()
{
	CALLED();
	return (read32(INTEL_DISPLAY_A_PIPE_CONTROL + fPipeOffset)
		& INTEL_PIPE_ENABLED) != 0;
}


void
Pipe::Configure(display_mode* mode)
{
	uint32 pipeControl = read32(INTEL_DISPLAY_A_PIPE_CONTROL + fPipeOffset);

	// Gen 6+ dithering configuration
	pipeControl |= (INTEL_PIPE_DITHER_EN | INTEL_PIPE_DITHER_TYPE_SP);

	// Progressive mode for Gen 6+
	pipeControl = (pipeControl & ~(0x7 << 21)) | INTEL_PIPE_PROGRESSIVE;

	write32(INTEL_DISPLAY_A_PIPE_CONTROL + fPipeOffset, pipeControl);
	read32(INTEL_DISPLAY_A_PIPE_CONTROL + fPipeOffset);

	// Gen 6+: Enable pipe before PLL configuration
	addr_t pipeReg = INTEL_DISPLAY_A_PIPE_CONTROL + fPipeOffset;
	write32(pipeReg, read32(pipeReg) | INTEL_PIPE_ENABLED);
}


void
Pipe::_ConfigureTranscoder(display_mode* target)
{
	CALLED();

	TRACE("%s: fPipeOffset: 0x%" B_PRIxADDR"\n", __func__, fPipeOffset);

	if (gInfo->shared_info->device_type.Generation() < 9) {
		// Gen 6-8: Configure transcoder timing registers
		write32(INTEL_TRANSCODER_A_HTOTAL + fPipeOffset,
			((uint32)(target->timing.h_total - 1) << 16)
			| ((uint32)target->timing.h_display - 1));
		write32(INTEL_TRANSCODER_A_HBLANK + fPipeOffset,
			((uint32)(target->timing.h_total - 1) << 16)
			| ((uint32)target->timing.h_display - 1));
		write32(INTEL_TRANSCODER_A_HSYNC + fPipeOffset,
			((uint32)(target->timing.h_sync_end - 1) << 16)
			| ((uint32)target->timing.h_sync_start - 1));

		write32(INTEL_TRANSCODER_A_VTOTAL + fPipeOffset,
			((uint32)(target->timing.v_total - 1) << 16)
			| ((uint32)target->timing.v_display - 1));
		write32(INTEL_TRANSCODER_A_VBLANK + fPipeOffset,
			((uint32)(target->timing.v_total - 1) << 16)
			| ((uint32)target->timing.v_display - 1));
		write32(INTEL_TRANSCODER_A_VSYNC + fPipeOffset,
			((uint32)(target->timing.v_sync_end - 1) << 16)
			| ((uint32)target->timing.v_sync_start - 1));
	}
	// Gen 9+: Timing handled in ConfigureTimings()
}


uint32
Pipe::TranscoderMode()
{
	if (gInfo->shared_info->device_type.Generation() < 9) {
		ERROR("%s: TranscoderMode only supported on Gen 9+\n", __func__);
		return 0;
	}

	TRACE("%s: trans conf reg: 0x%" B_PRIx32 "\n", __func__,
		read32(DDI_SKL_TRANS_CONF_A + fPipeOffset));
	TRACE("%s: trans DDI func ctl reg: 0x%" B_PRIx32 "\n", __func__,
		read32(PIPE_DDI_FUNC_CTL_A + fPipeOffset));

	uint32 value = (read32(PIPE_DDI_FUNC_CTL_A + fPipeOffset) & PIPE_DDI_MODESEL_MASK)
		>> PIPE_DDI_MODESEL_SHIFT;

	switch (value) {
		case PIPE_DDI_MODE_DVI:
			TRACE("%s: Transcoder uses DVI mode\n", __func__);
			break;
		case PIPE_DDI_MODE_DP_SST:
			TRACE("%s: Transcoder uses DP SST mode\n", __func__);
			break;
		case PIPE_DDI_MODE_DP_MST:
			TRACE("%s: Transcoder uses DP MST mode\n", __func__);
			break;
		default:
			TRACE("%s: Transcoder uses HDMI mode\n", __func__);
			break;
	}
	return value;
}


status_t
Pipe::SetFDILink(const display_timing& timing, uint32 linkBandwidth, 
	uint32 lanes, uint32 bitsPerPixel)
{
	TRACE("%s: fPipeOffset: 0x%" B_PRIxADDR "\n", __func__, fPipeOffset);
	TRACE("%s: FDI/PIPE link reference clock is %gMhz\n", __func__, 
		linkBandwidth / 1000.0f);

	TRACE("%s: FDI/PIPE M1 data before: 0x%" B_PRIx32 "\n", __func__, 
		read32(PCH_FDI_PIPE_A_DATA_M1 + fPipeOffset));
	TRACE("%s: FDI/PIPE N1 data before: 0x%" B_PRIx32 "\n", __func__, 
		read32(PCH_FDI_PIPE_A_DATA_N1 + fPipeOffset));

	if ((bitsPerPixel < 18) || (bitsPerPixel > 36)) {
		ERROR("%s: FDI/PIPE illegal colordepth set.\n", __func__);
		return B_ERROR;
	}
	TRACE("%s: FDI/PIPE link colordepth: %" B_PRIu32 "\n", __func__, bitsPerPixel);

	if (lanes > 4) {
		ERROR("%s: FDI/PIPE illegal number of lanes set.\n", __func__);
		return B_ERROR;
	}
	TRACE("%s: FDI/PIPE link with %" B_PRIx32 " lane(s) in use\n", __func__, lanes);

	// Setup Data M/N
	uint64 linkspeed = lanes * linkBandwidth * 8;
	uint64 ret_n = 1;
	while(ret_n < linkspeed) {
		ret_n *= 2;
	}
	if (ret_n > 0x800000) {
		ret_n = 0x800000;
	}
	uint64 ret_m = timing.pixel_clock * ret_n * bitsPerPixel / linkspeed;
	while ((ret_n > 0xffffff) || (ret_m > 0xffffff)) {
		ret_m >>= 1;
		ret_n >>= 1;
	}

	write32(PCH_FDI_PIPE_A_DATA_M1 + fPipeOffset, ret_m | FDI_PIPE_MN_TU_SIZE_MASK);
	write32(PCH_FDI_PIPE_A_DATA_N1 + fPipeOffset, ret_n);

	// Setup Link M/N
	linkspeed = linkBandwidth;
	ret_n = 1;
	while(ret_n < linkspeed) {
		ret_n *= 2;
	}
	if (ret_n > 0x800000) {
		ret_n = 0x800000;
	}
	ret_m = timing.pixel_clock * ret_n / linkspeed;
	while ((ret_n > 0xffffff) || (ret_m > 0xffffff)) {
		ret_m >>= 1;
		ret_n >>= 1;
	}
	write32(PCH_FDI_PIPE_A_LINK_M1 + fPipeOffset, ret_m);
	write32(PCH_FDI_PIPE_A_LINK_N1 + fPipeOffset, ret_n);

	TRACE("%s: FDI/PIPE M1 data after: 0x%" B_PRIx32 "\n", __func__, 
		read32(PCH_FDI_PIPE_A_DATA_M1 + fPipeOffset));
	TRACE("%s: FDI/PIPE N1 data after: 0x%" B_PRIx32 "\n", __func__, 
		read32(PCH_FDI_PIPE_A_DATA_N1 + fPipeOffset));

	return B_OK;
}


void
Pipe::ConfigureScalePos(display_mode* target)
{
	CALLED();

	TRACE("%s: fPipeOffset: 0x%" B_PRIxADDR "\n", __func__, fPipeOffset);

	if (target == NULL) {
		ERROR("%s: Invalid display mode!\n", __func__);
		return;
	}

	// Set the image size
	write32(INTEL_DISPLAY_A_PIPE_SIZE + fPipeOffset,
		((uint32)(target->timing.h_display - 1) << 16)
			| ((uint32)target->timing.v_display - 1));

	// Gen 6+: Set plane size (required for DDI platforms)
	if (gInfo->shared_info->device_type.HasDDI()) {
		// WARNING: height and width are swapped in this register
		write32(INTEL_DISPLAY_A_IMAGE_SIZE + fPipeOffset,
			((uint32)(target->timing.v_display - 1) << 16)
			| ((uint32)target->timing.h_display - 1));
	}
}


void
Pipe::ConfigureTimings(display_mode* target, bool hardware, port_index portIndex)
{
	CALLED();

	TRACE("%s(%d): fPipeOffset: 0x%" B_PRIxADDR"\n", __func__, hardware,
		fPipeOffset);

	if (target == NULL) {
		ERROR("%s: Invalid display mode!\n", __func__);
		return;
	}

	// For Gen 6+, configure transcoder or display timing based on DDI presence
	if (!fHasTranscoder || hardware) {
		// Gen 9+: These registers are part of the transcoder
		write32(INTEL_DISPLAY_A_HTOTAL + fPipeOffset,
			((uint32)(target->timing.h_total - 1) << 16)
			| ((uint32)target->timing.h_display - 1));
		write32(INTEL_DISPLAY_A_HBLANK + fPipeOffset,
			((uint32)(target->timing.h_total - 1) << 16)
			| ((uint32)target->timing.h_display - 1));
		write32(INTEL_DISPLAY_A_HSYNC + fPipeOffset,
			((uint32)(target->timing.h_sync_end - 1) << 16)
			| ((uint32)target->timing.h_sync_start - 1));

		write32(INTEL_DISPLAY_A_VTOTAL + fPipeOffset,
			((uint32)(target->timing.v_total - 1) << 16)
			| ((uint32)target->timing.v_display - 1));
		write32(INTEL_DISPLAY_A_VBLANK + fPipeOffset,
			((uint32)(target->timing.v_total - 1) << 16)
			| ((uint32)target->timing.v_display - 1));
		write32(INTEL_DISPLAY_A_VSYNC + fPipeOffset,
			((uint32)(target->timing.v_sync_end - 1) << 16)
			| ((uint32)target->timing.v_sync_start - 1));
	}

	ConfigureScalePos(target);

	// Gen 6-7: Transcoder not applicable for eDP on SNB/IVB
	if ((gInfo->shared_info->device_type.InGroup(INTEL_GROUP_SNB) ||
		 gInfo->shared_info->device_type.InGroup(INTEL_GROUP_IVB)) &&
		(portIndex == INTEL_PORT_A)) {
		return;
	}

	if (fHasTranscoder && hardware) {
		_ConfigureTranscoder(target);
	}
}


void
Pipe::ConfigureClocks(const pll_divisors& divisors, uint32 pixelClock,
	uint32 extraFlags)
{
	CALLED();

	addr_t pllControl = INTEL_DISPLAY_A_PLL;
	addr_t pllMD = INTEL_DISPLAY_A_PLL_MD;
	addr_t pllDivisorA = INTEL_DISPLAY_A_PLL_DIVISOR_0;
	addr_t pllDivisorB = INTEL_DISPLAY_A_PLL_DIVISOR_1;

	if (fPipeIndex == INTEL_PIPE_B) {
		pllControl = INTEL_DISPLAY_B_PLL;
		pllMD = INTEL_DISPLAY_B_PLL_MD;
		pllDivisorA = INTEL_DISPLAY_B_PLL_DIVISOR_0;
		pllDivisorB = INTEL_DISPLAY_B_PLL_DIVISOR_1;
	}

	// Disable PLL
	write32(pllControl, read32(pllControl) & ~DISPLAY_PLL_ENABLED);
	spin(150);

	// Gen 6+ PLL programming
	write32(pllDivisorA, (((divisors.n - 2) << DISPLAY_PLL_N_DIVISOR_SHIFT)
			& DISPLAY_PLL_N_DIVISOR_MASK)
		| (((divisors.m1 - 2) << DISPLAY_PLL_M1_DIVISOR_SHIFT)
			& DISPLAY_PLL_M1_DIVISOR_MASK)
		| (((divisors.m2 - 2) << DISPLAY_PLL_M2_DIVISOR_SHIFT)
			& DISPLAY_PLL_M2_DIVISOR_MASK));
	write32(pllDivisorB, (((divisors.n - 2) << DISPLAY_PLL_N_DIVISOR_SHIFT)
			& DISPLAY_PLL_N_DIVISOR_MASK)
		| (((divisors.m1 - 2) << DISPLAY_PLL_M1_DIVISOR_SHIFT)
			& DISPLAY_PLL_M1_DIVISOR_MASK)
		| (((divisors.m2 - 2) << DISPLAY_PLL_M2_DIVISOR_SHIFT)
			& DISPLAY_PLL_M2_DIVISOR_MASK));

	uint32 pll = DISPLAY_PLL_ENABLED | DISPLAY_PLL_NO_VGA_CONTROL | extraFlags;

	// Gen 6+ configuration
	pll |= ((1 << (divisors.p1 - 1)) << DISPLAY_PLL_POST1_DIVISOR_SHIFT)
		& DISPLAY_PLL_9xx_POST1_DIVISOR_MASK;

	// SandyBridge specific
	if (gInfo->shared_info->device_type.Generation() == 6) {
		pll |= ((1 << (divisors.p1 - 1)) << DISPLAY_PLL_SNB_FP0_POST1_DIVISOR_SHIFT)
			& DISPLAY_PLL_SNB_FP0_POST1_DIVISOR_MASK;
	}

	if (divisors.p2 == 5 || divisors.p2 == 7)
		pll |= DISPLAY_PLL_DIVIDE_HIGH;

	// Configure PLL while keeping it disabled
	write32(pllControl, pll & ~DISPLAY_PLL_ENABLED & ~DISPLAY_PLL_NO_VGA_CONTROL);
	read32(pllControl);
	spin(150);

	// Enable PLL
	write32(pllControl, pll);
	read32(pllControl);
	spin(150);

	// Gen 6: Configure PLL selection for transcoder
	if (gInfo->shared_info->device_type.Generation() >= 6) {
		uint32 pllSel = read32(SNB_DPLL_SEL);
		TRACE("Old PLL selection: 0x%" B_PRIx32 "\n", pllSel);

		uint32 shift = 0;
		uint32 pllIndex = 0;

		if (fPipeIndex == INTEL_PIPE_A) {
			shift = 0;
			pllIndex = 0;
			TRACE("Route PLL A to transcoder A\n");
		} else if (fPipeIndex == INTEL_PIPE_B) {
			shift = 4;
			pllIndex = 1;
			TRACE("Route PLL B to transcoder B\n");
		} else {
			ERROR("Attempting to configure PLL for unhandled pipe\n");
			return;
		}

		pllSel &= ~(0xF << shift);
		pllSel |= (8 | pllIndex) << shift;

		TRACE("New PLL selection: 0x%" B_PRIx32 "\n", pllSel);
		write32(SNB_DPLL_SEL, pllSel);
	}
}


void
Pipe::ConfigureClocksSKL(const skl_wrpll_params& wrpll_params, uint32 pixelClock,
	port_index pllForPort, uint32* pllSel)
{
	CALLED();

	// Find PLL assigned by BIOS
	uint32 portSel = read32(SKL_DPLL_CTRL2);
	*pllSel = 0xff;

	switch (pllForPort) {
		case INTEL_PORT_A:
			*pllSel = (portSel & 0x0006) >> 1;
			break;
		case INTEL_PORT_B:
			*pllSel = (portSel & 0x0030) >> 4;
			break;
		case INTEL_PORT_C:
			*pllSel = (portSel & 0x0180) >> 7;
			break;
		case INTEL_PORT_D:
			*pllSel = (portSel & 0x0c00) >> 10;
			break;
		case INTEL_PORT_E:
			*pllSel = (portSel & 0x6000) >> 13;
			break;
		default:
			TRACE("No port selected!\n");
			return;
	}
	TRACE("PLL selected is %" B_PRIx32 "\n", *pllSel);

	TRACE("Skylake DPLL_CFGCR1 0x%" B_PRIx32 "\n",
		read32(SKL_DPLL1_CFGCR1 + (*pllSel - 1) * 8));
	TRACE("Skylake DPLL_CFGCR2 0x%" B_PRIx32 "\n",
		read32(SKL_DPLL1_CFGCR2 + (*pllSel - 1) * 8));

	// Program PLL for non-DP modes
	portSel = read32(SKL_DPLL_CTRL1);
	if ((portSel & (1 << (*pllSel * 6 + 5))) && *pllSel) {
		// Enable programming on our PLL
		write32(SKL_DPLL_CTRL1, portSel | (1 << (*pllSel * 6)));

		write32(SKL_DPLL1_CFGCR1 + (*pllSel - 1) * 8,
			1 << 31 |
			wrpll_params.dco_fraction << 9 |
			wrpll_params.dco_integer);
		write32(SKL_DPLL1_CFGCR2 + (*pllSel - 1) * 8,
			 wrpll_params.qdiv_ratio << 8 |
			 wrpll_params.qdiv_mode << 7 |
			 wrpll_params.kdiv << 5 |
			 wrpll_params.pdiv << 2 |
			 wrpll_params.central_freq);
		read32(SKL_DPLL1_CFGCR1 + (*pllSel - 1) * 8);
		read32(SKL_DPLL1_CFGCR2 + (*pllSel - 1) * 8);

		spin(5);

		if (read32(SKL_DPLL_STATUS) & (1 << (*pllSel * 8))) {
			TRACE("Programmed PLL; PLL is locked\n");
		} else {
			TRACE("Programmed PLL; PLL did not lock\n");
		}

		TRACE("Skylake DPLL_CFGCR1 now: 0x%" B_PRIx32 "\n",
			read32(SKL_DPLL1_CFGCR1 + (*pllSel - 1) * 8));
		TRACE("Skylake DPLL_CFGCR2 now: 0x%" B_PRIx32 "\n",
			read32(SKL_DPLL1_CFGCR2 + (*pllSel - 1) * 8));
	} else {
		TRACE("PLL programming not needed, skipping.\n");
	}

	TRACE("Skylake DPLL_CTRL1: 0x%" B_PRIx32 "\n", read32(SKL_DPLL_CTRL1));
	TRACE("Skylake DPLL_CTRL2: 0x%" B_PRIx32 "\n", read32(SKL_DPLL_CTRL2));
	TRACE("Skylake DPLL_STATUS: 0x%" B_PRIx32 "\n", read32(SKL_DPLL_STATUS));
}


void
Pipe::Enable(bool enable)
{
	CALLED();

	addr_t pipeReg = INTEL_DISPLAY_A_PIPE_CONTROL + fPipeOffset;
	addr_t planeReg = INTEL_DISPLAY_A_CONTROL + fPlaneOffset;

	if (enable) {
		write32(pipeReg, read32(pipeReg) | INTEL_PIPE_ENABLED);
		wait_for_vblank();
		write32(planeReg, read32(planeReg) | DISPLAY_CONTROL_ENABLED);

		// Gen 6+: Enable default watermarks (PCH CPT)
		if (gInfo->shared_info->pch_info == INTEL_PCH_CPT) {
			if (fPipeOffset == 0)
				write32(INTEL_DISPLAY_A_PIPE_WATERMARK, 0x0783818);
			else
				write32(INTEL_DISPLAY_B_PIPE_WATERMARK, 0x0783818);
		}
	} else {
		write32(planeReg, read32(planeReg) & ~DISPLAY_CONTROL_ENABLED);
		wait_for_vblank();
		// Gen 6+: Keep pipe enabled for link training if needed
	}

	read32(INTEL_DISPLAY_A_BASE);
}
