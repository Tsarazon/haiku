/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include "I2CPrivate.h"


I2CDevice::I2CDevice(dk_node* node, I2CBus* bus, i2c_addr slaveAddress)
	:
	fNode(node),
	fBus(bus),
	fSlaveAddress(slaveAddress)
{
	CALLED();
}


I2CDevice::~I2CDevice()
{
}


status_t
I2CDevice::InitCheck()
{
	return B_OK;
}


status_t
I2CDevice::ExecCommand(i2c_op op, const void* cmdBuffer,
	size_t cmdLength, void* dataBuffer, size_t dataLength)
{
	CALLED();
	return fBus->ExecCommand(op, fSlaveAddress, cmdBuffer, cmdLength,
		dataBuffer, dataLength);
}


status_t
I2CDevice::AcquireBus()
{
	CALLED();
	return fBus->AcquireBus();
}


void
I2CDevice::ReleaseBus()
{
	CALLED();
	fBus->ReleaseBus();
}


// i2c_device_interface implementation: these are what peripheral drivers
// call via get_interface(I2C_DEVICE_INTERFACE_NAME, ANCESTORS). The
// cookie returned to the caller is the I2CDevice* this driver stashes
// as its driver cookie in attach.

static status_t
i2c_device_exec_command(i2c_device _cookie, i2c_op op,
	const void* cmdBuffer, size_t cmdLength,
	void* dataBuffer, size_t dataLength)
{
	I2CDevice* device = (I2CDevice*)_cookie;
	return device->ExecCommand(op, cmdBuffer, cmdLength,
		dataBuffer, dataLength);
}


static status_t
i2c_device_acquire_bus(i2c_device _cookie)
{
	I2CDevice* device = (I2CDevice*)_cookie;
	return device->AcquireBus();
}


static void
i2c_device_release_bus(i2c_device _cookie)
{
	I2CDevice* device = (I2CDevice*)_cookie;
	device->ReleaseBus();
}


static i2c_device_interface sI2CDeviceOps = {
	.exec_command	= i2c_device_exec_command,
	.acquire_bus	= i2c_device_acquire_bus,
	.release_bus	= i2c_device_release_bus,
};


// dk_driver_info callbacks

static status_t
i2c_device_attach(dk_node* node, void** _cookie)
{
	CALLED();

	// Get parent I2CBus driver cookie. I2C device nodes are created as
	// direct children of the I2C bus node by the controller's scan_bus,
	// so parent's attached driver is always gI2CBusDriver with an
	// I2CBus* as its cookie.
	I2CBus* bus;
	{
		dk_node* parent = gDeviceKeeper->get_parent_node(node);
		if (parent == NULL)
			return B_NO_INIT;

		status_t status = gDeviceKeeper->get_node_driver(parent,
			NULL, (void**)&bus);
		gDeviceKeeper->put_node(parent);
		if (status != B_OK)
			return status;
	}

	// read slave address from node properties
	uint16 slaveAddress;
	if (gDeviceKeeper->get_property_uint16(node, KOSM_I2C_ADDRESS,
		&slaveAddress, false) != B_OK) {
		ERROR("I2CDevice: missing %s property\n", KOSM_I2C_ADDRESS);
		return B_BAD_VALUE;
	}

	I2CDevice* device = new(std::nothrow) I2CDevice(node, bus,
		slaveAddress);
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = device->InitCheck();
	if (status != B_OK) {
		ERROR("I2CDevice: init failed\n");
		delete device;
		return status;
	}

	// Publish the typed i2c_device_interface on this node so peripheral
	// drivers (i2c_hid, i2c_elan, …) can retrieve it from their attach
	// via get_interface(I2C_DEVICE_INTERFACE_NAME, ANCESTORS). The
	// cookie passed to their exec_command callbacks is our driver
	// cookie (the I2CDevice* below).
	status = gDeviceKeeper->publish_interface(node,
		I2C_DEVICE_INTERFACE_NAME, &sI2CDeviceOps);
	if (status != B_OK) {
		ERROR("I2CDevice: publish_interface failed: %s\n",
			strerror(status));
		delete device;
		return status;
	}

	*_cookie = device;
	return B_OK;
}


static void
i2c_device_detach(void* _cookie)
{
	CALLED();
	I2CDevice* device = (I2CDevice*)_cookie;
	delete device;
}


static status_t
i2c_device_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
		default:
			return B_ERROR;
	}
}


dk_driver_info gI2CDeviceDriver = {
	.info = {
		I2C_DEVICE_MODULE_NAME,
		0,
		i2c_device_std_ops
	},
	// No .match: auto-attached by module name when the I2C bus manager
	// registers a child node with I2C_DEVICE_MODULE_NAME.
	.attach	= i2c_device_attach,
	.detach	= i2c_device_detach,
	// No .ops: i2c peripherals are exposed to userspace via the
	// peripheral driver (e.g. i2c_hid), not directly here.
};
