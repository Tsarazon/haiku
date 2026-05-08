/*
 * Copyright 2018-2021 Haiku, Inc. All rights reserved.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		B Krishnan Iyer, krishnaniyer97@gmail.com
 *		Adrien Destugues, pulkomandy@pulkomandy.tk
 */
#include "mmc_bus.h"


dk_keeper_info* gDeviceKeeper = NULL;


// Forward decl: per-device interface ops table is defined below.
extern mmc_device_interface gMmcBusOps;


static status_t
mmc_bus_init(dk_node* node, void** _cookie)
{
	CALLED();
	MMCBus* bus = new(std::nothrow) MMCBus(node);
	if (bus == NULL) {
		ERROR("Unable to allocate MMC bus\n");
		return B_NO_MEMORY;
	}

	status_t result = bus->InitCheck();
	if (result != B_OK) {
		TRACE("failed to set up mmc bus device object\n");
		delete bus;
		return result;
	}
	TRACE("MMC bus object created\n");

	// Publish the MMC device interface so peripheral drivers (mmc_disk,
	// sdio, ...) can retrieve it via get_interface walk-up from each
	// detected card node.
	result = gDeviceKeeper->publish_interface(node, MMC_DEVICE_INTERFACE_NAME,
		&gMmcBusOps);
	if (result != B_OK) {
		delete bus;
		return result;
	}

	*_cookie = bus;
	return B_OK;
}


static void
mmc_bus_uninit(void* cookie)
{
	CALLED();
	MMCBus* bus = (MMCBus*)cookie;
	delete bus;
}


static status_t
mmc_bus_rescan(void* cookie)
{
	MMCBus* bus = (MMCBus*)cookie;
	bus->Rescan();
	return B_OK;
}


static status_t
mmc_bus_execute_command(dk_node* node, void* cookie, uint16_t rca,
	uint8_t command, uint32_t argument, uint32_t* result)
{
	TRACE("In mmc_bus_execute_command\n");

	MMCBus* bus = (MMCBus*)cookie;

	bus->AcquireBus();
	status_t error = bus->ExecuteCommand(rca, command, argument, result);
	bus->ReleaseBus();

	return error;
}


static status_t
mmc_bus_do_io(dk_node* node, void* cookie, uint16_t rca, uint8_t command,
	IOOperation* operation, bool offsetAsSectors)
{
	MMCBus* bus = (MMCBus*)cookie;
	status_t result = B_OK;

	bus->AcquireBus();
	result = bus->DoIO(rca, command, operation, offsetAsSectors);
	bus->ReleaseBus();

	return result;
}


static void
mmc_bus_set_width(dk_node* node, void* cookie, int width)
{
	MMCBus* bus = (MMCBus*)cookie;

	bus->AcquireBus();
	bus->SetBusWidth(width);
	bus->ReleaseBus();
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			return B_ERROR;
	}
}


// Bus protocol ops exposed to child drivers (mmc_disk, sdio, etc.)
// via get_interface(MMC_DEVICE_INTERFACE_NAME).
mmc_device_interface gMmcBusOps = {
	mmc_bus_execute_command,
	mmc_bus_do_io,
	mmc_bus_set_width
};


// Single unified driver for the MMC bus manager.
// Attached to a child node explicitly registered by sdhci.
dk_driver_info gMmcBusDriver = {
	.info            = { MMC_BUS_MODULE_NAME, 0, std_ops },
	.attach          = mmc_bus_init,
	.detach          = mmc_bus_uninit,
	.rescan_children = mmc_bus_rescan,
	.node_flags      = KOSM_FIND_MULTIPLE_CHILDREN,
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{}
};


module_info* modules[] = {
	(module_info*)&gMmcBusDriver,
	NULL
};
