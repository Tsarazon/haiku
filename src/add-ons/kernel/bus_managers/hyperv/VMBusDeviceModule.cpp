/*
 * Copyright 2026 John Davis. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "VMBusDevicePrivate.h"


static status_t
vmbus_device_init(dk_node* node, void** _driverCookie)
{
	CALLED();

	VMBusDevice* device = new(std::nothrow) VMBusDevice(node);
	if (device == NULL) {
		ERROR("Unable to allocate VMBus device object\n");
		return B_NO_MEMORY;
	}

	status_t status = device->InitCheck();
	if (status != B_OK) {
		ERROR("Failed to set up VMBus device object\n");
		delete device;
		return status;
	}
	TRACE("VMBus device object created\n");

	// Publish the per-device interface on this node so Hyper-V peripheral
	// drivers (heartbeat, time sync, KVP, ...) can retrieve it via
	// get_interface walk-up.
	status = gDeviceKeeper->publish_interface(node,
		HYPERV_DEVICE_INTERFACE_NAME, &gVMBusDeviceOps);
	if (status != B_OK) {
		ERROR("Failed to publish VMBus device interface\n");
		delete device;
		return status;
	}

	*_driverCookie = device;
	return B_OK;
}


static void
vmbus_device_uninit(void* driverCookie)
{
	CALLED();
	VMBusDevice* device = reinterpret_cast<VMBusDevice*>(driverCookie);
	delete device;
}


static uint32
vmbus_device_get_bus_version(hyperv_device cookie)
{
	CALLED();
	VMBusDevice* device = reinterpret_cast<VMBusDevice*>(cookie);
	return device->GetBusVersion();
}


static status_t
vmbus_device_get_reference_counter(hyperv_device cookie, uint64* _count)
{
	CALLED();
	VMBusDevice* device = reinterpret_cast<VMBusDevice*>(cookie);
	return device->GetReferenceCounter(_count);
}


static status_t
vmbus_device_open(hyperv_device cookie, uint32 txLength, uint32 rxLength,
	hyperv_device_callback callback, void* callbackData)
{
	CALLED();
	VMBusDevice* device = reinterpret_cast<VMBusDevice*>(cookie);
	return device->Open(txLength, rxLength, callback, callbackData);
}


static void
vmbus_device_close(hyperv_device cookie)
{
	CALLED();
	VMBusDevice* device = reinterpret_cast<VMBusDevice*>(cookie);
	device->Close();
}


static status_t
vmbus_device_read_packet(hyperv_device cookie, void* buffer, uint32* bufferLength,
	uint32* _headerLength, uint32* _dataLength)
{
	VMBusDevice* device = reinterpret_cast<VMBusDevice*>(cookie);
	return device->ReadPacket(buffer, bufferLength, _headerLength, _dataLength);
}


static status_t
vmbus_device_write_packet(hyperv_device cookie, uint16 type, const void* buffer, uint32 length,
	bool responseRequired, uint64 transactionID)
{
	VMBusDevice* device = reinterpret_cast<VMBusDevice*>(cookie);
	return device->WritePacket(type, buffer, length, responseRequired, transactionID);
}


static status_t
vmbus_device_write_gpa_packet(hyperv_device cookie, uint32 rangeCount,
	const vmbus_gpa_range* rangesList, uint32 rangesLength, const void* buffer, uint32 length,
	bool responseRequired, uint64 transactionID)
{
	VMBusDevice* device = reinterpret_cast<VMBusDevice*>(cookie);
	return device->WriteGPAPacket(rangeCount, rangesList, rangesLength, buffer, length,
		responseRequired, transactionID);
}


static status_t
vmbus_device_allocate_gpadl(hyperv_device cookie, uint32 length, void** _buffer, uint32* _gpadl)
{
	VMBusDevice* device = reinterpret_cast<VMBusDevice*>(cookie);
	return device->AllocateGPADL(length, _buffer, _gpadl);
}


static status_t
vmbus_device_free_gpadl(hyperv_device cookie, uint32 gpadl)
{
	VMBusDevice* device = reinterpret_cast<VMBusDevice*>(cookie);
	return device->FreeGPADL(gpadl);
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			break;
	}

	return B_ERROR;
}


hyperv_device_interface gVMBusDeviceOps = {
	vmbus_device_get_bus_version,
	vmbus_device_get_reference_counter,
	vmbus_device_open,
	vmbus_device_close,
	vmbus_device_read_packet,
	vmbus_device_write_packet,
	vmbus_device_write_gpa_packet,
	vmbus_device_allocate_gpadl,
	vmbus_device_free_gpadl
};

dk_driver_info gVMBusDeviceDriver = {
	.info       = { HYPERV_DEVICE_MODULE_NAME, 0, std_ops },
	.attach     = vmbus_device_init,
	.detach     = vmbus_device_uninit,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};
