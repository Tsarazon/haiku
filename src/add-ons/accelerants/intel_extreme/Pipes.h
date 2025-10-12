/*
 * Copyright 2011-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz, mmlr@mlotz.ch
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 */
#ifndef INTEL_PIPE_H
#define INTEL_PIPE_H


#include <edid.h>

#include "intel_extreme.h"

#include "pll.h"
#include "FlexibleDisplayInterface.h"
#include "PanelFitter.h"


#define MAX_PIPES	4	// Gen 12+ supports 4 pipes (A, B, C, D)


// Global color mode configuration for all pipes
void program_pipe_color_modes(uint32 colorMode);


class Pipe {
public:
									Pipe(pipe_index pipeIndex);
									~Pipe();

		pipe_index					Index()
										{ return fPipeIndex; }

		bool						IsEnabled();
		void						Enable(bool enable);

		// Display mode configuration
		void						Configure(display_mode* mode);

		// FDI link configuration (Gen 6-8 with PCH)
		status_t					SetFDILink(
										const display_timing& timing,
										uint32 linkBandwidth,
										uint32 lanes,
										uint32 bitsPerPixel);

		// Scaling and positioning
		void						ConfigureScalePos(display_mode* mode);

		// Timing configuration
		void						ConfigureTimings(display_mode* mode,
										bool hardware = true,
										port_index portIndex = INTEL_PORT_ANY);

		// PLL configuration methods
		// Gen 6-8: Traditional divisor-based PLLs
		void						ConfigureClocks(
										const pll_divisors& divisors,
										uint32 pixelClock,
										uint32 extraFlags);

		// Gen 9-11: Skylake WRPLL
		void						ConfigureClocksSKL(
										const skl_wrpll_params& wrpll_params,
										uint32 pixelClock,
										port_index pllForPort,
										uint32* pllSel);

		// Gen 9+: Get transcoder mode (HDMI/DVI/DP)
		uint32						TranscoderMode();

		// Access to pipe components
		::FDILink*					FDI()
										{ return fFDILink; }
		::PanelFitter*				PFT()
										{ return fPanelFitter; }

private:
		// Transcoder configuration (Gen 6+)
		void						_ConfigureTranscoder(display_mode* mode);

		bool						fHasTranscoder;

		FDILink*					fFDILink;		// Gen 6-8 with PCH
		PanelFitter*				fPanelFitter;	// Gen 6+

		pipe_index					fPipeIndex;

		addr_t						fPipeOffset;	// Register offset for this pipe
		addr_t						fPlaneOffset;	// Plane offset for this pipe
};


#endif // INTEL_PIPE_H
