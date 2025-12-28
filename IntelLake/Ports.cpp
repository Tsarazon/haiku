/*
 * Copyright 2006-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Michael Lotz, mmlr@mlotz.ch
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 *		Rudolf Cornelissen, ruud@highsand-juicylake.nl
 *
 * Refactored for Gen 9+ only support (Skylake and newer)
 */


#include "Ports.h"

#include <ddc.h>
#include <dp_raw.h>
#include <stdlib.h>
#include <string.h>
#include <Debug.h>
#include <KernelExport.h>

#undef TRACE
#define TRACE(x...) _sPrintf("intel_extreme: " x)
#define ERROR(x...) _sPrintf("intel_extreme: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

#include "accelerant.h"
#include "accelerant_protos.h"
#include "intel_extreme.h"

#include "FlexibleDisplayInterface.h"
#include "PanelFitter.h"
#include "TigerLakePLL.h"

#include <new>


static bool
wait_for_set(addr_t address, uint32 mask, uint32 timeout)
{
	int interval = 50;
	uint32 i = 0;
	for (i = 0; i <= timeout; i += interval) {
		spin(interval);
		if ((read32(address) & mask) != 0)
			return true;
	}
	return false;
}


static bool
wait_for_clear(addr_t address, uint32 mask, uint32 timeout)
{
	int interval = 50;
	uint32 i = 0;
	for (i = 0; i <= timeout; i += interval) {
		spin(interval);
		if ((read32(address) & mask) == 0)
			return true;
	}
	return false;
}


static uint32
wait_for_clear_status(addr_t address, uint32 mask, uint32 timeout)
{
	int interval = 50;
	uint32 i = 0;
	uint32 status = 0;
	for (i = 0; i <= timeout; i += interval) {
		spin(interval);
		status = read32(address);
		if ((status & mask) == 0)
			return status;
	}
	return status;
}


// #pragma mark - Port base class


Port::Port(port_index index, const char* baseName)
	:
	fPipe(NULL),
	fEDIDState(B_NO_INIT),
	fPortIndex(index),
	fPortName(NULL)
{
	char portID[2];
	portID[0] = 'A' + index - INTEL_PORT_A;
	portID[1] = 0;

	char buffer[32];
	buffer[0] = 0;

	strlcat(buffer, baseName, sizeof(buffer));
	strlcat(buffer, " ", sizeof(buffer));
	strlcat(buffer, portID, sizeof(buffer));
	fPortName = strdup(buffer);
}


Port::~Port()
{
	free(fPortName);
}


bool
Port::HasEDID()
{
	if (fEDIDState == B_NO_INIT)
		GetEDID(NULL);

	return fEDIDState == B_OK;
}


status_t
Port::SetPipe(Pipe* pipe)
{
	CALLED();

	if (pipe == NULL) {
		ERROR("%s: Invalid pipe provided!\n", __func__);
		return B_ERROR;
	}

	uint32 portRegister = _PortRegister();
	if (portRegister == 0) {
		ERROR("%s: Invalid PortRegister (0x%" B_PRIx32 ") for %s\n", __func__,
			portRegister, PortName());
		return B_ERROR;
	}

	if (fPipe != NULL) {
		ERROR("%s: Can't reassign display pipe (yet)\n", __func__);
		return B_ERROR;
	}

	switch (pipe->Index()) {
		case INTEL_PIPE_B:
			TRACE("%s: Assigning %s (0x%" B_PRIx32 ") to pipe B\n", __func__,
				PortName(), portRegister);
			break;
		case INTEL_PIPE_C:
			TRACE("%s: Assigning %s (0x%" B_PRIx32 ") to pipe C\n", __func__,
				PortName(), portRegister);
			break;
		case INTEL_PIPE_D:
			TRACE("%s: Assigning %s (0x%" B_PRIx32 ") to pipe D\n", __func__,
				PortName(), portRegister);
			break;
		default:
			TRACE("%s: Assigning %s (0x%" B_PRIx32 ") to pipe A\n", __func__,
				PortName(), portRegister);
			break;
	}

	// Gen 9+: Pipe selection is done via DDI_FUNC_CTL registers
	// The actual pipe-to-port mapping is handled in DigitalDisplayInterface::SetPipe()
	fPipe = pipe;

	if (fPipe == NULL)
		return B_NO_MEMORY;

	// Disable display pipe until modesetting enables it
	if (fPipe->IsEnabled())
		fPipe->Enable(false);

	read32(portRegister);

	return B_OK;
}


status_t
Port::Power(bool enabled)
{
	if (fPipe == NULL) {
		ERROR("%s: Setting power mode without assigned pipe!\n", __func__);
		return B_ERROR;
	}

	fPipe->Enable(enabled);

	return B_OK;
}


status_t
Port::GetEDID(edid1_info* edid, bool forceRead)
{
	CALLED();

	if (fEDIDState == B_NO_INIT || forceRead) {
		TRACE("%s: trying to read EDID\n", PortName());

		i2c_bus bus;
		if (SetupI2c(&bus) != B_OK)
			return fEDIDState;

		fEDIDState = ddc2_read_edid1(&bus, &fEDIDInfo, NULL, NULL);

		if (fEDIDState == B_OK) {
			TRACE("%s: found EDID information!\n", PortName());
			edid_dump(&fEDIDInfo);
		} else if (SetupI2cFallback(&bus) == B_OK) {
			fEDIDState = ddc2_read_edid1(&bus, &fEDIDInfo, NULL, NULL);

			if (fEDIDState == B_OK) {
				TRACE("%s: found EDID information!\n", PortName());
				edid_dump(&fEDIDInfo);
			}
		}
	}

	if (fEDIDState != B_OK) {
		TRACE("%s: no EDID information found.\n", PortName());
		return fEDIDState;
	}

	if (edid != NULL)
		memcpy(edid, &fEDIDInfo, sizeof(edid1_info));

	return B_OK;
}


status_t
Port::SetupI2c(i2c_bus *bus)
{
	addr_t ddcRegister = _DDCRegister();
	if (ddcRegister == 0) {
		TRACE("%s: no DDC register found\n", PortName());
		fEDIDState = B_ERROR;
		return fEDIDState;
	}

	TRACE("%s: using ddc @ 0x%" B_PRIxADDR "\n", PortName(), ddcRegister);

	ddc2_init_timing(bus);
	bus->cookie = (void*)ddcRegister;
	bus->set_signals = &_SetI2CSignals;
	bus->get_signals = &_GetI2CSignals;

	return B_OK;
}


status_t
Port::SetupI2cFallback(i2c_bus *bus)
{
	return B_ERROR;
}


