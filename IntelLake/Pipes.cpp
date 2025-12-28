/*
 * Copyright 2011-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz, mmlr@mlotz.ch
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 *
 * Refactored 2025: Removed Gen < 9 support (i830-Broadwell)
 */
#include "Pipes.h"

#include "accelerant.h"
#include "accelerant_protos.h"
#include "intel_extreme.h"

#undef TRACE
#define TRACE(x...) _sPrintf("intel_extreme: " x)
#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

#include <stdlib.h>
#include <string.h>

#include <new>


void
program_pipe_color_modes(uint32 colorMode)
{
	// Gen 9+ uses SKY color mask format
	// PRM Vol 7: Display, PLANE_CTL register
	write32(INTEL_DISPLAY_A_CONTROL, (read32(INTEL_DISPLAY_A_CONTROL)
		& ~(DISPLAY_CONTROL_COLOR_MASK_SKY | DISPLAY_CONTROL_GAMMA))
		| colorMode);
	write32(INTEL_DISPLAY_B_CONTROL, (read32(INTEL_DISPLAY_B_CONTROL)
		& ~(DISPLAY_CONTROL_COLOR_MASK_SKY | DISPLAY_CONTROL_GAMMA))
		| colorMode);
}


// #pragma mark - Pipe


Pipe::Pipe(pipe_index pipeIndex)
	:
	fHasTranscoder(false),
	fPanelFitter(NULL),
	fPipeIndex(pipeIndex),
	fPipeOffset(0),
	fPlaneOffset(0)
{
	// Gen 9+ pipe offsets per PRM Vol 2c Part 1: MMIO
	// Pipe A: base
	// Pipe B: +0x1000
	// Pipe C: +0x2000
	// Pipe D: +0xF000 (TGL+)
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
			// Pipe D plane offset depends on platform, typically no separate plane
			break;
		default:
			TRACE("Pipe A.\n");
			break;
	}

	// Gen 9+ (Skylake+): DDI directly connected, no FDI
	// Transcoder exists for all PCH configurations
	if (gInfo->shared_info->pch_info != INTEL_PCH_NONE) {
		fHasTranscoder = true;
		fPanelFitter = new(std::nothrow) PanelFitter(pipeIndex);
	}

	TRACE("Pipe Base: 0x%" B_PRIxADDR " Plane Base: 0x%" B_PRIxADDR "\n",
		fPipeOffset, fPlaneOffset);
}


Pipe::~Pipe()
{
	delete fPanelFitter;
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

	// TODO: Dithering configuration for Gen 9+
	// PRM Vol 7: PIPE_MISC register for dithering

	// Progressive mode only for now
	// PRM: PIPECONF bits [23:21] for interlace mode
	pipeControl = (pipeControl & ~(0x7 << 21)) | INTEL_PIPE_PROGRESSIVE;

	write32(INTEL_DISPLAY_A_PIPE_CONTROL + fPipeOffset, pipeControl);
	read32(INTEL_DISPLAY_A_PIPE_CONTROL + fPipeOffset);

	// Gen 9+: Pipe must be enabled before transcoder configuration
	// PRM Vol 7: Mode Set sequence
	addr_t pipeReg = INTEL_DISPLAY_A_PIPE_CONTROL + fPipeOffset;
	write32(pipeReg, read32(pipeReg) | INTEL_PIPE_ENABLED);
}


void
Pipe::_ConfigureTranscoder(display_mode* target)
{
	CALLED();

	TRACE("%s: fPipeOffset: 0x%" B_PRIxADDR"\n", __func__, fPipeOffset);

	// Gen 9+: Transcoder timing is configured via TRANS_* registers
	// which are the same as INTEL_DISPLAY_A_* registers (aliased in SKL+)
	// Timing already done in ConfigureTimings() for Gen 9+
	// This function kept for potential future eDP transcoder handling
}


