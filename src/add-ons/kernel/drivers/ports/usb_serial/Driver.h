/*
 * Copyright (c) 2007-2008 by Michael Lotz
 * Heavily based on the original usb_serial driver which is:
 *
 * Copyright (c) 2003 by Siarzhuk Zharski <imker@gmx.li>
 * Distributed under the terms of the MIT License.
 */
#ifndef _USB_SERIAL_DRIVER_H_
#define _USB_SERIAL_DRIVER_H_

#include <device_manager.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <OS.h>
#include <USB3.h>

#include <bus/USB.h>
#include <usb/USB_cdc.h>

#include <lock.h>
#include <string.h>

#include "Tracing.h"

extern "C" {
#include <tty_module.h>
}


#define DRIVER_NAME		"usb_serial"	// driver name for debug output
#define DEVICES_COUNT	20				// max simultaneously open devices

/* This one rounds the size to integral count of segs (segments) */
#define ROUNDUP(size, seg) (((size) + (seg) - 1) & ~((seg) - 1))
/* Default device buffer size */
#define DEF_BUFFER_SIZE	0x200


extern usb_module_info *gUSBModule;
extern tty_module_info *gTTYModule;
extern device_manager_info *gDeviceManager;
extern struct ddomain gSerialDomain;

class SerialDevice;

// Global device list for TTY service routing
extern SerialDevice *gSerialDevices[DEVICES_COUNT];
extern sem_id gDriverLock;

extern "C" {
bool		usb_serial_service(struct tty *tty, uint32 op, void *buffer,
				size_t length);
}

#endif //_USB_SERIAL_DRIVER_H_