status_t
Port::GetPLLLimits(pll_limits& limits)
{
	return B_ERROR;
}


pipe_index
Port::PipePreference()
{
	CALLED();

	// Gen 9+: All ports use DDI, scan pipes to find the one connected to current port
	// PRM Vol 2c: PIPE_DDI_FUNC_CTL register defines port selection
	if (gInfo->shared_info->device_type.HasDDI()) {
		uint32 pipeState = 0;
		for (uint32 pipeCnt = 0; pipeCnt < 4; pipeCnt++) {
			switch (pipeCnt) {
				case 0:
					pipeState = read32(PIPE_DDI_FUNC_CTL_A);
					break;
				case 1:
					pipeState = read32(PIPE_DDI_FUNC_CTL_B);
					break;
				case 2:
					pipeState = read32(PIPE_DDI_FUNC_CTL_C);
					break;
				default:
					pipeState = read32(PIPE_DDI_FUNC_CTL_EDP);
					break;
			}

			if ((((pipeState & PIPE_DDI_SELECT_MASK) >> PIPE_DDI_SELECT_SHIFT) + 1)
				== (uint32)PortIndex()) {
				switch (pipeCnt) {
					case 0:
						return INTEL_PIPE_A;
					case 1:
						return INTEL_PIPE_B;
					case 2:
						return INTEL_PIPE_C;
					default:
						return INTEL_PIPE_D;
				}
			}
		}
	}

	return INTEL_PIPE_ANY;
}


status_t
Port::_GetI2CSignals(void* cookie, int* _clock, int* _data)
{
	addr_t ioRegister = (addr_t)cookie;
	uint32 value = read32(ioRegister);

	*_clock = (value & I2C_CLOCK_VALUE_IN) != 0;
	*_data = (value & I2C_DATA_VALUE_IN) != 0;

	return B_OK;
}


status_t
Port::_SetI2CSignals(void* cookie, int clock, int data)
{
	addr_t ioRegister = (addr_t)cookie;
	uint32 value;

	// Gen 9+: preserve reserved bits manually
	value = read32(ioRegister) & I2C_RESERVED;

	// if we send clk or data, we always send low logic level;
	// if we want to send high level, we actually receive and let the
	// external pullup resistors create the high level on the bus.
	value |= I2C_DATA_VALUE_MASK;  // sets data = 0, always latch
	value |= I2C_CLOCK_VALUE_MASK; // sets clock = 0, always latch

	if (data != 0)
		value |= I2C_DATA_DIRECTION_MASK;
	else
		value |= I2C_DATA_DIRECTION_MASK | I2C_DATA_DIRECTION_OUT;

	if (clock != 0)
		value |= I2C_CLOCK_DIRECTION_MASK;
	else
		value |= I2C_CLOCK_DIRECTION_MASK | I2C_CLOCK_DIRECTION_OUT;

	write32(ioRegister, value);
	read32(ioRegister);
		// make sure the PCI bus has flushed the write

	return B_OK;
}


bool
Port::_IsPortInVBT(uint32* foundIndex)
{
	// check VBT mapping
	bool found = false;
	const uint32 deviceConfigCount = gInfo->shared_info->device_config_count;
	for (uint32 i = 0; i < deviceConfigCount; i++) {
		child_device_config& config = gInfo->shared_info->device_configs[i];
		if (config.dvo_port > DVO_PORT_HDMII) {
			ERROR("%s: DVO port unknown\n", __func__);
			continue;
		}
		dvo_port port = (dvo_port)config.dvo_port;
		switch (PortIndex()) {
			case INTEL_PORT_A:
				found = port == DVO_PORT_HDMIA || port == DVO_PORT_DPA;
				break;
			case INTEL_PORT_B:
				found = port == DVO_PORT_HDMIB || port == DVO_PORT_DPB;
				break;
			case INTEL_PORT_C:
				found = port == DVO_PORT_HDMIC || port == DVO_PORT_DPC;
				break;
			case INTEL_PORT_D:
				found = port == DVO_PORT_HDMID || port == DVO_PORT_DPD;
				break;
			case INTEL_PORT_E:
				found = port == DVO_PORT_HDMIE || port == DVO_PORT_DPE || port == DVO_PORT_CRT;
				break;
			case INTEL_PORT_F:
				found = port == DVO_PORT_HDMIF || port == DVO_PORT_DPF;
				break;
			case INTEL_PORT_G:
				found = port == DVO_PORT_HDMIG || port == DVO_PORT_DPG;
				break;
			default:
				ERROR("%s: DDI port unknown\n", __func__);
				break;
		}
		if (found) {
			if (foundIndex != NULL)
				*foundIndex = i;
			break;
		}
	}
	return found;
}


bool
Port::_IsDisplayPortInVBT()
{
	uint32 foundIndex = 0;
	if (!_IsPortInVBT(&foundIndex))
		return false;
	child_device_config& config = gInfo->shared_info->device_configs[foundIndex];
	return config.aux_channel > 0 && (config.device_type & DEVICE_TYPE_DISPLAYPORT_OUTPUT) != 0;
}


bool
Port::_IsHdmiInVBT()
{
	uint32 foundIndex = 0;
	if (!_IsPortInVBT(&foundIndex))
		return false;
	child_device_config& config = gInfo->shared_info->device_configs[foundIndex];
	return config.ddc_pin > 0 && ((config.device_type & DEVICE_TYPE_NOT_HDMI_OUTPUT) == 0
			|| (config.device_type & DEVICE_TYPE_TMDS_DVI_SIGNALING) != 0);
}


bool
Port::_IsEDPPort()
{
	uint32 foundIndex = 0;
	if (!_IsPortInVBT(&foundIndex))
		return false;
	child_device_config& config = gInfo->shared_info->device_configs[foundIndex];
	return (config.device_type & (DEVICE_TYPE_INTERNAL_CONNECTOR | DEVICE_TYPE_DISPLAYPORT_OUTPUT))
		== (DEVICE_TYPE_INTERNAL_CONNECTOR | DEVICE_TYPE_DISPLAYPORT_OUTPUT);
}


