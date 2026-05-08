/*
 * Copyright 2026 John Davis. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "TimeSync.h"


static float
hyperv_timesync_supports_device(dk_node* parent)
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

	if (strcmp(type, VMBUS_TYPE_TIMESYNC) != 0)
		return 0.0f;

	TRACE("Hyper-V Time Sync device found!\n");
	return 0.8f;
}


static status_t
hyperv_timesync_attach(dk_node* node, void** _driverCookie)
{
	CALLED();

	TimeSync* timeSync = new(std::nothrow) TimeSync(node);
	if (timeSync == NULL) {
		ERROR("Unable to allocate Hyper-V Time Sync object\n");
		return B_NO_MEMORY;
	}

	status_t status = timeSync->InitCheck();
	if (status != B_OK) {
		ERROR("Failed to set up Hyper-V Time Sync object\n");
		delete timeSync;
		return status;
	}
	TRACE("Hyper-V Time Sync object created\n");

	*_driverCookie = timeSync;
	return B_OK;
}


static void
hyperv_timesync_detach(void* driverCookie)
{
	CALLED();
	TimeSync* timeSync = reinterpret_cast<TimeSync*>(driverCookie);
	delete timeSync;
}


dk_driver_info gHyperVTimeSyncDriverModule = {
	{
		HYPERV_TIMESYNC_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	.probe = hyperv_timesync_supports_device,
	.attach = hyperv_timesync_attach,
	.detach = hyperv_timesync_detach,
};
