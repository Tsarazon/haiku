/*
 * Copyright (c) 2007-2008 by Michael Lotz
 * Heavily based on the original usb_serial driver which is:
 *
 * Copyright (c) 2003 by Siarzhuk Zharski <imker@gmx.li>
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <Drivers.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>

#include "Driver.h"
#include "SerialDevice.h"
#include "USB3.h"


#define DEVICE_BASE_NAME "ports/usb"

#define USB_SERIAL_DRIVER_MODULE_NAME "drivers/ports/usb_serial/driver_v1"
#define USB_SERIAL_DEVICE_MODULE_NAME "drivers/ports/usb_serial/device_v1"
#define USB_SERIAL_DEVICE_ID_GENERATOR "usb_serial/device_id"


SerialDevice *gSerialDevices[DEVICES_COUNT];
usb_module_info *gUSBModule = NULL;
tty_module_info *gTTYModule = NULL;
device_manager_info *gDeviceManager = NULL;
sem_id gDriverLock = -1;
static bool sInitialized = false;


typedef struct {
	device_node*	node;
	SerialDevice*	device;
	int32			index;
} usb_serial_driver_info;


// Ensure global state is initialized once
static void
ensure_globals_initialized()
{
	if (sInitialized)
		return;

	load_settings();
	create_log_file();

	for (int32 i = 0; i < DEVICES_COUNT; i++)
		gSerialDevices[i] = NULL;

	gDriverLock = create_sem(1, DRIVER_NAME "_devices_table_lock");
	sInitialized = true;
}


bool
usb_serial_service(struct tty *tty, uint32 op, void *buffer, size_t length)
{
	TRACE_FUNCALLS("> usb_serial_service(%p, 0x%08lx, %p, %lu)\n", tty,
		op, buffer, length);

	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i]
			&& gSerialDevices[i]->Service(tty, op, buffer, length)) {
			TRACE_FUNCRET("< usb_serial_service() returns: true\n");
			return true;
		}
	}

	TRACE_FUNCRET("< usb_serial_service() returns: false\n");
	return false;
}


//	#pragma mark - device module API


static status_t
usb_serial_open(void *_info, const char *path, int openMode, void **_cookie)
{
	TRACE_FUNCALLS("> usb_serial_open(%s, 0x%08x)\n", path, openMode);
	usb_serial_driver_info *info = (usb_serial_driver_info *)_info;

	status_t status = info->device->Open(openMode);
	if (status != B_OK)
		return status;

	*_cookie = info->device;
	return B_OK;
}


static status_t
usb_serial_read(void *cookie, off_t position, void *buffer, size_t *numBytes)
{
	TRACE_FUNCALLS("> usb_serial_read(0x%08x, %lld, 0x%08x, %d)\n", cookie,
		position, buffer, *numBytes);
	SerialDevice *device = (SerialDevice *)cookie;
	status_t status = device->Read((char *)buffer, numBytes);
	TRACE_FUNCRET("< usb_serial_read() returns: 0x%08x\n", status);
	return status;
}


static status_t
usb_serial_write(void *cookie, off_t position, const void *buffer,
	size_t *numBytes)
{
	TRACE_FUNCALLS("> usb_serial_write(0x%08x, %lld, 0x%08x, %d)\n", cookie,
		position, buffer, *numBytes);
	SerialDevice *device = (SerialDevice *)cookie;
	status_t status = device->Write((const char *)buffer, numBytes);
	TRACE_FUNCRET("< usb_serial_write() returns: 0x%08x\n", status);
	return status;
}


static status_t
usb_serial_control(void *cookie, uint32 op, void *arg, size_t length)
{
	TRACE_FUNCALLS("> usb_serial_control(0x%08x, 0x%08x, 0x%08x, %d)\n",
		cookie, op, arg, length);
	SerialDevice *device = (SerialDevice *)cookie;
	status_t status = device->Control(op, arg, length);
	TRACE_FUNCRET("< usb_serial_control() returns: 0x%08x\n", status);
	return status;
}


static status_t
usb_serial_select(void *cookie, uint8 event, selectsync *sync)
{
	TRACE_FUNCALLS("> usb_serial_select(0x%08x, 0x%08x, %p)\n",
		cookie, event, sync);
	SerialDevice *device = (SerialDevice *)cookie;
	status_t status = device->Select(event, 0, sync);
	TRACE_FUNCRET("< usb_serial_select() returns: 0x%08x\n", status);
	return status;
}


static status_t
usb_serial_deselect(void *cookie, uint8 event, selectsync *sync)
{
	TRACE_FUNCALLS("> usb_serial_deselect(0x%08x, 0x%08x, %p)\n",
		cookie, event, sync);
	SerialDevice *device = (SerialDevice *)cookie;
	status_t status = device->DeSelect(event, sync);
	TRACE_FUNCRET("< usb_serial_deselect() returns: 0x%08x\n", status);
	return status;
}


static status_t
usb_serial_close(void *cookie)
{
	TRACE_FUNCALLS("> usb_serial_close(0x%08x)\n", cookie);
	SerialDevice *device = (SerialDevice *)cookie;
	status_t status = device->Close();
	TRACE_FUNCRET("< usb_serial_close() returns: 0x%08x\n", status);
	return status;
}


static status_t
usb_serial_free(void *cookie)
{
	TRACE_FUNCALLS("> usb_serial_free(0x%08x)\n", cookie);
	SerialDevice *device = (SerialDevice *)cookie;
	status_t status = device->Free();
	TRACE_FUNCRET("< usb_serial_free() returns: 0x%08x\n", status);
	return status;
}


//	#pragma mark - driver module API


static float
usb_serial_supports_device(device_node *parent)
{
	TRACE_FUNCALLS("> usb_serial_supports_device()\n");
	const char *bus;

	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;

	if (strcmp(bus, "usb"))
		return 0.0;

	// usb_serial matches all USB devices with low priority
	// SerialDevice::MakeDevice() does the actual vendor/product matching
	return 0.1;
}


static status_t
usb_serial_register_device(device_node *node)
{
	TRACE_FUNCALLS("> usb_serial_register_device()\n");

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "USB Serial"} },
		{ NULL }
	};

	return gDeviceManager->register_node(node,
		USB_SERIAL_DRIVER_MODULE_NAME, attrs, NULL, NULL);
}


static status_t
usb_serial_init_driver(device_node *node, void **cookie)
{
	TRACE_FUNCALLS("> usb_serial_init_driver()\n");

	ensure_globals_initialized();

	// get USB device ID
	usb_device usbDevice;
	if (gDeviceManager->get_attr_uint32(node, USB_DEVICE_ID_ITEM,
			&usbDevice, true) != B_OK) {
		return B_ERROR;
	}

	const usb_device_descriptor *descriptor
		= gUSBModule->get_device_descriptor(usbDevice);
	if (descriptor == NULL)
		return B_ERROR;

	TRACE_ALWAYS("probing device: 0x%04x/0x%04x\n", descriptor->vendor_id,
		descriptor->product_id);

	SerialDevice *serialDevice = SerialDevice::MakeDevice(usbDevice,
		descriptor->vendor_id, descriptor->product_id);

	status_t status = B_ERROR;
	const usb_configuration_info *configuration;
	for (int i = 0; i < descriptor->num_configurations; i++) {
		configuration = gUSBModule->get_nth_configuration(usbDevice, i);
		if (configuration == NULL)
			continue;

		status = serialDevice->AddDevice(configuration);
		if (status == B_OK)
			break;
	}

	if (status < B_OK) {
		delete serialDevice;
		return status;
	}

	status = serialDevice->Init();
	if (status < B_OK) {
		delete serialDevice;
		return status;
	}

	// find a slot in the global device array (for TTY service routing)
	acquire_sem(gDriverLock);
	int32 index = -1;
	for (int32 i = 0; i < DEVICES_COUNT; i++) {
		if (gSerialDevices[i] == NULL) {
			gSerialDevices[i] = serialDevice;
			index = i;
			break;
		}
	}
	release_sem(gDriverLock);

	if (index < 0) {
		delete serialDevice;
		return B_NO_MORE_FDS;
	}

	usb_serial_driver_info *info = (usb_serial_driver_info *)malloc(
		sizeof(usb_serial_driver_info));
	if (info == NULL) {
		acquire_sem(gDriverLock);
		gSerialDevices[index] = NULL;
		release_sem(gDriverLock);
		delete serialDevice;
		return B_NO_MEMORY;
	}

	info->node = node;
	info->device = serialDevice;
	info->index = index;

	*cookie = info;
	TRACE_ALWAYS("%s (0x%04x/0x%04x) added\n", serialDevice->Description(),
		descriptor->vendor_id, descriptor->product_id);
	return B_OK;
}


static void
usb_serial_uninit_driver(void *_cookie)
{
	TRACE_FUNCALLS("> usb_serial_uninit_driver()\n");
	usb_serial_driver_info *info = (usb_serial_driver_info *)_cookie;

	acquire_sem(gDriverLock);
	if (info->index >= 0 && info->index < DEVICES_COUNT)
		gSerialDevices[info->index] = NULL;
	release_sem(gDriverLock);

	delete info->device;
	free(info);
}


static status_t
usb_serial_register_child_devices(void *_cookie)
{
	TRACE_FUNCALLS("> usb_serial_register_child_devices()\n");
	usb_serial_driver_info *info = (usb_serial_driver_info *)_cookie;

	char name[64];
	snprintf(name, sizeof(name), "%s%" B_PRId32, DEVICE_BASE_NAME,
		info->index);

	return gDeviceManager->publish_device(info->node, name,
		USB_SERIAL_DEVICE_MODULE_NAME);
}


static void
usb_serial_driver_device_removed(void *_cookie)
{
	TRACE_FUNCALLS("> usb_serial_driver_device_removed()\n");
	usb_serial_driver_info *info = (usb_serial_driver_info *)_cookie;
	if (info->device != NULL)
		info->device->Removed();
}


//	#pragma mark - device init/uninit


static status_t
usb_serial_init_device(void *_info, void **_cookie)
{
	*_cookie = _info;
	return B_OK;
}


static void
usb_serial_uninit_device(void *_cookie)
{
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&gDeviceManager },
	{ B_USB_MODULE_NAME, (module_info **)&gUSBModule },
	{ B_TTY_MODULE_NAME, (module_info **)&gTTYModule },
	{ NULL }
};

struct device_module_info sUsbSerialDevice = {
	{
		USB_SERIAL_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	usb_serial_init_device,
	usb_serial_uninit_device,
	NULL,	// device_removed

	usb_serial_open,
	usb_serial_close,
	usb_serial_free,
	usb_serial_read,
	usb_serial_write,
	NULL,	// io
	usb_serial_control,

	usb_serial_select,
	usb_serial_deselect,
};

struct driver_module_info sUsbSerialDriver = {
	{
		USB_SERIAL_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	usb_serial_supports_device,
	usb_serial_register_device,
	usb_serial_init_driver,
	usb_serial_uninit_driver,
	usb_serial_register_child_devices,
	NULL,	// rescan
	usb_serial_driver_device_removed,
};

module_info *modules[] = {
	(module_info *)&sUsbSerialDriver,
	(module_info *)&sUsbSerialDevice,
	NULL
};