addr_t
Port::_DDCPin()
{
	uint32 foundIndex = 0;
	if (!_IsPortInVBT(&foundIndex))
		return 0;

	child_device_config& config = gInfo->shared_info->device_configs[foundIndex];

	// PRM Vol 2c: GPIO pin mappings for DDC
	// Gen 11+ (Ice Lake PCH and newer): TGL/ICL pin mapping
	if (gInfo->shared_info->pch_info >= INTEL_PCH_ICP) {
		switch (config.ddc_pin) {
			case 1:
				return INTEL_I2C_IO_A;
			case 2:
				return INTEL_I2C_IO_B;
			case 3:
				return INTEL_I2C_IO_C;
			case 4:
				return INTEL_I2C_IO_I;
			case 5:
				return INTEL_I2C_IO_J;
			case 6:
				return INTEL_I2C_IO_K;
			case 7:
				return INTEL_I2C_IO_L;
			case 8:
				return INTEL_I2C_IO_M;
			case 9:
				return INTEL_I2C_IO_N;
			default:
				return 0;
		}
	}

	// Gen 9.5 (Cannon Point PCH - Coffee Lake): CNP pin mapping
	if (gInfo->shared_info->pch_info >= INTEL_PCH_CNP) {
		switch (config.ddc_pin) {
			case 1:
				return INTEL_I2C_IO_A;
			case 2:
				return INTEL_I2C_IO_B;
			case 3:
				return INTEL_I2C_IO_D;
			case 4:
				return INTEL_I2C_IO_C;
			default:
				return 0;
		}
	}

	// Gen 9 (Skylake/Kaby Lake): SKL pin mapping
	// PRM Vol 2c: Skylake GPIO_CTL registers
	if (gInfo->shared_info->device_type.Generation() == 9) {
		switch (config.ddc_pin) {
			case 4:
				return INTEL_I2C_IO_D;
			case 5:
				return INTEL_I2C_IO_E;
			case 6:
				return INTEL_I2C_IO_F;
			default:
				return 0;
		}
	}

	return 0;
}


status_t
Port::_SetupDpAuxI2c(i2c_bus *bus)
{
	CALLED();

	ddc2_init_timing(bus);
	bus->cookie = this;
	bus->send_receive = &_DpAuxSendReceiveHook;

	// Gen 11+: Power well management for AUX channels
	// PRM Vol 2c: ICL_PWR_WELL_CTL_AUX2
	if (gInfo->shared_info->device_type.Generation() >= 11) {
		uint32 value = read32(ICL_PWR_WELL_CTL_AUX2);
		if ((value & HSW_PWR_WELL_CTL_STATE(0)) != 0)
			return B_OK;

		write32(ICL_PWR_WELL_CTL_AUX2, value | HSW_PWR_WELL_CTL_REQ(0));
		if (!wait_for_set(ICL_PWR_WELL_CTL_AUX2, HSW_PWR_WELL_CTL_STATE(0), 1000))
			ERROR("%s: %s AUX didn't power on within 1000us!\n", __func__, PortName());
	}

	return B_OK;
}


status_t
Port::_DpAuxSendReceive(uint32 slaveAddress,
	const uint8 *writeBuffer, size_t writeLength, uint8 *readBuffer, size_t readLength)
{
	size_t transferLength = 16;

	dp_aux_msg message;
	memset(&message, 0, sizeof(message));

	if (writeBuffer != NULL) {
		message.address = slaveAddress;
		message.buffer = NULL;
		message.request = DP_AUX_I2C_WRITE;
		message.size = 0;
		ssize_t result = _DpAuxTransfer(&message);
		if (result < 0)
			return result;

		for (size_t i = 0; i < writeLength;) {
			message.buffer = (void*)(writeBuffer + i);
			message.size = min_c(transferLength, writeLength - i);
			// Middle-Of-Transmission on final transaction
			if (writeLength - i > transferLength)
				message.request |= DP_AUX_I2C_MOT;
			else
				message.request &= ~DP_AUX_I2C_MOT;

			for (int attempt = 0; attempt < 7; attempt++) {
				ssize_t result = _DpAuxTransfer(&message);
				if (result < 0) {
					ERROR("%s: aux_ch transaction failed!\n", __func__);
					return result;
				}

				switch (message.reply & DP_AUX_I2C_REPLY_MASK) {
					case DP_AUX_I2C_REPLY_ACK:
						goto nextWrite;
					case DP_AUX_I2C_REPLY_NACK:
						TRACE("%s: aux i2c nack\n", __func__);
						return B_IO_ERROR;
					case DP_AUX_I2C_REPLY_DEFER:
						TRACE("%s: aux i2c defer\n", __func__);
						snooze(400);
						break;
					default:
						TRACE("%s: aux invalid I2C reply: 0x%02x\n",
							__func__, message.reply);
						return B_ERROR;
				}
			}
nextWrite:
			if (result < 0)
				return result;
			i += message.size;
		}
	}

	if (readBuffer != NULL) {
		message.address = slaveAddress;
		message.buffer = NULL;
		message.request = DP_AUX_I2C_READ;
		message.size = 0;
		ssize_t result = _DpAuxTransfer(&message);
		if (result < 0)
			return result;

		for (size_t i = 0; i < readLength;) {
			message.buffer = readBuffer + i;
			message.size = min_c(transferLength, readLength - i);
			// Middle-Of-Transmission on final transaction
			if (readLength - i > transferLength)
				message.request |= DP_AUX_I2C_MOT;
			else
				message.request &= ~DP_AUX_I2C_MOT;

			for (int attempt = 0; attempt < 7; attempt++) {
				result = _DpAuxTransfer(&message);
				if (result < 0) {
					ERROR("%s: aux_ch transaction failed!\n", __func__);
					return result;
				}

				switch (message.reply & DP_AUX_I2C_REPLY_MASK) {
					case DP_AUX_I2C_REPLY_ACK:
						goto nextRead;
					case DP_AUX_I2C_REPLY_NACK:
						TRACE("%s: aux i2c nack\n", __func__);
						return B_IO_ERROR;
					case DP_AUX_I2C_REPLY_DEFER:
						TRACE("%s: aux i2c defer\n", __func__);
						snooze(400);
						break;
					default:
						TRACE("%s: aux invalid I2C reply: 0x%02x\n",
							__func__, message.reply);
						return B_ERROR;
				}
			}
nextRead:
			if (result < 0)
				return result;
			if (result == 0)
				i += message.size;
		}
	}

	return B_OK;
}


status_t
Port::_DpAuxSendReceiveHook(const struct i2c_bus *bus, uint32 slaveAddress,
	const uint8 *writeBuffer, size_t writeLength, uint8 *readBuffer, size_t readLength)
{
	CALLED();
	Port* port = (Port*)bus->cookie;
	return port->_DpAuxSendReceive(slaveAddress, writeBuffer, writeLength, readBuffer, readLength);
}


