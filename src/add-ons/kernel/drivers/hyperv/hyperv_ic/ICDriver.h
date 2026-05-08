/*
 * Copyright 2026 John Davis. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _HYPERV_IC_DRIVER_H_
#define _HYPERV_IC_DRIVER_H_


#include <device_keeper.h>
#include <KernelExport.h>


extern dk_keeper_info* gDeviceKeeper;

extern dk_driver_info gHyperVHeartbeatDriverModule;
extern dk_driver_info gHyperVTimeSyncDriverModule;


#endif // _HYPERV_IC_DRIVER_H_
