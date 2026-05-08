/*
 * Copyright 2026 John Davis. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "VMBusPrivate.h"


static float
vmbus_supports_device(dk_node* parent)
{
	CALLED();
	char bus[64];

	if (gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK) {
		TRACE("Could not find required attribute device/bus\n");
		return -1;
	}

	if (strcmp(bus, "root") != 0)
		return 0.0f;

	status_t status = vmbus_detect_hyperv();
	if (status != B_OK)
		return 0.0f;

	return 0.8f;
}


static status_t
vmbus_init_driver(dk_node* node, void** _driverCookie)
{
	CALLED();

	VMBus* vmbus = new(std::nothrow) VMBus(node);
	if (vmbus == NULL) {
		ERROR("Unable to allocate VMBus object\n");
		return B_NO_MEMORY;
	}

	status_t status = vmbus->InitCheck();
	if (status != B_OK) {
		ERROR("Failed to set up VMBus object\n");
		delete vmbus;
		return status;
	}
	TRACE("VMBus object created\n");

	// Publish the VMBus root interface on this node so per-channel
	// VMBusDevices can retrieve it via get_interface walk-up. The cookie
	// passed back to callers is the VMBus* itself (driver cookie).
	status = gDeviceKeeper->publish_interface(node,
		HYPERV_VMBUS_INTERFACE_NAME, &gVMBusOps);
	if (status != B_OK) {
		ERROR("Failed to publish VMBus interface\n");
		delete vmbus;
		return status;
	}

	// Request channels during attach
	vmbus->RequestChannels();

	*_driverCookie = vmbus;
	return B_OK;
}


static void
vmbus_uninit_driver(void* driverCookie)
{
	CALLED();
	VMBus* vmbus = reinterpret_cast<VMBus*>(driverCookie);
	delete vmbus;
}


static uint32
vmbus_get_version(hyperv_bus cookie)
{
	CALLED();
	VMBus* vmbus = reinterpret_cast<VMBus*>(cookie);
	return vmbus->GetVersion();
}


static status_t
vmbus_open_channel(hyperv_bus cookie, uint32 channel, uint32 gpadl, uint32 rxOffset,
	hyperv_bus_callback callback, void* callbackData)
{
	CALLED();
	VMBus* vmbus = reinterpret_cast<VMBus*>(cookie);
	return vmbus->OpenChannel(channel, gpadl, rxOffset, callback, callbackData);
}


static status_t
vmbus_close_channel(hyperv_bus cookie, uint32 channel)
{
	CALLED();
	VMBus* vmbus = reinterpret_cast<VMBus*>(cookie);
	return vmbus->CloseChannel(channel);
}


static status_t
vmbus_allocate_gpadl(hyperv_bus cookie, uint32 channel, uint32 length, void** _buffer,
	uint32* _gpadl)
{
	CALLED();
	VMBus* vmbus = reinterpret_cast<VMBus*>(cookie);
	return vmbus->AllocateGPADL(channel, length, _buffer, _gpadl);
}


static status_t
vmbus_free_gpadl(hyperv_bus cookie, uint32 channel, uint32 gpadl)
{
	CALLED();
	VMBus* vmbus = reinterpret_cast<VMBus*>(cookie);
	return vmbus->FreeGPADL(channel, gpadl);
}


static status_t
vmbus_signal_channel(hyperv_bus cookie, uint32 channel)
{
	VMBus* vmbus = reinterpret_cast<VMBus*>(cookie);
	return vmbus->SignalChannel(channel);
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			break;
	}

	return B_ERROR;
}


hyperv_bus_ops gVMBusOps = {
	vmbus_get_version,
	vmbus_open_channel,
	vmbus_close_channel,
	vmbus_allocate_gpadl,
	vmbus_free_gpadl,
	vmbus_signal_channel
};


static const dk_match_rule sVMBusMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "root"),
	DK_MATCH_END
};

static const dk_match_dict sVMBusMatchDict = {
	sVMBusMatchRules,
	0
};


dk_driver_info gVMBusDriver = {
	.info       = { HYPERV_VMBUS_MODULE_NAME, 0, std_ops },
	.match      = &sVMBusMatchDict,
	.probe      = vmbus_supports_device,
	.attach     = vmbus_init_driver,
	.detach     = vmbus_uninit_driver,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};