uint32
Pipe::TranscoderMode()
{
	// Gen 9+ only
	// PRM Vol 2c: PIPE_DDI_FUNC_CTL register
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


void
Pipe::ConfigureScalePos(display_mode* target)
{
	CALLED();

	TRACE("%s: fPipeOffset: 0x%" B_PRIxADDR "\n", __func__, fPipeOffset);

	if (target == NULL) {
		ERROR("%s: Invalid display mode!\n", __func__);
		return;
	}

	// Gen 9+: Pipe source size
	// PRM Vol 7: PIPESRC register
	write32(INTEL_DISPLAY_A_PIPE_SIZE + fPipeOffset,
		((uint32)(target->timing.h_display - 1) << 16)
			| ((uint32)target->timing.v_display - 1));

	// Gen 9+ DDI: Set plane size
	// PRM Vol 7: PLANE_SIZE register
	// Note: height and width are swapped compared to pipe size!
	write32(INTEL_DISPLAY_A_IMAGE_SIZE + fPipeOffset,
		((uint32)(target->timing.v_display - 1) << 16)
			| ((uint32)target->timing.h_display - 1));
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

	// Gen 9+: Transcoder timing registers
	// PRM Vol 2c: TRANS_HTOTAL, TRANS_HBLANK, TRANS_HSYNC, etc.
	// These are aliased to INTEL_DISPLAY_A_* for pipe A
	if (!fHasTranscoder || hardware) {
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

	if (fHasTranscoder && hardware) {
		_ConfigureTranscoder(target);
	}
}


void
Pipe::ConfigureClocksSKL(const skl_wrpll_params& wrpll_params, uint32 pixelClock,
	port_index pllForPort, uint32* pllSel)
{
	CALLED();

	// Gen 9+ DPLL configuration
	// PRM Vol 2c: DPLL_CTRL1, DPLL_CTRL2, DPLL_CFGCR1/2

	// Find PLL assigned to port by BIOS
	uint32 portSel = read32(SKL_DPLL_CTRL2);
	*pllSel = 0xff;

	// PRM: DPLL_CTRL2 bits for port-to-PLL mapping
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

	// Only program PLLs in HDMI/DVI mode (non-DP)
	// DP mode uses link rate from training
	portSel = read32(SKL_DPLL_CTRL1);
	if ((portSel & (1 << (*pllSel * 6 + 5))) && *pllSel) {
		// Enable programming for this PLL
		write32(SKL_DPLL_CTRL1, portSel | (1 << (*pllSel * 6)));

		// PRM: DPLL_CFGCR1 format:
		// [31] - DCO integer enable
		// [23:9] - DCO fraction
		// [8:0] - DCO integer
		write32(SKL_DPLL1_CFGCR1 + (*pllSel - 1) * 8,
			(1 << 31) |
			(wrpll_params.dco_fraction << 9) |
			wrpll_params.dco_integer);

		// PRM: DPLL_CFGCR2 format:
		// [15:8] - Qdiv ratio
		// [7] - Qdiv mode
		// [6:5] - Kdiv
		// [4:2] - Pdiv
		// [1:0] - Central frequency
		write32(SKL_DPLL1_CFGCR2 + (*pllSel - 1) * 8,
			(wrpll_params.qdiv_ratio << 8) |
			(wrpll_params.qdiv_mode << 7) |
			(wrpll_params.kdiv << 5) |
			(wrpll_params.pdiv << 2) |
			wrpll_params.central_freq);

		read32(SKL_DPLL1_CFGCR1 + (*pllSel - 1) * 8);
		read32(SKL_DPLL1_CFGCR2 + (*pllSel - 1) * 8);

		spin(5);

		// Check PLL lock status
		// PRM: DPLL_STATUS bit [pll*8] indicates lock
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

	// PRM Vol 7: Display enable sequence
	// 1. Enable pipe
	// 2. Wait for vblank
	// 3. Enable plane

	if (enable) {
		write32(pipeReg, read32(pipeReg) | INTEL_PIPE_ENABLED);
		wait_for_vblank();
		write32(planeReg, read32(planeReg) | DISPLAY_CONTROL_ENABLED);
	} else {
		// Disable sequence: plane first, then pipe
		write32(planeReg, read32(planeReg) & ~DISPLAY_CONTROL_ENABLED);
		wait_for_vblank();
		// Gen 9+: Keep pipe enabled during DDI operations
		// Only disable when fully shutting down display
	}

	// Flush cached PCI writes
	read32(INTEL_DISPLAY_A_BASE);
}
