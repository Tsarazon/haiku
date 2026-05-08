/*
 * Copyright 2026 John Davis. All rights reserved.
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2008-2011 Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT License.
 */


#include "DeviceList.h"
#include "Driver.h"
#include "HIDDevice.h"
#include "ProtocolHandler.h"

#include <lock.h>
#include <util/AutoLock.h>


typedef struct {
	dk_node*					node;
	hyperv_device_interface*	hyperv;
	hyperv_device				hyperv_cookie;
	HIDDevice*					hid_device;
} hid_driver_cookie;


typedef struct {
	ProtocolHandler*	handler;
	uint32				cookie;
	hid_driver_cookie*	driver_cookie;
} device_cookie;


static dk_keeper_info* sDeviceKeeper;
DeviceList* gDeviceList = NULL;
static mutex sDriverLock;


static status_t
hyperv_hid_open(void* deviceCookie, const char* path, int openMode, void** _cookie)
{
	CALLED();

	device_cookie* cookie = new(std::nothrow) device_cookie();
	if (cookie == NULL)
		return B_NO_MEMORY;
	cookie->driver_cookie = reinterpret_cast<hid_driver_cookie*>(deviceCookie);

	MutexLocker locker(sDriverLock);
	ProtocolHandler* handler = reinterpret_cast<ProtocolHandler*>(gDeviceList->FindDevice(path));
	if (handler == NULL) {
		delete cookie;
		return B_ENTRY_NOT_FOUND;
	}

	cookie->handler = handler;
	cookie->cookie = 0;

	status_t status = handler->Open(openMode, &cookie->cookie);
	if (status != B_OK) {
		delete cookie;
		return status;
	}

	*_cookie = cookie;
	return B_OK;
}


static status_t
hyperv_hid_close(void* cookie)
{
	CALLED();
	device_cookie* deviceCookie = reinterpret_cast<device_cookie*>(cookie);
	return deviceCookie->handler->Close(&deviceCookie->cookie);
}


static status_t
hyperv_hid_free(void* cookie)
{
	CALLED();

	device_cookie* deviceCookie = reinterpret_cast<device_cookie*>(cookie);

	mutex_lock(&sDriverLock);

	HIDDevice* device = deviceCookie->handler->Device();
	if (device->IsOpen()) {
		// Another handler of this device is still open so we can't free it
	} else if (device->IsRemoved()) {
		// The parent device is removed already and none of its handlers are
		// open anymore so we can free it here
		delete device;
	}

	mutex_unlock(&sDriverLock);

	delete deviceCookie;
	return B_OK;
}


static status_t
hyperv_hid_read(void* cookie, off_t pos, void* buffer, size_t* _length)
{
	CALLED();
	device_cookie* deviceCookie = reinterpret_cast<device_cookie*>(cookie);
	return deviceCookie->handler->Read(&deviceCookie->cookie, pos, buffer, _length);
}


static status_t
hyperv_hid_write(void* cookie, off_t pos, const void* buffer, size_t* _length)
{
	CALLED();
	device_cookie* deviceCookie = reinterpret_cast<device_cookie*>(cookie);
	return deviceCookie->handler->Write(&deviceCookie->cookie, pos, buffer, _length);
}


static status_t
hyperv_hid_control(void* cookie, uint32 op, void* buffer, size_t length)
{
	CALLED();
	device_cookie* deviceCookie = reinterpret_cast<device_cookie*>(cookie);
	return deviceCookie->handler->Control(&deviceCookie->cookie, op, buffer, length);
}


