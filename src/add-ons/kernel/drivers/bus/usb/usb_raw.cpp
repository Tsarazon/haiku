/*
 * Copyright 2006-2018, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Adrien Destugues <pulkomandy@pulkomandy.tk>
 */

#include "usb_raw.h"

#include <util/AutoLock.h>
#include <AutoDeleter.h>
#include <device_keeper.h>
#include <KernelExport.h>
#include <Drivers.h>
#include <algorithm>
#include <lock.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <bus/USB.h>
#include <kernel.h>


//#define TRACE_USB_RAW
#ifdef TRACE_USB_RAW
#define TRACE(x)	dprintf x
#else
#define TRACE(x)	/* nothing */
#endif

#define DRIVER_NAME		"usb_raw"
#define DEVICE_NAME		"bus/usb"


#define USB_RAW_DRIVER_MODULE_NAME "drivers/bus/usb_raw/dk_driver_v1"
#define USB_RAW_DEVICE_ID_GENERATOR "usb_raw/device_id"


typedef struct {
	dk_node*			node;
	usb_device			device;
	mutex				lock;
	uint32				reference_count;
	char				name[64];
	sem_id				notify;
	status_t			status;
	size_t				actual_length;
} raw_device;

static usb_module_info *gUSBModule = NULL;
static dk_keeper_info *sDeviceKeeper = NULL;


//
//#pragma mark - Helper Functions
//


static const usb_configuration_info *
usb_raw_get_configuration(raw_device *device, uint32 configIndex,
	status_t *status)
{
	const usb_configuration_info *result = gUSBModule->get_nth_configuration(
		device->device, configIndex);
	if (result == NULL) {
		*status = B_USB_RAW_STATUS_INVALID_CONFIGURATION;
		return NULL;
	}

	return result;
}


static const usb_interface_info *
usb_raw_get_interface(raw_device *device, uint32 configIndex,
	uint32 interfaceIndex, uint32 alternateIndex, status_t *status)
{
	const usb_configuration_info *configurationInfo
		= usb_raw_get_configuration(device, configIndex, status);
	if (configurationInfo == NULL)
		return NULL;

	if (interfaceIndex >= configurationInfo->interface_count) {
		*status = B_USB_RAW_STATUS_INVALID_INTERFACE;
		return NULL;
	}

	const usb_interface_info *result = NULL;
	if (alternateIndex == B_USB_RAW_ACTIVE_ALTERNATE)
		result = configurationInfo->interface[interfaceIndex].active;
	else {
		if (alternateIndex >= configurationInfo->interface[interfaceIndex].alt_count) {
			*status = B_USB_RAW_STATUS_INVALID_INTERFACE;
			return NULL;
		}
		result = &configurationInfo->interface[interfaceIndex].alt[alternateIndex];
	}

	return result;
}


static void
usb_raw_callback(void *cookie, status_t status, void *data,
	size_t actualLength)
{
	raw_device *device = (raw_device *)cookie;
	device->status = status;
	device->actual_length = actualLength;
	release_sem(device->notify);
}


//
//#pragma mark - device module API
//


static status_t
usb_raw_open(void *_info, const char *path, int openMode, void **_cookie)
{
	TRACE((DRIVER_NAME": open(%s)\n", path));
	raw_device *device = (raw_device *)_info;
	device->reference_count++;
	*_cookie = device;
	return B_OK;
}


static status_t
usb_raw_close(void *cookie)
{
	TRACE((DRIVER_NAME": close()\n"));
	return B_OK;
}


static status_t
usb_raw_free(void *cookie)
{
	TRACE((DRIVER_NAME": free()\n"));
	raw_device *device = (raw_device *)cookie;
	device->reference_count--;
	return B_OK;
}