ssize_t
Port::_DpAuxTransfer(dp_aux_msg* message)
{
	CALLED();
	if (message == NULL) {
		ERROR("%s: DP message is invalid!\n", __func__);
		return B_ERROR;
	}

	if (message->size > 16) {
		ERROR("%s: Too many bytes! (%" B_PRIuSIZE ")\n", __func__,
			message->size);
		return B_ERROR;
	}

	uint8 transmitSize = message->size > 0 ? 4 : 3;
	uint8 receiveSize;

	switch (message->request & ~DP_AUX_I2C_MOT) {
		case DP_AUX_NATIVE_WRITE:
		case DP_AUX_I2C_WRITE:
		case DP_AUX_I2C_WRITE_STATUS_UPDATE:
			transmitSize += message->size;
			break;
	}

	// If not bare address, check for buffer
	if (message->size > 0 && message->buffer == NULL) {
		ERROR("%s: DP message uninitalized buffer!\n", __func__);
		return B_ERROR;
	}

	uint8 receiveBuffer[20];
	uint8 transmitBuffer[20];
	transmitBuffer[0] = (message->request << 4) | ((message->address >> 16) & 0xf);
	transmitBuffer[1] = (message->address >> 8) & 0xff;
	transmitBuffer[2] = message->address & 0xff;
	transmitBuffer[3] = message->size != 0 ? (message->size - 1) : 0;

	uint8 retry;
	for (retry = 0; retry < 7; retry++) {
		ssize_t result = B_ERROR;
		switch (message->request & ~DP_AUX_I2C_MOT) {
			case DP_AUX_NATIVE_WRITE:
			case DP_AUX_I2C_WRITE:
			case DP_AUX_I2C_WRITE_STATUS_UPDATE:
				receiveSize = 2;
				if (message->buffer != NULL)
					memcpy(transmitBuffer + 4, message->buffer, message->size);
				result = _DpAuxTransfer(transmitBuffer,
					transmitSize, receiveBuffer, receiveSize);
				if (result > 0) {
					message->reply = receiveBuffer[0] >> 4;
					if (result > 1)
						result = min_c(receiveBuffer[1], message->size);
					else
						result = message->size;
				}
				break;
			case DP_AUX_NATIVE_READ:
			case DP_AUX_I2C_READ:
				receiveSize = message->size + 1;
				result = _DpAuxTransfer(transmitBuffer,
					transmitSize, receiveBuffer, receiveSize);
				if (result > 0) {
					message->reply = receiveBuffer[0] >> 4;
					result--;
					if (message->buffer != NULL)
						memcpy(message->buffer, receiveBuffer + 1, result);
				}
				break;
			default:
				ERROR("%s: Unknown dp_aux_msg request!\n", __func__);
				return B_ERROR;
		}

		if (result == B_BUSY)
			continue;
		else if (result < B_OK)
			return result;

		switch (message->reply & DP_AUX_NATIVE_REPLY_MASK) {
			case DP_AUX_NATIVE_REPLY_ACK:
				return B_OK;
			case DP_AUX_NATIVE_REPLY_NACK:
				TRACE("%s: aux native reply nack\n", __func__);
				return B_IO_ERROR;
			case DP_AUX_NATIVE_REPLY_DEFER:
				TRACE("%s: aux reply defer received. Snoozing.\n", __func__);
				snooze(400);
				break;
			default:
				TRACE("%s: aux invalid native reply: 0x%02x\n", __func__,
					message->reply);
				return B_IO_ERROR;
		}
	}

	ERROR("%s: IO Error. %" B_PRIu8 " attempts\n", __func__, retry);
	return B_IO_ERROR;
}


ssize_t
Port::_DpAuxTransfer(uint8* transmitBuffer, uint8 transmitSize,
	uint8* receiveBuffer, uint8 receiveSize)
{
	addr_t channelControl;
	addr_t channelData[5];
	aux_channel channel = _DpAuxChannel();
	TRACE("%s: %s DpAuxChannel: 0x%x\n", __func__, PortName(), channel);

	// Gen 9+: Always use DP_AUX_CH_CTL registers
	// PRM Vol 2c: DP_AUX_CH_CTL_x registers
	channelControl = DP_AUX_CH_CTL(channel);
	for (int i = 0; i < 5; i++)
		channelData[i] = DP_AUX_CH_DATA(channel, i);

	if (transmitSize > 20 || receiveSize > 20)
		return E2BIG;

	int tries = 0;
	while ((read32(channelControl) & INTEL_DP_AUX_CTL_BUSY) != 0) {
		if (tries++ == 3) {
			ERROR("%s: %s AUX channel is busy!\n", __func__, PortName());
			return B_BUSY;
		}
		snooze(1000);
	}

	// Gen 9+: AUX channel control configuration
	// PRM Vol 2c: DP_AUX_CH_CTL bit definitions
	uint32 sendControl = INTEL_DP_AUX_CTL_BUSY
		| INTEL_DP_AUX_CTL_DONE
		| INTEL_DP_AUX_CTL_INTERRUPT
		| INTEL_DP_AUX_CTL_TIMEOUT_ERROR
		| INTEL_DP_AUX_CTL_TIMEOUT_1600us
		| INTEL_DP_AUX_CTL_RECEIVE_ERROR
		| (transmitSize << INTEL_DP_AUX_CTL_MSG_SIZE_SHIFT)
		| INTEL_DP_AUX_CTL_FW_SYNC_PULSE_SKL(32)
		| INTEL_DP_AUX_CTL_SYNC_PULSE_SKL(32);

	uint8 retry;
	uint32 status = 0;
	for (retry = 0; retry < 5; retry++) {
		for (uint8 i = 0; i < transmitSize;) {
			uint8 index = i / 4;
			uint32 data = ((uint32)transmitBuffer[i++]) << 24;
			if (i < transmitSize)
				data |= ((uint32)transmitBuffer[i++]) << 16;
			if (i < transmitSize)
				data |= ((uint32)transmitBuffer[i++]) << 8;
			if (i < transmitSize)
				data |= transmitBuffer[i++];
			write32(channelData[index], data);
		}
		write32(channelControl, sendControl);

		// wait 10 ms reading channelControl until INTEL_DP_AUX_CTL_BUSY clears
		status = wait_for_clear_status(channelControl, INTEL_DP_AUX_CTL_BUSY, 10000);
		if ((status & INTEL_DP_AUX_CTL_BUSY) != 0) {
			ERROR("%s: %s AUX channel stayed busy for 10000us!\n", __func__, PortName());
		}

		write32(channelControl, status | INTEL_DP_AUX_CTL_DONE | INTEL_DP_AUX_CTL_TIMEOUT_ERROR
			| INTEL_DP_AUX_CTL_RECEIVE_ERROR);

		if ((status & INTEL_DP_AUX_CTL_TIMEOUT_ERROR) != 0)
			continue;
		if ((status & INTEL_DP_AUX_CTL_RECEIVE_ERROR) != 0) {
			snooze(400);
			continue;
		}
		if ((status & INTEL_DP_AUX_CTL_DONE) != 0)
			goto done;
	}

	if ((status & INTEL_DP_AUX_CTL_DONE) == 0) {
		ERROR("%s: Busy Error. %" B_PRIu8 " attempts\n", __func__, retry);
		return B_BUSY;
	}

done:
	if ((status & INTEL_DP_AUX_CTL_RECEIVE_ERROR) != 0)
		return B_IO_ERROR;
	if ((status & INTEL_DP_AUX_CTL_TIMEOUT_ERROR) != 0)
		return B_TIMEOUT;

	uint8 bytes = (status & INTEL_DP_AUX_CTL_MSG_SIZE_MASK) >> INTEL_DP_AUX_CTL_MSG_SIZE_SHIFT;
	if (bytes == 0 || bytes > 20) {
		ERROR("%s: Status byte count incorrect %u\n", __func__, bytes);
		return B_BUSY;
	}
	if (bytes > receiveSize)
		bytes = receiveSize;

	for (uint8 i = 0; i < bytes;) {
		uint32 data = read32(channelData[i / 4]);
		receiveBuffer[i++] = data >> 24;
		if (i < bytes)
			receiveBuffer[i++] = data >> 16;
		if (i < bytes)
			receiveBuffer[i++] = data >> 8;
		if (i < bytes)
			receiveBuffer[i++] = data;
	}

	return bytes;
}