static float
hyperv_hid_supports_device(dk_node* parent)
{
	CALLED();

	// Check if parent is the Hyper-V bus manager
	char bus[64];
	if (sDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1;

	if (strcmp(bus, HYPERV_BUS_NAME) != 0)
		return 0.0f;

	// Check if parent is a Hyper-V Input device
	char type[64];
	if (sDeviceKeeper->get_property_string(parent, HYPERV_DEVICE_TYPE_STRING_ITEM,
			type, sizeof(type), NULL, false) != B_OK)
		return 0.0f;

	if (strcmp(type, VMBUS_TYPE_INPUT) != 0)
		return 0.0f;

	TRACE("Hyper-V HID device found!\n");
	return 0.8f;
}


static dk_device_ops sDeviceOps = {
	hyperv_hid_open,
	hyperv_hid_close,
	hyperv_hid_free,
	hyperv_hid_read,
	hyperv_hid_write,
	NULL,	// io
	hyperv_hid_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


static status_t hyperv_hid_publish_devices(hid_driver_cookie* hidCookie);

static status_t
hyperv_hid_attach(dk_node* node, void** _driverCookie)
{
	CALLED();

	hid_driver_cookie* hidCookie = new(std::nothrow) hid_driver_cookie;
	if (hidCookie == NULL)
		return B_NO_MEMORY;
	memset(hidCookie, 0, sizeof(*hidCookie));

	hidCookie->node = node;

	status_t status = sDeviceKeeper->get_interface(node,
		HYPERV_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&hidCookie->hyperv, (void**)&hidCookie->hyperv_cookie);
	if (status != B_OK) {
		ERROR("failed to get Hyper-V device interface\n");
		delete hidCookie;
		return status;
	}

	mutex_lock(&sDriverLock);
	HIDDevice* hidDevice = new(std::nothrow) HIDDevice(hidCookie->hyperv,
		hidCookie->hyperv_cookie);
	if (hidDevice == NULL) {
		mutex_unlock(&sDriverLock);
		delete hidCookie;
		return B_NO_MEMORY;
	}

	status = hidDevice->InitCheck();
	if (status != B_OK) {
		mutex_unlock(&sDriverLock);
		delete hidDevice;
		delete hidCookie;
		return status;
	}

	hidCookie->hid_device = hidDevice;

	mutex_unlock(&sDriverLock);

	// Publish child devices in devfs
	hyperv_hid_publish_devices(hidCookie);

	*_driverCookie = hidCookie;
	return B_OK;
}


static void
hyperv_hid_detach(void* driverCookie)
{
	CALLED();
	hid_driver_cookie* hidCookie = reinterpret_cast<hid_driver_cookie*>(driverCookie);
	if (hidCookie == NULL)
		return;

	MutexLocker locker(sDriverLock);

	HIDDevice* hidDevice = hidCookie->hid_device;
	if (hidDevice != NULL) {
		for (uint32 i = 0;; i++) {
			ProtocolHandler* handler = hidDevice->ProtocolHandlerAt(i);
			if (handler == NULL)
				break;
			if (handler->PublishPath() != NULL) {
				sDeviceKeeper->unpublish_device(hidCookie->node,
					handler->PublishPath());
				gDeviceList->RemoveDevice(NULL, handler);
			}
		}

		if (!hidDevice->IsOpen())
			delete hidDevice;
		else
			hidDevice->Removed();
	}

	locker.Unlock();
	delete hidCookie;
}


static status_t
hyperv_hid_publish_devices(hid_driver_cookie* hidCookie)
{
	HIDDevice* hidDevice = hidCookie->hid_device;

	for (uint32 i = 0;; i++) {
		ProtocolHandler* handler = hidDevice->ProtocolHandlerAt(i);
		if (handler == NULL)
			break;

		// As devices can be un- and replugged at will, we cannot
		// simply rely on a device count. If there is just one
		// keyboard, this does not mean that it uses the 0 name.
		// There might have been two keyboards and the one using 0
		// might have been unplugged. So we just generate names
		// until we find one that is not currently in use.
		int32 index = 0;
		char pathBuffer[B_DEV_NAME_LENGTH];
		const char* basePath = handler->BasePath();
		while (true) {
			sprintf(pathBuffer, "%s%" B_PRId32, basePath, index++);
			if (gDeviceList->FindDevice(pathBuffer) == NULL) {
				// Name is still free
				handler->SetPublishPath(strdup(pathBuffer));
				break;
			}
		}

		gDeviceList->AddDevice(handler->PublishPath(), handler);
		sDeviceKeeper->publish_device(hidCookie->node, pathBuffer,
			&sDeviceOps);
	}

	return B_OK;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			gDeviceList = new(std::nothrow) DeviceList();
			if (gDeviceList == NULL)
				return B_NO_MEMORY;
			mutex_init(&sDriverLock, "hyper-v hid driver lock");

			return B_OK;
		case B_MODULE_UNINIT:
			delete gDeviceList;
			gDeviceList = NULL;
			mutex_destroy(&sDriverLock);
			return B_OK;

		default:
			break;
	}

	return B_ERROR;
}


static const dk_match_rule sHyperVHIDMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, HYPERV_BUS_NAME),
	DK_MATCH_END
};

static const dk_match_dict sHyperVHIDMatchDict = {
	sHyperVHIDMatchRules,
	0
};

static dk_driver_info sHyperVHIDDriverModule = {
	.info   = { HYPERV_HID_DRIVER_MODULE_NAME, 0, &std_ops },
	.match  = &sHyperVHIDMatchDict,
	.probe  = hyperv_hid_supports_device,
	.attach = hyperv_hid_attach,
	.detach = hyperv_hid_detach,
	.ops    = &sDeviceOps,
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&sDeviceKeeper },
	{ NULL }
};


module_info* modules[] = {
	(module_info*)&sHyperVHIDDriverModule,
	NULL
};
