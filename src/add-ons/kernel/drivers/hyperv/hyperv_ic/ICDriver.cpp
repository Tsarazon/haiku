/*
 * Copyright 2026 John Davis. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ICDriver.h"


dk_keeper_info* gDeviceKeeper;


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{}
};


module_info* modules[] = {
	(module_info*)&gHyperVHeartbeatDriverModule,
	(module_info*)&gHyperVTimeSyncDriverModule,
	NULL
};
