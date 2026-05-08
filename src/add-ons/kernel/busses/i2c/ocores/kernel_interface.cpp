/*
 * Copyright 2022, Haiku, Inc.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include "ocores_i2c.h"


dk_keeper_info* gDeviceKeeper;


// Published on the controller's dk_node in OcoresI2c::InitDriverInt via
// publish_interface(I2C_SIM_INTERFACE_NAME). The i2c_bus_cookie passed
// to every callback is the OcoresI2c* driver cookie.
i2c_sim_interface gOcoresI2cSimInterface = {
	.set_i2c_bus = NULL,
	.exec_command = [](i2c_bus_cookie cookie, i2c_op op,
		i2c_addr slaveAddress, const void *cmdBuffer, size_t cmdLength,
		void* dataBuffer, size_t dataLength) {
		return static_cast<OcoresI2c*>(cookie)->ExecCommand(op, slaveAddress,
			(const uint8*)cmdBuffer, cmdLength, (uint8*)dataBuffer, dataLength);
	},
	.scan_bus = NULL,
		// ocores has no enumeration path — devices are described by FDT
		// and are registered directly by the FDT bus manager.
	.acquire_bus = [](i2c_bus_cookie cookie) {
		return static_cast<OcoresI2c*>(cookie)->AcquireBus();
	},
	.release_bus = [](i2c_bus_cookie cookie) {
		static_cast<OcoresI2c*>(cookie)->ReleaseBus();
	},
	.install_interrupt_handler = NULL,
	.uninstall_interrupt_handler = NULL,
};


static dk_driver_info sOcoresDriver = {
	.info = {
		OCORES_I2C_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.probe = [](dk_node* parent) {
		return OcoresI2c::SupportsDevice(parent);
	},
	.attach = [](dk_node* node, void** driverCookie) {
		return OcoresI2c::InitDriver(node, *(OcoresI2c**)driverCookie);
	},
	.detach = [](void* driverCookie) {
		static_cast<OcoresI2c*>(driverCookie)->UninitDriver();
	},
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};


_EXPORT module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{}
};

_EXPORT module_info *modules[] = {
	(module_info *)&sOcoresDriver,
	NULL
};
