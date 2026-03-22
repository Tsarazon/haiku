/*
	Driver for USB Human Interface Devices.
	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
	Distributed under the terms of the MIT license.
*/
#ifndef _USB_HID_DRIVER_H_
#define _USB_HID_DRIVER_H_

#include <device_manager.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <OS.h>
#include <USB3.h>
#include <util/kernel_cpp.h>

#include <bus/USB.h>

#include "DeviceList.h"

#define DRIVER_NAME	"usb_hid"
#define DEVICE_PATH_SUFFIX	"usb"
#define DEVICE_NAME	"USB"

#define USB_INTERFACE_CLASS_HID			3
#define USB_INTERFACE_SUBCLASS_HID_BOOT	1
#define USB_DEFAULT_CONFIGURATION		0

extern usb_module_info *gUSBModule;
extern device_manager_info *gDeviceManager;
extern DeviceList *gDeviceList;

#define	TRACE(x...)			/*dprintf(DRIVER_NAME ": " x)*/
#define TRACE_ALWAYS(x...)	dprintf(DRIVER_NAME ": " x)

#endif //_USB_HID_DRIVER_H_