static status_t
usb_raw_ioctl(void *cookie, uint32 op, void *buffer, size_t length)
{
	TRACE((DRIVER_NAME": ioctl\n"));
	raw_device *device = (raw_device *)cookie;
	if (device->device == 0)
		return B_DEV_NOT_READY;

	usb_raw_command command;
	if (length < sizeof(command)) {
		TRACE((DRIVER_NAME": raw ioctl buffer too small\n"));
		return B_BUFFER_OVERFLOW;
	}

	if (!IS_USER_ADDRESS(buffer) || user_memcpy(&command, buffer,
			sizeof(command)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	command.version.status = B_USB_RAW_STATUS_FAILED;
	status_t status = B_OK;

	switch (op) {
		case B_USB_RAW_COMMAND_GET_VERSION:
		{
			command.version.status = B_USB_RAW_PROTOCOL_VERSION;
			break;
		}

		case B_USB_RAW_COMMAND_GET_DEVICE_DESCRIPTOR:
		{
			const usb_device_descriptor *deviceDescriptor =
				gUSBModule->get_device_descriptor(device->device);
			if (deviceDescriptor == NULL) {
				command.device.status = B_USB_RAW_STATUS_ABORTED;
				break;
			}

			if (user_memcpy(command.device.descriptor,
					deviceDescriptor, sizeof(usb_device_descriptor))
					!= B_OK) {
				return B_BAD_ADDRESS;
			}
			command.device.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_CONFIGURATION_DESCRIPTOR:
		{
			const usb_configuration_info *configurationInfo
				= usb_raw_get_configuration(device,
					command.config.config_index, &command.config.status);
			if (configurationInfo == NULL)
				break;

			if (user_memcpy(command.config.descriptor,
					configurationInfo->descr,
					sizeof(usb_configuration_descriptor)) != B_OK) {
				return B_BAD_ADDRESS;
			}
			command.config.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_CONFIGURATION_DESCRIPTOR_ETC:
		{
			const usb_configuration_info *configurationInfo
				= usb_raw_get_configuration(device,
					command.config_etc.config_index,
					&command.config_etc.status);
			if (configurationInfo == NULL)
				break;

			size_t actualLength = std::min(command.config_etc.length,
				(size_t)configurationInfo->descr->total_length);
			const usb_configuration_descriptor *descriptor
				= gUSBModule->get_nth_configuration(device->device,
					command.config_etc.config_index)->descr;

			if (user_memcpy(command.config_etc.descriptor,
					descriptor, actualLength) != B_OK) {
				return B_BAD_ADDRESS;
			}
			command.config_etc.length = actualLength;
			command.config_etc.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_ALT_INTERFACE_COUNT:
		{
			const usb_configuration_info *configurationInfo
				= usb_raw_get_configuration(device,
					command.alternate.config_index,
					&command.alternate.status);
			if (configurationInfo == NULL)
				break;

			uint32 interfaceIndex = command.alternate.interface_index;
			if (interfaceIndex >= configurationInfo->interface_count) {
				command.alternate.status
					= B_USB_RAW_STATUS_INVALID_INTERFACE;
				break;
			}

			command.alternate.alternate_info
				= configurationInfo->interface[interfaceIndex].alt_count;
			command.alternate.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_ACTIVE_ALT_INTERFACE_INDEX:
		{
			const usb_configuration_info *configurationInfo
				= usb_raw_get_configuration(device,
					command.alternate.config_index,
					&command.alternate.status);
			if (configurationInfo == NULL)
				break;

			uint32 interfaceIndex = command.alternate.interface_index;
			if (interfaceIndex >= configurationInfo->interface_count) {
				command.alternate.status
					= B_USB_RAW_STATUS_INVALID_INTERFACE;
				break;
			}

			for (uint32 i = 0; i < configurationInfo->interface[
					interfaceIndex].alt_count; i++) {
				if (&configurationInfo->interface[interfaceIndex].alt[i]
						== configurationInfo->interface[
							interfaceIndex].active) {
					command.alternate.alternate_info = i;
					break;
				}
			}
			command.alternate.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_INTERFACE_DESCRIPTOR:
		case B_USB_RAW_COMMAND_GET_INTERFACE_DESCRIPTOR_ETC:
		{
			uint32 configIndex = command.interface.config_index;
			uint32 interfaceIndex = command.interface.interface_index;
			uint32 alternateIndex = B_USB_RAW_ACTIVE_ALTERNATE;
			if (op == B_USB_RAW_COMMAND_GET_INTERFACE_DESCRIPTOR_ETC)
				alternateIndex = command.interface_etc.alternate_index;

			const usb_interface_info *interfaceInfo = usb_raw_get_interface(
				device, configIndex, interfaceIndex, alternateIndex,
				&command.interface.status);
			if (interfaceInfo == NULL)
				break;

			if (user_memcpy(command.interface.descriptor,
					interfaceInfo->descr,
					sizeof(usb_interface_descriptor)) != B_OK) {
				return B_BAD_ADDRESS;
			}
			command.interface.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_ENDPOINT_DESCRIPTOR:
		case B_USB_RAW_COMMAND_GET_ENDPOINT_DESCRIPTOR_ETC:
		{
			uint32 configIndex = command.endpoint.config_index;
			uint32 interfaceIndex = command.endpoint.interface_index;
			uint32 endpointIndex = command.endpoint.endpoint_index;
			uint32 alternateIndex = B_USB_RAW_ACTIVE_ALTERNATE;
			if (op == B_USB_RAW_COMMAND_GET_ENDPOINT_DESCRIPTOR_ETC)
				alternateIndex = command.endpoint_etc.alternate_index;

			const usb_interface_info *interfaceInfo = usb_raw_get_interface(
				device, configIndex, interfaceIndex, alternateIndex,
				&command.endpoint.status);
			if (interfaceInfo == NULL)
				break;

			if (endpointIndex >= interfaceInfo->endpoint_count) {
				command.endpoint.status
					= B_USB_RAW_STATUS_INVALID_ENDPOINT;
				break;
			}

			if (user_memcpy(command.endpoint.descriptor,
					interfaceInfo->endpoint[endpointIndex].descr,
					sizeof(usb_endpoint_descriptor)) != B_OK) {
				return B_BAD_ADDRESS;
			}
			command.endpoint.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_GENERIC_DESCRIPTOR:
		case B_USB_RAW_COMMAND_GET_GENERIC_DESCRIPTOR_ETC:
		{
			uint32 configIndex = command.generic.config_index;
			uint32 interfaceIndex = command.generic.interface_index;
			uint32 genericIndex = command.generic.generic_index;
			uint32 alternateIndex = B_USB_RAW_ACTIVE_ALTERNATE;
			if (op == B_USB_RAW_COMMAND_GET_GENERIC_DESCRIPTOR_ETC)
				alternateIndex = command.generic_etc.alternate_index;

			const usb_interface_info *interfaceInfo = usb_raw_get_interface(
				device, configIndex, interfaceIndex, alternateIndex,
				&command.generic.status);
			if (interfaceInfo == NULL)
				break;

			if (genericIndex >= interfaceInfo->generic_count) {
				command.generic.status = B_USB_RAW_STATUS_INVALID_ENDPOINT;
				break;
			}

			usb_descriptor *descriptor
				= interfaceInfo->generic[genericIndex];
			if (descriptor == NULL)
				break;

			size_t actualLength = std::min(command.generic.length,
				(size_t)descriptor->generic.length);
			if (user_memcpy(command.generic.descriptor,
					descriptor, actualLength) != B_OK) {
				return B_BAD_ADDRESS;
			}
			command.generic.length = actualLength;
			command.generic.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_STRING_DESCRIPTOR:
		{
			size_t actualLength = std::min(command.string.length,
				(size_t)256);
			uint8 stringBuffer[256];

			status = gUSBModule->get_descriptor(device->device,
				USB_DESCRIPTOR_STRING, command.string.string_index, 0,
				stringBuffer, actualLength, &actualLength);
			if (status < B_OK) {
				command.string.status = B_USB_RAW_STATUS_ABORTED;
				command.string.length = 0;
				break;
			}

			if (user_memcpy(command.string.descriptor, stringBuffer,
					actualLength) != B_OK) {
				return B_BAD_ADDRESS;
			}
			command.string.length = actualLength;
			command.string.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_DESCRIPTOR:
		{
			size_t actualLength = 0;
			uint8 *descriptorBuffer = (uint8 *)malloc(command.descriptor.length);
			if (descriptorBuffer == NULL) {
				command.descriptor.status = B_USB_RAW_STATUS_NO_MEMORY;
				command.descriptor.length = 0;
				break;
			}

			status = gUSBModule->get_descriptor(device->device,
				command.descriptor.type, command.descriptor.index,
				command.descriptor.language_id, descriptorBuffer,
				command.descriptor.length, &actualLength);
			if (status < B_OK) {
				command.descriptor.status = B_USB_RAW_STATUS_ABORTED;
				command.descriptor.length = 0;
				free(descriptorBuffer);
				break;
			}

			if (user_memcpy(command.descriptor.data, descriptorBuffer,
					actualLength) != B_OK) {
				free(descriptorBuffer);
				return B_BAD_ADDRESS;
			}
			command.descriptor.length = actualLength;
			command.descriptor.status = B_USB_RAW_STATUS_SUCCESS;
			free(descriptorBuffer);
			break;
		}

		case B_USB_RAW_COMMAND_SET_CONFIGURATION:
		{
			const usb_configuration_info *configurationInfo
				= usb_raw_get_configuration(device,
					command.config.config_index, &command.config.status);
			if (configurationInfo == NULL)
				break;

			if (gUSBModule->set_configuration(device->device,
					configurationInfo) < B_OK) {
				command.config.status = B_USB_RAW_STATUS_FAILED;
				break;
			}

			command.config.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_SET_ALT_INTERFACE:
		{
			const usb_interface_info *interfaceInfo = usb_raw_get_interface(
				device, command.alternate.config_index,
				command.alternate.interface_index,
				command.alternate.alternate_info, &command.alternate.status);
			if (interfaceInfo == NULL)
				break;

			if (gUSBModule->set_alt_interface(device->device,
					interfaceInfo) < B_OK) {
				command.alternate.status = B_USB_RAW_STATUS_FAILED;
				break;
			}

			command.alternate.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_CONTROL_TRANSFER:
		{
			void *controlData = NULL;
			MemoryDeleter dataDeleter;
			bool inTransfer = (command.control.request_type
				& USB_REQTYPE_DEVICE_IN) != 0;

			if (command.control.length > 0) {
				controlData = malloc(command.control.length);
				if (controlData == NULL) {
					command.control.status = B_USB_RAW_STATUS_NO_MEMORY;
					command.control.length = 0;
					break;
				}

				dataDeleter.SetTo(controlData);
				if (!IS_USER_ADDRESS(command.control.data)
					|| (!inTransfer && user_memcpy(controlData,
						command.control.data, command.control.length)
						!= B_OK)) {
					return B_BAD_ADDRESS;
				}
			}

			size_t actualLength = command.control.length;
			MutexLocker deviceLocker(device->lock);
			status = gUSBModule->send_request(device->device,
				command.control.request_type, command.control.request,
				command.control.value, command.control.index,
				command.control.length, controlData,
				&actualLength);
			command.control.length = actualLength;
			deviceLocker.Unlock();

			if (status < B_OK) {
				command.control.status = B_USB_RAW_STATUS_FAILED;
				command.control.length = 0;
				break;
			}

			if (inTransfer && command.control.length > 0
				&& user_memcpy(command.control.data, controlData,
					command.control.length) != B_OK) {
				return B_BAD_ADDRESS;
			}

			command.control.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_INTERRUPT_TRANSFER:
		case B_USB_RAW_COMMAND_BULK_TRANSFER:
		case B_USB_RAW_COMMAND_ISOCHRONOUS_TRANSFER:
		{
			const usb_configuration_info *configurationInfo
				= gUSBModule->get_configuration(device->device);
			if (configurationInfo == NULL) {
				command.transfer.status = B_USB_RAW_STATUS_ABORTED;
				command.transfer.length = 0;
				break;
			}

			if (command.transfer.interface
					>= configurationInfo->interface_count) {
				command.transfer.status
					= B_USB_RAW_STATUS_INVALID_INTERFACE;
				command.transfer.length = 0;
				break;
			}

			const usb_interface_info *interfaceInfo
				= configurationInfo->interface[
					command.transfer.interface].active;
			if (interfaceInfo == NULL) {
				command.transfer.status
					= B_USB_RAW_STATUS_ABORTED;
				command.transfer.length = 0;
				break;
			}

			if (command.transfer.endpoint
					>= interfaceInfo->endpoint_count) {
				command.transfer.status
					= B_USB_RAW_STATUS_INVALID_ENDPOINT;
				command.transfer.length = 0;
				break;
			}

			const usb_endpoint_info *endpointInfo
				= &interfaceInfo->endpoint[command.transfer.endpoint];
			if (endpointInfo == NULL || endpointInfo->handle == 0) {
				command.transfer.status = B_USB_RAW_STATUS_INVALID_ENDPOINT;
				command.transfer.length = 0;
				break;
			}

			size_t descriptorsSize = 0;
			usb_iso_packet_descriptor *packetDescriptors = NULL;
			MemoryDeleter descriptorsDeleter;

			void *transferData = NULL;
			MemoryDeleter dataDeleter;

			bool inTransfer = (endpointInfo->descr->endpoint_address
				& USB_ENDPOINT_ADDR_DIR_IN) != 0;

			if (op == B_USB_RAW_COMMAND_ISOCHRONOUS_TRANSFER) {
				descriptorsSize = command.isochronous.packet_count
					* sizeof(usb_iso_packet_descriptor);
				packetDescriptors
					= (usb_iso_packet_descriptor *)malloc(descriptorsSize);
				if (packetDescriptors == NULL) {
					command.transfer.status = B_USB_RAW_STATUS_NO_MEMORY;
					command.transfer.length = 0;
					break;
				}

				descriptorsDeleter.SetTo(packetDescriptors);
				if (user_memcpy(packetDescriptors,
						command.isochronous.packet_descriptors,
						descriptorsSize) != B_OK) {
					return B_BAD_ADDRESS;
				}
			} else {
				transferData = malloc(command.transfer.length);
				if (transferData == NULL) {
					command.transfer.status = B_USB_RAW_STATUS_NO_MEMORY;
					command.transfer.length = 0;
					break;
				}
				dataDeleter.SetTo(transferData);

				if (!IS_USER_ADDRESS(command.transfer.data) || (!inTransfer
						&& user_memcpy(transferData, command.transfer.data,
							command.transfer.length) != B_OK)) {
					return B_BAD_ADDRESS;
				}
			}

			status_t status;
			MutexLocker deviceLocker(device->lock);
			if (op == B_USB_RAW_COMMAND_INTERRUPT_TRANSFER) {
				status = gUSBModule->queue_interrupt(endpointInfo->handle,
					transferData, command.transfer.length,
					usb_raw_callback, device);
			} else if (op == B_USB_RAW_COMMAND_BULK_TRANSFER) {
				status = gUSBModule->queue_bulk(endpointInfo->handle,
					transferData, command.transfer.length,
					usb_raw_callback, device);
			} else {
				status = gUSBModule->queue_isochronous(endpointInfo->handle,
					command.isochronous.data, command.isochronous.length,
					packetDescriptors, command.isochronous.packet_count, NULL,
					0, usb_raw_callback, device);
			}

			if (status < B_OK) {
				command.transfer.status = B_USB_RAW_STATUS_FAILED;
				command.transfer.length = 0;
				status = B_OK;
				break;
			}

			status = acquire_sem_etc(device->notify, 1,
				B_KILL_CAN_INTERRUPT, 0);
			if (status != B_OK) {
				gUSBModule->cancel_queued_transfers(endpointInfo->handle);
				acquire_sem(device->notify);
			}

			command.transfer.status = device->status;
			command.transfer.length = device->actual_length;
			deviceLocker.Unlock();

			if (command.transfer.status == B_OK)
				status = B_OK;
			if (op == B_USB_RAW_COMMAND_ISOCHRONOUS_TRANSFER) {
				if (user_memcpy(command.isochronous.packet_descriptors,
						packetDescriptors, descriptorsSize) != B_OK) {
					status = B_BAD_ADDRESS;
				}
			} else {
				if (inTransfer && user_memcpy(command.transfer.data,
					transferData, command.transfer.length) != B_OK) {
					status = B_BAD_ADDRESS;
				}
			}

			break;
		}
	}

	if (user_memcpy(buffer, &command, min_c(length, sizeof(command))) != B_OK)
		return B_BAD_ADDRESS;

	return status;
}


static status_t
usb_raw_read(void *cookie, off_t position, void *buffer, size_t *length)
{
	TRACE((DRIVER_NAME": read()\n"));
	*length = 0;
	return B_OK;
}


static status_t
usb_raw_write(void *cookie, off_t position, const void *buffer,
	size_t *length)
{
	TRACE((DRIVER_NAME": write()\n"));
	*length = 0;
	return B_OK;
}


//
//#pragma mark - driver module API
//


static float
usb_raw_supports_device(dk_node *parent)
{
	TRACE((DRIVER_NAME": supports_device()\n"));
	char bus[64];

	if (sDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1;

	if (strcmp(bus, "usb"))
		return 0.0;

	// usb_raw matches all USB devices with low priority
	return 0.1;
}


static void usb_raw_device_removed(void*);

static dk_device_ops sDeviceOps = {
	usb_raw_open,
	usb_raw_close,
	usb_raw_free,
	usb_raw_read,
	usb_raw_write,
	NULL,	// io
	usb_raw_ioctl,
	NULL,	// select
	NULL,	// deselect
	usb_raw_device_removed,
};


static status_t
usb_raw_attach(dk_node *node, void **cookie)
{
	TRACE((DRIVER_NAME": attach()\n"));

	raw_device *device = (raw_device *)malloc(sizeof(raw_device));
	if (device == NULL)
		return B_NO_MEMORY;

	memset(device, 0, sizeof(raw_device));
	device->node = node;

	if (sDeviceKeeper->get_property_uint32(node, USB_DEVICE_ID_ITEM,
			&device->device, true) != B_OK) {
		free(device);
		return B_ERROR;
	}

	mutex_init(&device->lock, "usb_raw device lock");
	device->notify = create_sem(0, "usb_raw callback notify");
	if (device->notify < B_OK) {
		mutex_destroy(&device->lock);
		free(device);
		return B_NO_MORE_SEMS;
	}

	// generate a device name
	char deviceName[64];
	memcpy(deviceName, &device->device, sizeof(usb_device));
	if (gUSBModule->usb_ioctl('DNAM', deviceName, sizeof(deviceName))
			>= B_OK) {
		snprintf(device->name, sizeof(device->name), "%s/%s",
			DEVICE_NAME, deviceName);
	} else {
		snprintf(device->name, sizeof(device->name), "%s/%08" B_PRIx32,
			DEVICE_NAME, device->device);
	}

	// publish device in devfs
	status_t result = sDeviceKeeper->publish_device(device->node,
		device->name, &sDeviceOps);
	if (result != B_OK) {
		delete_sem(device->notify);
		mutex_destroy(&device->lock);
		free(device);
		return result;
	}

	*cookie = device;
	return B_OK;
}


static void
usb_raw_detach(void *_cookie)
{
	TRACE((DRIVER_NAME": detach()\n"));
	raw_device *device = (raw_device *)_cookie;
	if (device == NULL)
		return;

	if (device->name[0] != '\0')
		sDeviceKeeper->unpublish_device(device->node, device->name);

	mutex_lock(&device->lock);
	mutex_destroy(&device->lock);
	delete_sem(device->notify);
	free(device);
}


static void
usb_raw_device_removed(void *_cookie)
{
	TRACE((DRIVER_NAME": device_removed()\n"));
	raw_device *device = (raw_device *)_cookie;
	device->device = 0;
}


//
//#pragma mark -
//


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&sDeviceKeeper },
	{ B_USB_MODULE_NAME, (module_info **)&gUSBModule },
	{ NULL }
};

static const dk_match_rule sUsbRawMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "usb"),
	DK_MATCH_END
};

static const dk_match_dict sUsbRawMatchDict = {
	sUsbRawMatchRules,
	0
};


static dk_driver_info sUsbRawDriver = {
	.info = {
		USB_RAW_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.match   = &sUsbRawMatchDict,
	.probe   = usb_raw_supports_device,
	.attach  = usb_raw_attach,
	.detach  = usb_raw_detach,
	.ops     = &sDeviceOps,
};

module_info *modules[] = {
	(module_info *)&sUsbRawDriver,
	NULL
};
