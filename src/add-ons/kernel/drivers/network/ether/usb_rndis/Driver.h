/*
	Driver for USB RNDIS network devices
	Copyright (C) 2022 Adrien Destugues <pulkomandy@pulkomandy.tk>
	Distributed under the terms of the MIT license.
*/
#ifndef _USB_RNDIS_DRIVER_H_
#define _USB_RNDIS_DRIVER_H_

#include <device_keeper.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <OS.h>
#include <USB3.h>
#include <drivers/usb/USB_cdc.h>

#include <util/kernel_cpp.h>

#define DRIVER_NAME	"usb_rndis"

extern usb_module_info *gUSBModule;
extern dk_keeper_info *gDeviceKeeper;

class RNDISDevice;

typedef struct {
	dk_node*		node;
	int32			id;
	char			publishedPath[64];
	RNDISDevice*	device;
} usb_rndis_driver_info;

#if TRACE_RNDIS
#define TRACE(x...)			dprintf(DRIVER_NAME ": " x)
#else
#define TRACE(x...)
#endif
#define TRACE_ALWAYS(x...)	dprintf(DRIVER_NAME ": " x)

#endif //_USB_RNDIS_DRIVER_H_