aux_channel
Port::_DpAuxChannel()
{
	uint32 foundIndex = 0;
	if (!_IsPortInVBT(&foundIndex))
		return AUX_CH_A;

	child_device_config& config = gInfo->shared_info->device_configs[foundIndex];
	switch (config.aux_channel) {
		case DP_AUX_B:
			return AUX_CH_B;
		case DP_AUX_C:
			return AUX_CH_C;
		case DP_AUX_D:
			return AUX_CH_D;
		case DP_AUX_E:
			return AUX_CH_E;
		case DP_AUX_F:
			return AUX_CH_F;
		default:
			return AUX_CH_A;
	}
}


// #pragma mark - Digital Display Interface (DDI)
// Gen 9+: All display outputs use DDI


DigitalDisplayInterface::DigitalDisplayInterface(port_index index,
		const char* baseName)
	:
	Port(index, baseName),
	fMaxLanes(4)
{
}


addr_t
DigitalDisplayInterface::_PortRegister()
{
	// PRM Vol 2c: DDI_BUF_CTL register addresses
	switch (PortIndex()) {
		case INTEL_PORT_A:
			return DDI_BUF_CTL_A;
		case INTEL_PORT_B:
			return DDI_BUF_CTL_B;
		case INTEL_PORT_C:
			return DDI_BUF_CTL_C;
		case INTEL_PORT_D:
			return DDI_BUF_CTL_D;
		case INTEL_PORT_E:
			return DDI_BUF_CTL_E;
		case INTEL_PORT_F:
			// Gen 9.5+ (different from Gen 9 Skylake)
			if (gInfo->shared_info->device_type.Generation() > 9
				|| !gInfo->shared_info->device_type.InGroup(INTEL_GROUP_SKY))
				return DDI_BUF_CTL_F;
			return 0;
		case INTEL_PORT_G:
			// Gen 12+ only
			if (gInfo->shared_info->device_type.Generation() >= 12)
				return DDI_BUF_CTL_G;
			return 0;
		default:
			return 0;
	}
}


addr_t
DigitalDisplayInterface::_DDCRegister()
{
	return Port::_DDCPin();
}


status_t
DigitalDisplayInterface::Power(bool enabled)
{
	if (fPipe == NULL) {
		ERROR("%s: Setting power without assigned pipe!\n", __func__);
		return B_ERROR;
	}
	TRACE("%s: %s DDI enabled: %s\n", __func__, PortName(),
		enabled ? "true" : "false");

	fPipe->Enable(enabled);

	return B_OK;
}


status_t
DigitalDisplayInterface::SetPipe(Pipe* pipe)
{
	CALLED();

	if (pipe == NULL) {
		ERROR("%s: Invalid pipe provided!\n", __PRETTY_FUNCTION__);
		return B_ERROR;
	}

	if (fPipe != NULL) {
		ERROR("%s: Can't reassign display pipe (yet)\n", __PRETTY_FUNCTION__);
		return B_ERROR;
	}

	// Gen 9+: DDI port-to-pipe mapping is indirect via PIPE_DDI_FUNC_CTL
	// The BIOS typically sets this up, we just read the current assignment
	TRACE("%s: Assuming pipe %d is assigned by BIOS to port %d\n", __PRETTY_FUNCTION__,
		pipe->Index(), PortIndex());

	fPipe = pipe;

	if (fPipe == NULL)
		return B_NO_MEMORY;

	// Disable display pipe until modesetting enables it
	if (fPipe->IsEnabled())
		fPipe->Enable(false);

	return B_OK;
}


status_t
DigitalDisplayInterface::SetupI2c(i2c_bus *bus)
{
	CALLED();

	const uint32 deviceConfigCount = gInfo->shared_info->device_config_count;
	if (deviceConfigCount > 0) {
		if (!_IsDisplayPortInVBT())
			return Port::SetupI2c(bus);
	}

	return _SetupDpAuxI2c(bus);
}


status_t
DigitalDisplayInterface::SetupI2cFallback(i2c_bus *bus)
{
	CALLED();

	const uint32 deviceConfigCount = gInfo->shared_info->device_config_count;
	if (deviceConfigCount > 0 && _IsDisplayPortInVBT() && _IsHdmiInVBT()) {
		return Port::SetupI2c(bus);
	}

	return B_ERROR;
}


