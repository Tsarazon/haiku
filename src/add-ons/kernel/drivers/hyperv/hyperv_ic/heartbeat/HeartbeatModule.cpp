/*
 * Copyright 2026 John Davis. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "Heartbeat.h"


static float
hyperv_heartbeat_supports_device(dk_node* parent)
{
	CALLED();

	char bus[64];
	if (gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1;

	if (strcmp(bus, HYPERV_BUS_NAME) != 0)
		return 0.0f;

	char type[128];
	if (gDeviceKeeper->get_property_string(parent, HYPERV_DEVICE_TYPE_STRING_ITEM,
			type, sizeof(type), NULL, false) != B_OK)
		return 0.0f;

	if (strcmp(type, VMBUS_TYPE_HEARTBEAT) != 0)
		return 0.0f;

	TRACE("Hyper-V Heartbeat device found!\n");
	return 0.8f;
}


static status_t
hyperv_heartbeat_init_driver(dk_node* node, void** _driverCookie)
{
	CALLED();

	Heartbeat* heartbeat = new(std::nothrow) Heartbeat(node);
	if (heartbeat == NULL) {
		ERROR("Unable to allocate Hyper-V Heartbeat object\n");
		return B_NO_MEMORY;
	}

	status_t status = heartbeat->InitCheck();
	if (status != B_OK) {
		ERROR("Failed to set up Hyper-V Heartbeat object\n");
		delete heartbeat;
		return status;
	}
	TRACE("Hyper-V Heartbeat object created\n");

	*_driverCookie = heartbeat;
	return B_OK;
}


static void
hyperv_heartbeat_uninit_driver(void* driverCookie)
{
	CALLED();
	Heartbeat* heartbeat = reinterpret_cast<Heartbeat*>(driverCookie);
	delete heartbeat;
}


dk_driver_info gHyperVHeartbeatDriverModule = {
	{
		HYPERV_HEARTBEAT_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	.probe = hyperv_heartbeat_supports_device,
	.attach = hyperv_heartbeat_init_driver,
	.detach = hyperv_heartbeat_uninit_driver,
};
