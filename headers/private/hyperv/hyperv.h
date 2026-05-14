/*
 * Copyright 2026 John Davis. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _HYPERV_H_
#define _HYPERV_H_


#include <device_keeper.h>
#include <KernelExport.h>

#include <hyperv_spec.h>

#define HYPERV_BUS_NAME						"hyperv"

// Device attributes for the VMBus device
#define HYPERV_CHANNEL_ID_ITEM				"hyperv/channel"
#define HYPERV_DEVICE_TYPE_STRING_ITEM		"hyperv/type_string"
#define HYPERV_INSTANCE_ID_ITEM				"hyperv/instance"
#define HYPERV_MMIO_SIZE_ITEM				"hyperv/mmio_size"

// Pretty-name format used as KOSM_LABEL value for each VMBus channel.
// VMBus.cpp formats it with the channel id ("Hyper-V Channel %u").
#define HYPERV_PRETTYNAME_VMBUS_DEVICE_FMT	"Hyper-V Channel %u"


typedef void* hyperv_device;
typedef void (*hyperv_device_callback)(void* data);


// Bus interface name for publish_interface/get_interface. Published by
// the VMBus device bus manager on each VMBus channel node; consumed by
// Hyper-V device drivers via gDeviceKeeper->get_interface(node,
// HYPERV_DEVICE_INTERFACE_NAME, ...).
#define HYPERV_DEVICE_INTERFACE_NAME	"interface/hyperv/device/v1"


// Interface between the VMBus device driver, and the VMBus device bus manager
typedef struct hyperv_device_interface {
	uint32 (*get_bus_version)(hyperv_device cookie);
	status_t (*get_reference_counter)(hyperv_device cookie, uint64* _count);
	status_t (*open)(hyperv_device cookie, uint32 txLength, uint32 rxLength,
		hyperv_device_callback callback, void* callbackData);
	void (*close)(hyperv_device cookie);
	status_t (*read_packet)(hyperv_device cookie, void* buffer, uint32* bufferLength,
		uint32* _headerLength, uint32* _dataLength);
	status_t (*write_packet)(hyperv_device cookie, uint16 type, const void* buffer,
		uint32 length, bool responseRequired, uint64 transactionID);
	status_t (*write_gpa_packet)(hyperv_device cookie, uint32 rangeCount,
		const vmbus_gpa_range* rangesList, uint32 rangesLength, const void* buffer,
		uint32 length, bool responseRequired, uint64 transactionID);
	status_t (*allocate_gpadl)(hyperv_device cookie, uint32 length, void** _buffer,
		uint32* _gpadl);
	status_t (*free_gpadl)(hyperv_device cookie, uint32 gpadl);
} hyperv_device_interface;


#endif // _HYPERV_H_