bool
DigitalDisplayInterface::IsConnected()
{
	addr_t portRegister = _PortRegister();

	TRACE("%s: %s PortRegister: 0x%" B_PRIxADDR "\n", __func__, PortName(),
		portRegister);

	if (portRegister == 0) {
		TRACE("%s: Port not implemented\n", __func__);
		return false;
	}

	// Determine max lanes based on DDI_A_4_LANES configuration
	// PRM Vol 2c: DDI_BUF_CTL_A bit 4 (DDI_A_4_LANES)
	fMaxLanes = 4;
	if (gInfo->shared_info->device_type.Generation() == 9
		&& gInfo->shared_info->device_type.InGroup(INTEL_GROUP_SKY)) {
		// Skylake: check DDI_A_4_LANES bit
		if ((read32(DDI_BUF_CTL_A) & DDI_A_4_LANES) != 0) {
			switch (PortIndex()) {
				case INTEL_PORT_A:
					fMaxLanes = 4;
					break;
				case INTEL_PORT_E:
					fMaxLanes = 0;
					break;
				default:
					fMaxLanes = 4;
					break;
			}
		} else {
			switch (PortIndex()) {
				case INTEL_PORT_A:
					fMaxLanes = 2;
					break;
				case INTEL_PORT_E:
					fMaxLanes = 2;
					break;
				default:
					fMaxLanes = 4;
					break;
			}
		}
	}

	const uint32 deviceConfigCount = gInfo->shared_info->device_config_count;
	if (deviceConfigCount > 0) {
		// check VBT mapping
		if (!_IsPortInVBT()) {
			TRACE("%s: %s: port not found in VBT\n", __func__, PortName());
			return false;
		}
		TRACE("%s: %s: port found in VBT\n", __func__, PortName());
	}

	TRACE("%s: %s Maximum Lanes: %" B_PRId8 "\n", __func__,
		PortName(), fMaxLanes);

	// fetch EDID but determine 'in use' later
	bool edidDetected = HasEDID();

	// Internal panel detection (eDP on port A)
	uint32 pipeState = 0;
	if ((gInfo->shared_info->device_type.IsMobile() || _IsEDPPort())
		&& (PortIndex() == INTEL_PORT_A)) {
		// Gen 11 and older: check PIPE_DDI_FUNC_CTL_EDP
		if (gInfo->shared_info->device_type.Generation() < 12) {
			pipeState = read32(PIPE_DDI_FUNC_CTL_EDP);
			TRACE("%s: PIPE_DDI_FUNC_CTL_EDP: 0x%" B_PRIx32 "\n", __func__, pipeState);
			if (!(pipeState & PIPE_DDI_FUNC_CTL_ENABLE)) {
				TRACE("%s: Laptop, but eDP port down\n", __func__);
				return false;
			}
		}

		if (edidDetected)
			return true;

		if (gInfo->shared_info->has_vesa_edid_info) {
			TRACE("%s: Laptop. Using VESA edid info\n", __func__);
			memcpy(&fEDIDInfo, &gInfo->shared_info->vesa_edid_info, sizeof(edid1_info));
			if (fEDIDState != B_OK) {
				fEDIDState = B_OK;
				edid_dump(&fEDIDInfo);
			}
			return true;
		}

		if (gInfo->shared_info->got_vbt) {
			TRACE("%s: Laptop. No VESA EDID, but force enabled as we have a VBT\n", __func__);
			return true;
		}

		TRACE("%s: No (panel) type info found, assuming not connected\n", __func__);
		return false;
	}

	// External display detection: scan pipes to find one connected to this port
	for (uint32 pipeCnt = 0; pipeCnt < 3; pipeCnt++) {
		switch (pipeCnt) {
			case 1:
				pipeState = read32(PIPE_DDI_FUNC_CTL_B);
				break;
			case 2:
				pipeState = read32(PIPE_DDI_FUNC_CTL_C);
				break;
			default:
				pipeState = read32(PIPE_DDI_FUNC_CTL_A);
				break;
		}

		if ((((pipeState & PIPE_DDI_SELECT_MASK) >> PIPE_DDI_SELECT_SHIFT) + 1) == (uint32)PortIndex()) {
			TRACE("%s: PIPE_DDI_FUNC_CTL nr %" B_PRIx32 ": 0x%" B_PRIx32 "\n", __func__, pipeCnt + 1, pipeState);
			// Check if BIOS enabled this output
			if (pipeState & PIPE_DDI_FUNC_CTL_ENABLE) {
				TRACE("%s: Connected\n", __func__);
				return true;
			}
		}
	}

	// Check if EDID was detected but pipe not enabled
	if (edidDetected) {
		for (uint32 pipeCnt = 0; pipeCnt < 3; pipeCnt++) {
			uint32 pipeReg = 0;
			switch (pipeCnt) {
				case 1:
					pipeReg = PIPE_DDI_FUNC_CTL_B;
					break;
				case 2:
					pipeReg = PIPE_DDI_FUNC_CTL_C;
					break;
				default:
					pipeReg = PIPE_DDI_FUNC_CTL_A;
					break;
			}
			pipeState = read32(pipeReg);
			if ((pipeState & PIPE_DDI_FUNC_CTL_ENABLE) == 0) {
				TRACE("%s: Connected but port down\n", __func__);
				return false;
			}
			return true;
		}
		TRACE("%s: No pipe available, ignoring connected screen\n", __func__);
	}

	TRACE("%s: Not connected\n", __func__);
	return false;
}


