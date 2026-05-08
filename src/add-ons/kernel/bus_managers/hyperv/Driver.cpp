/*
 * Copyright 2026 John Davis. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include "Driver.h"

dk_keeper_info* gDeviceKeeper;
acpi_module_info* gACPI;
dpc_module_info* gDPC;


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ B_ACPI_MODULE_NAME, (module_info**)&gACPI },
	{ B_DPC_MODULE_NAME, (module_info **)&gDPC },
	{}
};


extern dk_driver_info gVMBusDriver;
extern dk_driver_info gVMBusDeviceDriver;


module_info* modules[] = {
	(module_info*)&gVMBusDriver,
	(module_info*)&gVMBusDeviceDriver,
	NULL
};
