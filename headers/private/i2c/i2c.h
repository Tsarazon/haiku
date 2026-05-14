/*
 * Copyright 2020, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _I2C_H_
#define _I2C_H_


#include <ACPI.h>
#include <device_keeper.h>
#include <KernelExport.h>


typedef uint16 i2c_addr;
typedef enum {
	I2C_OP_READ = 0,
	I2C_OP_READ_STOP = 1,
	I2C_OP_WRITE = 2,
	I2C_OP_WRITE_STOP = 3,
	I2C_OP_READ_BLOCK = 5,
	I2C_OP_WRITE_BLOCK = 7,
} i2c_op;


#define IS_READ_OP(op)	(((op) & I2C_OP_WRITE) == 0)
#define IS_WRITE_OP(op)	(((op) & I2C_OP_WRITE) != 0)
#define IS_STOP_OP(op)	(((op) & 1) != 0)
#define IS_BLOCK_OP(op)	(((op) & 4) != 0)


// bus cookie, issued by i2c bus manager
typedef void* i2c_bus;
// device cookie, issued by i2c bus manager
typedef void* i2c_device;


// Device node

// Bus interface name for publish_interface/get_interface. Published by
// the I2C bus manager on each I2C slave device node; peripheral drivers
// retrieve it via gDeviceKeeper->get_interface(node, NAME, ...).
// The slave address is exposed via the standard KOSM_I2C_ADDRESS property.
#define I2C_DEVICE_INTERFACE_NAME	"interface/i2c/device/v1"

// bus manager device interface for peripheral driver
typedef struct {
	status_t (*exec_command)(i2c_device cookie, i2c_op op,
		const void *cmdBuffer, size_t cmdLength, void* dataBuffer,
		size_t dataLength);
	status_t (*acquire_bus)(i2c_device cookie);
	void (*release_bus)(i2c_device cookie);
} i2c_device_interface;


#define I2C_DEVICE_MODULE_NAME "bus_managers/i2c/device/dk_driver_v1"


// Bus node

// name of I2C bus node driver
#define I2C_BUS_MODULE_NAME "bus_managers/i2c/bus/dk_driver_v1"



// Controller Node

typedef void *i2c_bus_cookie;
typedef void *i2c_intr_cookie;


// Bus interface name for publish_interface/get_interface. I2C controller
// drivers (pch_i2c, ocores_i2c) publish this on their node; the I2C bus
// manager retrieves it via get_interface(node, I2C_SIM_INTERFACE_NAME, ...).
#define I2C_SIM_INTERFACE_NAME	"interface/i2c/sim/v1"

// Bus manager interface used by I2C controller drivers.
// Published by the controller driver on its own node via
// publish_interface(I2C_SIM_INTERFACE_NAME, &ops); the I2C bus manager
// retrieves it in attach via get_interface(node, I2C_SIM_INTERFACE_NAME,
// KOSM_INTERFACE_ANCESTORS, ...).
//
// scan_bus receives the I2C bus manager's dk_node so the controller can
// register_node() directly for each discovered slave (the controller
// walks ACPI / firmware / probe) — replacing the old register_device
// callback path.
typedef struct {
	void (*set_i2c_bus)(i2c_bus_cookie cookie, i2c_bus bus);

	status_t (*exec_command)(i2c_bus_cookie cookie, i2c_op op,
		i2c_addr slaveAddress, const void *cmdBuffer, size_t cmdLength,
		void* dataBuffer, size_t dataLength);

	status_t (*scan_bus)(i2c_bus_cookie cookie, dk_node* busNode);

	status_t (*acquire_bus)(i2c_bus_cookie cookie);
	void (*release_bus)(i2c_bus_cookie cookie);

	status_t (*install_interrupt_handler)(i2c_bus_cookie cookie,
		i2c_intr_cookie intrCookie, interrupt_handler handler, void* data);

	status_t (*uninstall_interrupt_handler)(i2c_bus_cookie cookie,
		i2c_intr_cookie intrCookie);
} i2c_sim_interface;


// Devfs entry for raw bus access.
// used by i2c diag tool

enum {
	I2CEXEC = B_DEVICE_OP_CODES_END + 1,
};


typedef struct i2c_ioctl_exec
{
	i2c_op		op;
	i2c_addr	addr;
	const void*	cmdBuffer;
	size_t		cmdLength;
	void*		buffer;
	size_t		bufferLength;
} i2c_ioctl_exec;

#define I2C_EXEC_MAX_BUFFER_LENGTH  32

#endif	/* _I2C_H_ */