status_t
DigitalDisplayInterface::_SetPortLinkGen8(const display_timing& timing, uint32 pllSel)
{
	// PRM Vol 2c: DDI Link M/N programming
	uint32 linkBandwidth = 270000; // default 270MHz (2.7 Gbps / 10)

	if (gInfo->shared_info->device_type.Generation() >= 11) {
		// Gen 11+: PLL configuration is more complex
		// TODO: Implement proper link rate detection from DPLL configuration
		ERROR("%s: DDI PLL selection not fully implemented for Gen11+, "
			"assuming default DP-link reference\n", __func__);
	} else if (gInfo->shared_info->device_type.Generation() >= 9) {
		// Gen 9/9.5: Read link rate from DPLL_CTRL1
		// PRM Vol 2c: DPLL_CTRL1 register
		if (pllSel != 0xff) {
			linkBandwidth = (read32(SKL_DPLL_CTRL1) >> (1 + 6 * pllSel)) & SKL_DPLL_DP_LINKRATE_MASK;
			switch (linkBandwidth) {
				case SKL_DPLL_CTRL1_2700:
					linkBandwidth = 2700000 / 5;
					break;
				case SKL_DPLL_CTRL1_1350:
					linkBandwidth = 1350000 / 5;
					break;
				case SKL_DPLL_CTRL1_810:
					linkBandwidth = 810000 / 5;
					break;
				case SKL_DPLL_CTRL1_1620:
					linkBandwidth = 1620000 / 5;
					break;
				case SKL_DPLL_CTRL1_1080:
					linkBandwidth = 1080000 / 5;
					break;
				case SKL_DPLL_CTRL1_2160:
					linkBandwidth = 2160000 / 5;
					break;
				default:
					linkBandwidth = 270000;
					ERROR("%s: DDI No known DP-link reference clock selected, assuming default\n", __func__);
					break;
			}
		} else {
			ERROR("%s: DDI No known PLL selected, assuming default DP-link reference\n", __func__);
		}
	}
	TRACE("%s: DDI DP-link reference clock is %gMhz\n", __func__, linkBandwidth / 1000.0f);

	// Calculate pipe offset
	// PRM Vol 2c: PIPE register offsets
	uint32 fPipeOffset = 0;
	switch (fPipe->Index()) {
		case INTEL_PIPE_B:
			fPipeOffset = 0x1000;
			break;
		case INTEL_PIPE_C:
			fPipeOffset = 0x2000;
			break;
		case INTEL_PIPE_D:
			fPipeOffset = 0xf000;
			break;
		default:
			break;
	}

	TRACE("%s: DDI M1 data before: 0x%" B_PRIx32 "\n", __func__, read32(INTEL_DDI_PIPE_A_DATA_M + fPipeOffset));
	TRACE("%s: DDI N1 data before: 0x%" B_PRIx32 "\n", __func__, read32(INTEL_DDI_PIPE_A_DATA_N + fPipeOffset));
	TRACE("%s: DDI M1 link before: 0x%" B_PRIx32 "\n", __func__, read32(INTEL_DDI_PIPE_A_LINK_M + fPipeOffset));
	TRACE("%s: DDI N1 link before: 0x%" B_PRIx32 "\n", __func__, read32(INTEL_DDI_PIPE_A_LINK_N + fPipeOffset));

	// Read current color depth from PIPE_DDI_FUNC_CTL
	// PRM Vol 2c: PIPE_DDI_FUNC_CTL BPC field
	uint32 pipeFunc = read32(PIPE_DDI_FUNC_CTL_A + fPipeOffset);
	uint32 bitsPerPixel = (pipeFunc & PIPE_DDI_BPC_MASK) >> PIPE_DDI_COLOR_SHIFT;
	switch (bitsPerPixel) {
		case PIPE_DDI_8BPC:
			bitsPerPixel = 24;
			break;
		case PIPE_DDI_10BPC:
			bitsPerPixel = 30;
			break;
		case PIPE_DDI_6BPC:
			bitsPerPixel = 18;
			break;
		case PIPE_DDI_12BPC:
			bitsPerPixel = 36;
			break;
		default:
			ERROR("%s: DDI illegal link colordepth set.\n", __func__);
			return B_ERROR;
	}
	TRACE("%s: DDI Link Colordepth: %" B_PRIu32 "\n", __func__, bitsPerPixel);

	// Read current lane count
	// Only DP modes support less than 4 lanes
	uint32 lanes = 4;
	if (((pipeFunc & PIPE_DDI_MODESEL_MASK) >> PIPE_DDI_MODESEL_SHIFT) >= PIPE_DDI_MODE_DP_SST) {
		lanes = ((pipeFunc & PIPE_DDI_DP_WIDTH_MASK) >> PIPE_DDI_DP_WIDTH_SHIFT) + 1;
		TRACE("%s: DDI in DP mode with %" B_PRIx32 " lane(s) in use\n", __func__, lanes);
	} else {
		TRACE("%s: DDI in non-DP mode with %" B_PRIx32 " lane(s) in use\n", __func__, lanes);
	}

	// Setup Data M/N
	// PRM Vol 2c: Data M/N calculation for DP
	uint64 linkspeed = lanes * linkBandwidth * 8;
	uint64 ret_n = 1;
	while (ret_n < linkspeed)
		ret_n *= 2;

	if (ret_n > 0x800000)
		ret_n = 0x800000;

	uint64 ret_m = timing.pixel_clock * ret_n * bitsPerPixel / linkspeed;
	while ((ret_n > 0xffffff) || (ret_m > 0xffffff)) {
		ret_m >>= 1;
		ret_n >>= 1;
	}

	// Set TU size bits (to default, max) before link training
	write32(INTEL_DDI_PIPE_A_DATA_M + fPipeOffset, ret_m | FDI_PIPE_MN_TU_SIZE_MASK);
	write32(INTEL_DDI_PIPE_A_DATA_N + fPipeOffset, ret_n);

	// Setup Link M/N
	linkspeed = linkBandwidth;
	ret_n = 1;
	while (ret_n < linkspeed)
		ret_n *= 2;

	if (ret_n > 0x800000)
		ret_n = 0x800000;

	ret_m = timing.pixel_clock * ret_n / linkspeed;
	while ((ret_n > 0xffffff) || (ret_m > 0xffffff)) {
		ret_m >>= 1;
		ret_n >>= 1;
	}

	write32(INTEL_DDI_PIPE_A_LINK_M + fPipeOffset, ret_m);
	// Writing Link N triggers all four registers to be activated (on next VBlank)
	write32(INTEL_DDI_PIPE_A_LINK_N + fPipeOffset, ret_n);

	TRACE("%s: DDI M1 data after: 0x%" B_PRIx32 "\n", __func__, read32(INTEL_DDI_PIPE_A_DATA_M + fPipeOffset));
	TRACE("%s: DDI N1 data after: 0x%" B_PRIx32 "\n", __func__, read32(INTEL_DDI_PIPE_A_DATA_N + fPipeOffset));
	TRACE("%s: DDI M1 link after: 0x%" B_PRIx32 "\n", __func__, read32(INTEL_DDI_PIPE_A_LINK_M + fPipeOffset));
	TRACE("%s: DDI N1 link after: 0x%" B_PRIx32 "\n", __func__, read32(INTEL_DDI_PIPE_A_LINK_N + fPipeOffset));

	return B_OK;
}


