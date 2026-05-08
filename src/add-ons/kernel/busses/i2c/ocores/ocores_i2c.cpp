/*
 * Copyright 2022, Haiku, Inc.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include "ocores_i2c.h"
#include <bus/FDT.h>

#include <AutoDeleterDrivers.h>

#include <string.h>
#include <new>


status_t
OcoresI2c::WaitCompletion()
{
	// Poll the interrupt-status bit with a hard deadline. Without a
	// timeout, broken or disconnected hardware hangs the thread forever.
	// 100ms is ample: Opencores I2C at 100 kHz standard mode completes
	// even a full 32-byte transfer in under 3ms; fast-mode plus is an
	// order of magnitude faster.
	const bigtime_t deadline = system_time() + 100000LL;
	while (!fRegs->status.interrupt) {
		if (system_time() >= deadline)
			return B_TIMED_OUT;
	}
	return B_OK;
}


status_t
OcoresI2c::WriteByte(OcoresI2cRegsCommand cmd, uint8 val)
{
	cmd.intAck = true;
	cmd.write = true;
	fRegs->data = val;
	fRegs->command.val = cmd.val;
	CHECK_RET(WaitCompletion());
	return B_OK;
}


status_t
OcoresI2c::ReadByte(OcoresI2cRegsCommand cmd, uint8& val)
{
	cmd.intAck = true;
	cmd.read = true;
	cmd.nack = cmd.stop;
	fRegs->command.val = cmd.val;
	CHECK_RET(WaitCompletion());
	val = fRegs->data;
	return B_OK;
}


status_t
OcoresI2c::WriteAddress(i2c_addr adr, bool isRead)
{
	// TODO: 10 bit address support
	uint8 val = OcoresI2cRegsAddress7{.read = isRead, .address = (uint8)adr}.val;
	CHECK_RET(WriteByte({.start = true}, val));
	return B_OK;
}


//#pragma mark - driver

float
OcoresI2c::SupportsDevice(dk_node* parent)
{
	char bus[64];
	status_t status = gDeviceKeeper->get_property_string(parent,
		KOSM_DEVICE_BUS, bus, sizeof(bus), NULL, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(bus, "fdt") != 0)
		return 0.0f;

	char compatible[128];
	status = gDeviceKeeper->get_property_string(parent, "fdt/compatible",
		compatible, sizeof(compatible), NULL, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(compatible, "opencores,i2c-ocores") != 0
		&& strcmp(compatible, "sifive,fu740-c000-i2c") != 0
		&& strcmp(compatible, "sifive,i2c0") != 0) {
		return 0.0f;
	}

	return 1.0f;
}


status_t
OcoresI2c::InitDriver(dk_node* node, OcoresI2c*& outDriver)
{
	ObjectDeleter<OcoresI2c> driver(new(std::nothrow) OcoresI2c());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitDriverInt(node));
	outDriver = driver.Detach();
	return B_OK;
}


status_t
OcoresI2c::InitDriverInt(dk_node* node)
{
	fNode = node;
	dprintf("+OcoresI2c::InitDriver()\n");

	char bus[64];
	CHECK_RET(gDeviceKeeper->get_property_string(node,
		KOSM_DEVICE_BUS, bus, sizeof(bus), NULL, true));
	if (strcmp(bus, "fdt") != 0)
		return B_ERROR;

	// Retrieve the parent FDT device interface via walk-up. The FDT bus
	// manager publishes fdt_device_module_info on each FDT device node.
	fdt_device_module_info* parentModule;
	fdt_device* parentDev;
	CHECK_RET(gDeviceKeeper->get_interface(node,
		FDT_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&parentModule, (void**)&parentDev));

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!parentModule->get_reg(parentDev, 0, &regs, &regsLen))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("Ocores i2c MMIO", regs, regsLen,
		B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	// Publish the i2c_sim_interface on our node so that the I2C bus
	// manager, which auto-attaches to any child node registered with
	// I2C_BUS_MODULE_NAME, can walk up and find us.
	extern i2c_sim_interface gOcoresI2cSimInterface;
	CHECK_RET(gDeviceKeeper->publish_interface(node,
		I2C_SIM_INTERFACE_NAME, &gOcoresI2cSimInterface));

	// Register the I2C bus child so the bus manager is auto-attached.
	dk_property busAttrs[] = {
		DK_PROP_STRING(KOSM_LABEL, "Opencores I2C Bus"),
		DK_PROP_UINT32(KOSM_DEVICE_FLAGS, KOSM_FIND_MULTIPLE_CHILDREN),
		DK_PROP_END
	};
	CHECK_RET(gDeviceKeeper->register_node(node,
		I2C_BUS_MODULE_NAME, busAttrs, NULL, NULL));

	dprintf("-OcoresI2c::InitDriver()\n");
	return B_OK;
}


void
OcoresI2c::UninitDriver()
{
	delete this;
}


//#pragma mark - i2c controller

status_t
OcoresI2c::ExecCommand(i2c_op op,
	i2c_addr slaveAddress, const uint8 *cmdBuffer, size_t cmdLength,
	uint8* dataBuffer, size_t dataLength)
{
	(void)op;
	if (cmdLength > 0) {
		CHECK_RET(WriteAddress(slaveAddress, false));
		do {
			if (fRegs->status.nackReceived) {
				fRegs->command.val = OcoresI2cRegsCommand{
					.intAck = true,
					.stop = true
				}.val;
				return B_ERROR;
			}
			cmdLength--;
			CHECK_RET(WriteByte({.stop = cmdLength == 0 && dataLength == 0},
				*cmdBuffer++));
		} while (cmdLength > 0);
	}
	if (dataLength > 0) {
		CHECK_RET(WriteAddress(slaveAddress, true));
		do {
			dataLength--;
			CHECK_RET(ReadByte({.stop = dataLength == 0}, *dataBuffer++));
		} while (dataLength > 0);
	}
	return B_OK;
}


status_t
OcoresI2c::AcquireBus()
{
	return mutex_lock(&fLock);
}


void
OcoresI2c::ReleaseBus()
{
	mutex_unlock(&fLock);
}
