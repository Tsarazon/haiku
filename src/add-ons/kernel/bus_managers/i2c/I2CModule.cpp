/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include "I2CPrivate.h"


dk_keeper_info* gDeviceKeeper = NULL;

extern dk_driver_info gI2CBusDriver;
extern dk_driver_info gI2CDeviceDriver;


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{}
};


module_info* modules[] = {
	(module_info*)&gI2CBusDriver,
	(module_info*)&gI2CDeviceDriver,
	NULL
};
