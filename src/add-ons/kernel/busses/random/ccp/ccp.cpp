/*
 * Copyright 2022, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <dpc.h>
#include <module.h>

#include "random.h"

#include "ccp.h"


#define CCP_REG_TRNG	0xc


dk_keeper_info* gDeviceKeeper;
static random_for_controller_interface* sRandom;
static dpc_module_info* sDPC;


static void
handleDPC(void* arg)
{
	CALLED();
	ccp_device_info* bus = reinterpret_cast<ccp_device_info*>(arg);

	uint32 lowValue = read32(bus->registers + CCP_REG_TRNG);
	uint32 highValue = read32(bus->registers + CCP_REG_TRNG);
	if (lowValue == 0 || highValue == 0)
		return;
	sRandom->queue_randomness((uint64)lowValue | ((uint64)highValue << 32));
}


static int32
handleTimerHook(struct timer* timer)
{
	ccp_device_info* bus =
		reinterpret_cast<ccp_device_info*>(timer->user_data);

	sDPC->queue_dpc(bus->dpcHandle, handleDPC, bus);
	return B_HANDLED_INTERRUPT;
}


//	#pragma mark - driver hooks


static status_t
ccp_device_attach(dk_node* node, void** _cookie)
{
	CALLED();

	// Retrieve the parent's driver cookie — this is the
	// ccp_device_info allocated by the ACPI or PCI attach.
	dk_driver_info* parentDriver;
	ccp_device_info* bus;
	dk_node* parent = gDeviceKeeper->get_parent_node(node);
	if (parent == NULL)
		return B_NO_INIT;

	status_t status = gDeviceKeeper->get_node_driver(parent,
		&parentDriver, (void**)&bus);
	gDeviceKeeper->put_node(parent);
	if (status != B_OK)
		return status;

	TRACE_ALWAYS("ccp_device_attach() addr 0x%" B_PRIxPHYSADDR
		" size 0x%" B_PRIx64 "\n",
		bus->base_addr, bus->map_size);

	// load required modules
	if (sRandom == NULL) {
		status = get_module(RANDOM_FOR_CONTROLLER_MODULE_NAME,
			(module_info**)&sRandom);
		if (status != B_OK) {
			ERROR("failed to load random module: %s\n",
				strerror(status));
			return status;
		}
	}
	if (sDPC == NULL) {
		status = get_module(B_DPC_MODULE_NAME,
			(module_info**)&sDPC);
		if (status != B_OK) {
			ERROR("failed to load DPC module: %s\n",
				strerror(status));
			return status;
		}
	}

	// map MMIO registers
	bus->registersArea = map_physical_memory("CCP memory mapped registers",
		bus->base_addr, bus->map_size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&bus->registers);
	if (bus->registersArea < 0)
		return bus->registersArea;

	// set up periodic DPC for TRNG extraction
	status = sDPC->new_dpc_queue(&bus->dpcHandle, "ccp timer",
		B_LOW_PRIORITY);
	if (status != B_OK) {
		ERROR("DPC setup failed (%s)\n", strerror(status));
		delete_area(bus->registersArea);
		bus->registersArea = -1;
		return status;
	}

	bus->extractTimer.user_data = bus;
	status = add_timer(&bus->extractTimer, &handleTimerHook,
		1 * 1000 * 1000, B_PERIODIC_TIMER);
	if (status != B_OK) {
		ERROR("timer setup failed (%s)\n", strerror(status));
		sDPC->delete_dpc_queue(&bus->dpcHandle);
		delete_area(bus->registersArea);
		bus->registersArea = -1;
		return status;
	}

	// trigger an immediate extraction
	sDPC->queue_dpc(bus->dpcHandle, handleDPC, bus);

	*_cookie = bus;
	return B_OK;
}


static void
ccp_device_detach(void* cookie)
{
	ccp_device_info* bus = (ccp_device_info*)cookie;

	cancel_timer(&bus->extractTimer);

	if (sDPC != NULL)
		sDPC->delete_dpc_queue(&bus->dpcHandle);

	if (bus->registersArea >= 0)
		delete_area(bus->registersArea);
}


//	#pragma mark - module std ops


static status_t
ccp_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return get_module(KOSM_DEVICE_KEEPER_MODULE_NAME,
				(module_info**)&gDeviceKeeper);
		case B_MODULE_UNINIT:
			if (sRandom != NULL) {
				put_module(RANDOM_FOR_CONTROLLER_MODULE_NAME);
				sRandom = NULL;
			}
			if (sDPC != NULL) {
				put_module(B_DPC_MODULE_NAME);
				sDPC = NULL;
			}
			put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
			return B_OK;
		default:
			return B_ERROR;
	}
}


//	#pragma mark - module exports


// CCP device module: auto-attached as a fixed child of the
// ACPI or PCI driver node. No match dict needed — attachment
// is by module name (dk_driver_v1 suffix triggers auto-attach
// in _PostRegisterProbe).
static dk_driver_info sCcpDeviceModule = {
	.info = {
		CCP_DEVICE_MODULE_NAME,
		0,
		ccp_std_ops
	},
	.attach             = ccp_device_attach,
	.detach             = ccp_device_detach,
};


module_info* modules[] = {
	(module_info*)&gCcpAcpiDevice,
	(module_info*)&gCcpPciDevice,
	(module_info*)&sCcpDeviceModule,
	NULL
};
