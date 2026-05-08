/*
 *	Driver for USB Audio Device Class devices.
 *	Copyright (c) 2009-13 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 *
 */
#ifndef _USB_AUDIO_DRIVER_H_
#define _USB_AUDIO_DRIVER_H_


#include <device_keeper.h>
#include <Drivers.h>
#include <USB3.h>


#define DRIVER_NAME	"usb_audio"

const char* const kVersion = "ver.0.0.5";

// initial buffer size in samples
const uint32 kSamplesBufferSize = 2048;
// [sub]buffers count
const uint32 kSamplesBufferCount = 2;


extern usb_module_info* gUSBModule;
extern dk_keeper_info* gDeviceKeeper;

class Device;

typedef struct {
	dk_node*	node;
	int32		id;
	char		publishedPath[64];
	Device*		device;
} usb_audio_driver_info;


#endif // _USB_AUDIO_DRIVER_H_
