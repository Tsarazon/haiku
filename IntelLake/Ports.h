/*
 * Copyright 2011-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz, mmlr@mlotz.ch
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 *
 * Refactored for Gen 9+ only support (Skylake and newer)
 */
#ifndef INTEL_PORTS_H
#define INTEL_PORTS_H


#include <dp.h>
#include <edid.h>

#include "intel_extreme.h"

#include "Pipes.h"
#include "pll.h"


#define MAX_PORTS	20	// a generous upper bound

struct pll_limits;
struct i2c_bus;

enum port_type {
	INTEL_PORT_TYPE_ANY,		// wildcard for lookup functions
	INTEL_PORT_TYPE_DDI,		// Digital Display Interface (Gen 9+)
	INTEL_PORT_TYPE_eDP,		// Embedded DisplayPort (via DDI)
	INTEL_PORT_TYPE_DP,			// DisplayPort (via DDI)
	INTEL_PORT_TYPE_HDMI		// HDMI (via DDI)
};


class Port {
public:
									Port(port_index index,
										const char* baseName);
virtual								~Port();

virtual	uint32						Type() const = 0;
		const char*					PortName() const
										{ return fPortName; }

		port_index					PortIndex() const
										{ return fPortIndex; }

virtual	bool						IsConnected() = 0;

virtual	status_t					SetPipe(Pipe* pipe);
		::Pipe*						GetPipe()
										{ return fPipe; };

virtual	status_t					Power(bool enabled);

		bool						HasEDID();
virtual	status_t					GetEDID(edid1_info* edid,
										bool forceRead = false);
virtual	status_t					SetupI2c(struct i2c_bus *bus);
virtual status_t					SetupI2cFallback(struct i2c_bus *bus);

virtual	status_t					GetPLLLimits(pll_limits& limits);

virtual status_t					SetDisplayMode(display_mode* mode,
										uint32 colorMode) { return B_ERROR; };

virtual pipe_index					PipePreference();

protected:
		void						_SetName(const char* name);

static	status_t					_GetI2CSignals(void* cookie, int* _clock,
										int* _data);
static	status_t					_SetI2CSignals(void* cookie, int clock,
										int data);
		bool						_IsPortInVBT(uint32* foundIndex = NULL);
		bool						_IsDisplayPortInVBT();
		bool						_IsHdmiInVBT();
		bool						_IsEDPPort();
		addr_t						_DDCPin();
		status_t					_SetupDpAuxI2c(struct i2c_bus *bus);

		ssize_t						_DpAuxTransfer(dp_aux_msg* message);
		ssize_t						_DpAuxTransfer(uint8* transmitBuffer, uint8 transmitSize,
										uint8* receiveBuffer, uint8 receiveSize);
		status_t					_DpAuxSendReceive(uint32 slave_address,
										const uint8 *writeBuffer, size_t writeLength,
										uint8 *readBuffer, size_t readLength);
static 	status_t					_DpAuxSendReceiveHook(const struct i2c_bus *bus,
										uint32 slave_address, const uint8 *writeBuffer,
										size_t writeLength, uint8 *readBuffer,
										size_t readLength);
		aux_channel					_DpAuxChannel();

		display_mode				fCurrentMode;
		Pipe*						fPipe;

		status_t					fEDIDState;
		edid1_info					fEDIDInfo;

private:
virtual	addr_t						_DDCRegister() = 0;
virtual addr_t						_PortRegister() = 0;

		port_index					fPortIndex;
		char*						fPortName;
};


// Gen 9+: Digital Display Interface (DDI)
// This is the primary port class for Skylake and newer
class DigitalDisplayInterface : public Port {
public:
									DigitalDisplayInterface(
										port_index index = INTEL_PORT_A,
										const char* baseName = "Digital Display Interface");

virtual	uint32						Type() const
										{ return INTEL_PORT_TYPE_DDI; }

virtual	status_t					Power(bool enabled);

virtual	status_t					SetPipe(Pipe* pipe);
virtual	status_t					SetupI2c(i2c_bus *bus);
virtual status_t					SetupI2cFallback(struct i2c_bus *bus);

virtual	bool						IsConnected();

virtual status_t					SetDisplayMode(display_mode* mode,
										uint32 colorMode);

protected:
virtual	addr_t						_DDCRegister();
virtual addr_t						_PortRegister();

private:
		uint8						fMaxLanes;

		status_t					_SetPortLinkGen8(const display_timing& timing,
										uint32 pllSel);
};


#endif // INTEL_PORTS_H