status_t
DigitalDisplayInterface::SetDisplayMode(display_mode* target, uint32 colorMode)
{
	CALLED();
	TRACE("%s: %s %dx%d\n", __func__, PortName(), target->timing.h_display,
		target->timing.v_display);

	if (fPipe == NULL) {
		ERROR("%s: Setting display mode without assigned pipe!\n", __func__);
		return B_ERROR;
	}

	display_timing hardwareTarget = target->timing;
	bool needsScaling = false;

	// Internal panel handling (eDP on port A)
	if ((PortIndex() == INTEL_PORT_A)
		&& (gInfo->shared_info->device_type.IsMobile() || _IsEDPPort())) {
		// For internal panels, we may need to use native timing and scale

		if (gInfo->shared_info->got_vbt || HasEDID()) {
			// Set vbios hardware panel mode as base
			hardwareTarget = gInfo->shared_info->panel_timing;

			if (HasEDID()) {
				// Use first detailed timing from EDID
				int i;
				for (i = 0; i < EDID1_NUM_DETAILED_MONITOR_DESC; ++i) {
					edid1_detailed_monitor *monitor = &fEDIDInfo.detailed_monitor[i];
					if (monitor->monitor_desc_type == EDID1_IS_DETAILED_TIMING)
						break;
				}

				if (i < EDID1_NUM_DETAILED_MONITOR_DESC) {
					TRACE("%s: Using EDID detailed timing %d for the internal panel\n",
						__func__, i);
					const edid1_detailed_timing& timing
						= fEDIDInfo.detailed_monitor[i].data.detailed_timing;
					hardwareTarget.pixel_clock = timing.pixel_clock * 10;
					hardwareTarget.h_display = timing.h_active;
					hardwareTarget.h_sync_start = timing.h_active + timing.h_sync_off;
					hardwareTarget.h_sync_end = hardwareTarget.h_sync_start + timing.h_sync_width;
					hardwareTarget.h_total = timing.h_active + timing.h_blank;
					hardwareTarget.v_display = timing.v_active;
					hardwareTarget.v_sync_start = timing.v_active + timing.v_sync_off;
					hardwareTarget.v_sync_end = hardwareTarget.v_sync_start + timing.v_sync_width;
					hardwareTarget.v_total = timing.v_active + timing.v_blank;
					hardwareTarget.flags = 0;
					if (timing.sync == 3) {
						if (timing.misc & 1)
							hardwareTarget.flags |= B_POSITIVE_HSYNC;
						if (timing.misc & 2)
							hardwareTarget.flags |= B_POSITIVE_VSYNC;
					}
					if (timing.interlaced)
						hardwareTarget.flags |= B_TIMING_INTERLACED;
				}
			}

			if (hardwareTarget.h_display == target->timing.h_display
					&& hardwareTarget.v_display == target->timing.v_display) {
				// Native resolution requested
				hardwareTarget = target->timing;
				TRACE("%s: Setting internal panel to native resolution at %" B_PRIu32 "Hz\n", __func__,
					hardwareTarget.pixel_clock * 1000 / (hardwareTarget.h_total * hardwareTarget.v_total));
			} else {
				// Need panel fitter scaling
				TRACE("%s: Hardware mode will actually be %dx%d at %" B_PRIu32 "Hz\n", __func__,
					hardwareTarget.h_display, hardwareTarget.v_display,
					hardwareTarget.pixel_clock * 1000 / (hardwareTarget.h_total * hardwareTarget.v_total));
				needsScaling = true;
			}
		} else {
			TRACE("%s: Setting internal panel mode without VBT info, scaling may not work\n",
				__func__);
			hardwareTarget = target->timing;
		}
	}

	// Setup PanelFitter
	PanelFitter* fitter = fPipe->PFT();
	if (fitter != NULL)
		fitter->Enable(hardwareTarget);

	// Program general pipe config
	fPipe->Configure(target);

	// PLL programming
	uint32 pllSel = 0xff; // no PLL selected
	if (gInfo->shared_info->device_type.Generation() <= 11) {
		// Gen 9-11: WRPLL calculation
		skl_wrpll_params wrpll_params;
		skl_ddi_calculate_wrpll(
			hardwareTarget.pixel_clock * 1000 /* in Hz */,
			gInfo->shared_info->pll_info.reference_frequency,
			&wrpll_params);
		fPipe->ConfigureClocksSKL(wrpll_params,
			hardwareTarget.pixel_clock,
			PortIndex(),
			&pllSel);
	} else {
		// Gen 12+ (Tiger Lake): New PLL architecture
		// PRM Vol 12: HDMI/DP Combo PHY Programming
		int p, q, k;
		float dco;
		uint32 mode = fPipe->TranscoderMode();

		if ((mode == PIPE_DDI_MODE_DVI || mode == PIPE_DDI_MODE_HDMI)
			&& ComputeHdmiDpll(hardwareTarget.pixel_clock, &p, &q, &k, &dco)) {
			TRACE("PLL settings: DCO=%f, P,Q,K=%d,%d,%d\n", dco, p, q, k);
		} else if ((mode == PIPE_DDI_MODE_DP_SST || mode == PIPE_DDI_MODE_DP_MST)
			&& ComputeDisplayPortDpll(hardwareTarget.pixel_clock, &p, &q, &k, &dco)) {
			TRACE("PLL settings: DCO=%f, P,Q,K=%d,%d,%d\n", dco, p, q, k);
		} else {
			ERROR("%s: Could not find a matching PLL setting\n", __func__);
			return B_ERROR;
		}

		// TODO: Implement proper PLL assignment
		int chosenPLL = 0;
		if (PortIndex() == 7)
			chosenPLL = 1;
		TRACE("Using DPLL %d for port %d. PLL settings: DCO=%f, P,Q,K=%d,%d,%d\n", chosenPLL,
			PortIndex(), dco, p, q, k);
		ProgramPLL(chosenPLL, p, q, k, dco);

		// Configure DPLL mapping to port and enable clock
		// PRM Vol 2c: TGL_DPCLKA_CFGCR0
		uint32 config = read32(TGL_DPCLKA_CFGCR0);
		TRACE("PLL configuration before changes: %" B_PRIx32 "\n", config);

		if (chosenPLL == 0) {
			config |= TGL_DPCLKA_DDIA_CLOCK_OFF;
			config &= TGL_DPCLKA_DDIA_CLOCK_SELECT;
			write32(TGL_DPCLKA_CFGCR0, config);
			config &= ~TGL_DPCLKA_DDIA_CLOCK_OFF;
			write32(TGL_DPCLKA_CFGCR0, config);
		} else {
			config |= TGL_DPCLKA_DDIB_CLOCK_OFF;
			config &= TGL_DPCLKA_DDIB_CLOCK_SELECT;
			config |= 1 << TGL_DPCLKA_DDIB_CLOCK_SELECT_SHIFT;
			write32(TGL_DPCLKA_CFGCR0, config);
			config &= ~TGL_DPCLKA_DDIB_CLOCK_OFF;
			write32(TGL_DPCLKA_CFGCR0, config);
		}
		TRACE("PLL configuration after changes: %" B_PRIx32 "\n", config);
	}

	// Program target display mode
	fPipe->ConfigureTimings(target, !needsScaling);
	_SetPortLinkGen8(hardwareTarget, pllSel);

	// Set fCurrentMode to our set display mode
	memcpy(&fCurrentMode, target, sizeof(display_mode));

	return B_OK;
}
