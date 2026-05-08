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

#define USB_SERIAL_DRIVER_MODULE_NAME "drivers/ports/usb_serial/dk_driver_v1"
#define USB_SERIAL_DEVICE_ID_GENERATOR "usb_serial/device_id"


SerialDevice *gSerialDevices[DEVICES_COUNT];
usb_module_info *gUSBModule = NULL;
tty_module_info *gTTYModule = NULL;
dk_keeper_info *gDeviceKeeper = NULL;
sem_id gDriverLock = -1;


typedef struct {
	dk_node*		node;
	SerialDevice*	device;
	int32			index;
	char			publishedPath[64];
} usb_serial_driver_info;


static status_t
usb_serial_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			load_settings();
			create_log_file();
			for (int32 i = 0; i < DEVICES_COUNT; i++)
				gSerialDevices[i] = NULL;
			gDriverLock = create_sem(1, DRIVER_NAME "_devices_table_lock");
			if (gDriverLock < 0)
				return gDriverLock;
			return B_OK;

		case B_MODULE_UNINIT:
			if (gDriverLock >= 0) {
				delete_sem(gDriverLock);
				gDriverLock = -1;
			}
			return B_OK;
	}

	return B_ERROR;
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


//	#pragma mark - dk_device_ops


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


static void
usb_serial_device_removed(void *_info)
{
	TRACE_FUNCALLS("> usb_serial_device_removed()\n");
	usb_serial_driver_info *info = (usb_serial_driver_info *)_info;
	if (info->device != NULL)
		info->device->Removed();
}


static dk_device_ops sDeviceOps = {
	usb_serial_open,
	usb_serial_close,
	usb_serial_free,
	usb_serial_read,
	usb_serial_write,
	NULL,	// io
	usb_serial_control,
	usb_serial_select,
	usb_serial_deselect,
	usb_serial_device_removed,
};


//	#pragma mark - dk_driver_info


static const dk_match_rule sUsbSerialMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "usb"),
	DK_MATCH_END
};

static const dk_match_dict sUsbSerialMatchDict = {
	sUsbSerialMatchRules,
	0
};


static float
usb_serial_probe(dk_node *node)
{
	TRACE_FUNCALLS("> usb_serial_probe()\n");

	// usb_serial matches all USB devices with low priority
	// SerialDevice::MakeDevice() does the actual vendor/product matching
	return 0.1;
}


static status_t
usb_serial_attach(dk_node *node, void **cookie)
{
	TRACE_FUNCALLS("> usb_serial_attach()\n");

	// get USB device ID
	usb_device usbDevice;
	if (gDeviceKeeper->get_property_uint32(node, USB_DEVICE_ID_ITEM,
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

	snprintf(info->publishedPath, sizeof(info->publishedPath), "%s%" B_PRId32,
		DEVICE_BASE_NAME, info->index);
	status = gDeviceKeeper->publish_device(node, info->publishedPath,
		&sDeviceOps);
	if (status != B_OK) {
		acquire_sem(gDriverLock);
		gSerialDevices[index] = NULL;
		release_sem(gDriverLock);
		delete serialDevice;
		free(info);
		return status;
	}

	*cookie = info;
	TRACE_ALWAYS("%s (0x%04x/0x%04x) added\n", serialDevice->Description(),
		descriptor->vendor_id, descriptor->product_id);
	return B_OK;
}


static void
usb_serial_detach(void *_cookie)
{
	TRACE_FUNCALLS("> usb_serial_detach()\n");
	usb_serial_driver_info *info = (usb_serial_driver_info *)_cookie;
	if (info == NULL)
		return;

	if (info->publishedPath[0] != '\0')
		gDeviceKeeper->unpublish_device(info->node, info->publishedPath);

	acquire_sem(gDriverLock);
	if (info->index >= 0 && info->index < DEVICES_COUNT)
		gSerialDevices[info->index] = NULL;
	release_sem(gDriverLock);

	delete info->device;
	free(info);
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&gDeviceKeeper },
	{ B_USB_MODULE_NAME, (module_info **)&gUSBModule },
	{ B_TTY_MODULE_NAME, (module_info **)&gTTYModule },
	{ NULL }
};

struct dk_driver_info sUsbSerialDriver = {
	.info = {
		USB_SERIAL_DRIVER_MODULE_NAME,
		0,
		usb_serial_std_ops
	},
	.match   = &sUsbSerialMatchDict,
	.probe   = usb_serial_probe,
	.attach  = usb_serial_attach,
	.detach  = usb_serial_detach,
	.ops     = &sDeviceOps,
};

module_info *modules[] = {
	(module_info *)&sUsbSerialDriver,
	NULL
};
