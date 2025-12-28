/*
 * Copyright 2011, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz, mmlr@mlotz.ch
 */


#include "PanelFitter.h"

#include <stdlib.h>
#include <string.h>
#include <Debug.h>

#include "accelerant.h"
#include "intel_extreme.h"


#undef TRACE
#define TRACE(x...) _sPrintf("intel_extreme: " x)
#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


// Gen 9+ uses Panel Scaler (PS), register base offset +0x100 from legacy PF
// See Intel PRM Vol 2c: Display Engine Registers - PS_CTRL, PS_WIN_POS, PS_WIN_SZ


PanelFitter::PanelFitter(pipe_index pipeIndex)
	:
	fRegisterBase(PCH_PANEL_FITTER_BASE_REGISTER + 0x100)
{
	if (pipeIndex == INTEL_PIPE_B)
		fRegisterBase += PCH_PANEL_FITTER_PIPE_OFFSET;
	if (pipeIndex == INTEL_PIPE_C)
		fRegisterBase += 2 * PCH_PANEL_FITTER_PIPE_OFFSET;

	TRACE("%s: requested scaler for pipe #%d\n", __func__, (int)pipeIndex);

	uint32 psCtrl = read32(fRegisterBase + PCH_PANEL_FITTER_CONTROL);
	if ((psCtrl & PANEL_FITTER_ENABLED) != 0) {
		TRACE("%s: scaler is enabled by BIOS\n", __func__);
	} else {
		TRACE("%s: scaler not setup by BIOS, enabling\n", __func__);
		psCtrl |= PANEL_FITTER_ENABLED;
		write32(fRegisterBase + PCH_PANEL_FITTER_CONTROL, psCtrl);
	}
}


PanelFitter::~PanelFitter()
{
}


bool
PanelFitter::IsEnabled()
{
	return (read32(fRegisterBase + PCH_PANEL_FITTER_CONTROL)
		& PANEL_FITTER_ENABLED) != 0;
}


void
PanelFitter::Enable(const display_timing& timing)
{
	_Enable(true);

	// TODO: program scaler mode (PS_CTRL bits 28:25) for proper filtering
	// Note: assuming BIOS setup, pipeA uses scalerA, etc.
	TRACE("%s: PS_CTRL: 0x%" B_PRIx32 "\n", __func__,
		read32(fRegisterBase + PCH_PANEL_FITTER_CONTROL));
	TRACE("%s: PS_WIN_POS: 0x%" B_PRIx32 "\n", __func__,
		read32(fRegisterBase + PCH_PANEL_FITTER_WINDOW_POS));

	// Window size must be written last - arms all other registers
	write32(fRegisterBase + PCH_PANEL_FITTER_WINDOW_SIZE,
		(timing.h_display << 16) | timing.v_display);
}


void
PanelFitter::Disable()
{
	_Enable(false);

	// Window size must be written last - arms all other registers
	write32(fRegisterBase + PCH_PANEL_FITTER_WINDOW_SIZE, 0);
}


void
PanelFitter::_Enable(bool enable)
{
	uint32 targetRegister = fRegisterBase + PCH_PANEL_FITTER_CONTROL;
	write32(targetRegister, (read32(targetRegister) & ~PANEL_FITTER_ENABLED)
		| (enable ? PANEL_FITTER_ENABLED : 0));
	read32(targetRegister);
}
