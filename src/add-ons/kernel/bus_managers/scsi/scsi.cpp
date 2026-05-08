/*
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "scsi_internal.h"

dk_keeper_info *pnp;

module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&pnp },
	{}
};

module_info *modules[] = {
	(module_info *)&scsi_for_sim_module,
	(module_info *)&gSCSIBusDriver,
	(module_info *)&gSCSIDeviceDriver,
	NULL
};
