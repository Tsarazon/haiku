/*
 *	Davicom DM9601 USB 1.1 Ethernet Driver.
 *	Copyright (c) 2008, 2011 Siarzhuk Zharski <imker@gmx.li>
 *	Copyright (c) 2009 Adrien Destugues <pulkomandy@gmail.com>
 *	Distributed under the terms of the MIT license.
 *
 *	Heavily based on code of the
 *	Driver for USB Ethernet Control Model devices
 *	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
 *	Distributed under the terms of the MIT license.
 */
#ifndef _USB_DAVICOM_DRIVER_H_
#define _USB_DAVICOM_DRIVER_H_


#include <device_manager.h>
#include <Drivers.h>
#include <USB3.h>


// extra tracing in debug mode
//#define UDAV_TRACE

#define DRIVER_NAME	"usb_davicom"


const char* const kVersion = "ver.0.9.5";
extern usb_module_info *gUSBModule;
extern device_manager_info *gDeviceManager;

class DavicomDevice;

// bus manager device interface for peripheral driver
typedef struct {
	driver_module_info info;
} usb_device_interface;

typedef struct {
	device_node*			node;
	::usb_device			usb_device;
	usb_device_interface*	usb;
	DavicomDevice*			device;
} usb_davicom_driver_info;


#endif // _USB_DAVICOM_DRIVER_H_
