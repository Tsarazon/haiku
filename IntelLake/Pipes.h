/*
 * Copyright 2011-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz, mmlr@mlotz.ch
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 *
 * Refactored 2025: Removed Gen < 9 support
 */
#ifndef INTEL_PIPE_H
#define INTEL_PIPE_H


#include <edid.h>

#include "intel_extreme.h"

#include "pll.h"
#include "PanelFitter.h"


#define MAX_PIPES	4	// Gen 9: 3 pipes (A/B/C), Gen 11+: 4 pipes (A/B/C/D)


void program_pipe_color_modes(uint32 colorMode);


class Pipe {
public:
									Pipe(pipe_index pipeIndex);
									~Pipe();

		pipe_index					Index()
										{ return fPipeIndex; }

		bool						IsEnabled();
		void						Enable(bool enable);

		void						Configure(display_mode* mode);
		void						ConfigureScalePos(display_mode* mode);
		void						ConfigureTimings(display_mode* mode,
										bool hardware = true,
										port_index portIndex = INTEL_PORT_ANY);
		void						ConfigureClocksSKL(
										const skl_wrpll_params& wrpll_params,
										uint32 pixelClock,
										port_index pllForPort,
										uint32* pllSel);
		uint32						TranscoderMode();

		::PanelFitter*				PFT()
										{ return fPanelFitter; }

private:
		void						_ConfigureTranscoder(display_mode* mode);

		bool						fHasTranscoder;
		PanelFitter*				fPanelFitter;

		pipe_index					fPipeIndex;

		addr_t						fPipeOffset;
		addr_t						fPlaneOffset;
};


#endif // INTEL_PIPE_H
