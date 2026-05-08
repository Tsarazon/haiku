/*
 * Copyright 2013, 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioPrivate.h"


dk_keeper_info* gDeviceKeeper = NULL;


//	#pragma mark - virtio_device_interface forwarders


static status_t
virtio_negotiate_features(virtio_device _device, uint64 supported,
	uint64* negotiated, const char* (*get_feature_name)(uint64))
{
	CALLED();
	VirtioDevice* device = (VirtioDevice*)_device;
	return device->NegotiateFeatures(supported, negotiated, get_feature_name);
}


static status_t
virtio_clear_feature(virtio_device _device, uint64 feature)
{
	CALLED();
	VirtioDevice* device = (VirtioDevice*)_device;
	return device->ClearFeature(feature);
}


static status_t
virtio_read_device_config(virtio_device _device, uint8 offset, void* buffer,
	size_t bufferSize)
{
	CALLED();
	VirtioDevice* device = (VirtioDevice*)_device;
	return device->ReadDeviceConfig(offset, buffer, bufferSize);
}


static status_t
virtio_write_device_config(virtio_device _device, uint8 offset,
	const void* buffer, size_t bufferSize)
{
	CALLED();
	VirtioDevice* device = (VirtioDevice*)_device;
	return device->WriteDeviceConfig(offset, buffer, bufferSize);
}


static status_t
virtio_alloc_queues(virtio_device _device, size_t count, virtio_queue* queues,
	uint16* requestedSizes)
{
	CALLED();
	VirtioDevice* device = (VirtioDevice*)_device;
	return device->AllocateQueues(count, queues, requestedSizes);
}


static void
virtio_free_queues(virtio_device _device)
{
	CALLED();
	VirtioDevice* device = (VirtioDevice*)_device;
	device->FreeQueues();
}


static status_t
virtio_setup_interrupt(virtio_device _device, virtio_intr_func config_handler,
	void* driverCookie)
{
	CALLED();
	VirtioDevice* device = (VirtioDevice*)_device;
	return device->SetupInterrupt(config_handler, driverCookie);
}


static status_t
virtio_free_interrupts(virtio_device _device)
{
	CALLED();
	VirtioDevice* device = (VirtioDevice*)_device;
	return device->FreeInterrupts();
}


static status_t
virtio_queue_setup_interrupt(virtio_queue _queue, virtio_callback_func handler,
	void* cookie)
{
	CALLED();
	VirtioQueue* queue = (VirtioQueue*)_queue;
	return queue->SetupInterrupt(handler, cookie);
}


static status_t
virtio_queue_request_v(virtio_queue _queue, const physical_entry* vector,
	size_t readVectorCount, size_t writtenVectorCount, void* cookie)
{
	CALLED();
	VirtioQueue* queue = (VirtioQueue*)_queue;
	return queue->QueueRequest(vector, readVectorCount, writtenVectorCount,
		cookie);
}


static status_t
virtio_queue_request(virtio_queue _queue, const physical_entry* readEntry,
	const physical_entry* writtenEntry, void* cookie)
{
	physical_entry entries[2];
	if (readEntry != NULL) {
		entries[0] = *readEntry;
		if (writtenEntry != NULL)
			entries[1] = *writtenEntry;
	} else if (writtenEntry != NULL)
		entries[0] = *writtenEntry;

	return virtio_queue_request_v(_queue, entries, readEntry != NULL ? 1 : 0,
		writtenEntry != NULL ? 1 : 0, cookie);
}


static bool
virtio_queue_is_full(virtio_queue _queue)
{
	VirtioQueue* queue = (VirtioQueue*)_queue;
	return queue->IsFull();
}


static bool
virtio_queue_is_empty(virtio_queue _queue)
{
	VirtioQueue* queue = (VirtioQueue*)_queue;
	return queue->IsEmpty();
}


static uint16
virtio_queue_size(virtio_queue _queue)
{
	VirtioQueue* queue = (VirtioQueue*)_queue;
	return queue->Size();
}


static bool
virtio_queue_dequeue(virtio_queue _queue, void** _cookie, uint32* _usedLength)
{
	VirtioQueue* queue = (VirtioQueue*)_queue;
	return queue->Dequeue(_cookie, _usedLength);
}


//	#pragma mark - virtio_for_controller_interface forwarders


static status_t
virtio_queue_interrupt_handler(virtio_sim sim, uint16 queue)
{
	VirtioDevice* device = (VirtioDevice*)sim;
	return device->QueueInterrupt(queue);
}


static status_t
virtio_config_interrupt_handler(virtio_sim sim)
{
	VirtioDevice* device = (VirtioDevice*)sim;
	return device->ConfigInterrupt();
}


//	#pragma mark - ops table


static virtio_device_interface sVirtioDeviceOps = {
	virtio_negotiate_features,
	virtio_clear_feature,
	virtio_read_device_config,
	virtio_write_device_config,
	virtio_alloc_queues,
	virtio_free_queues,
	virtio_setup_interrupt,
	virtio_free_interrupts,
	virtio_queue_setup_interrupt,
	virtio_queue_request,
	virtio_queue_request_v,
	virtio_queue_is_full,
	virtio_queue_is_empty,
	virtio_queue_size,
	virtio_queue_dequeue
};


//	#pragma mark - attach/detach for virtio_device module


static status_t
virtio_device_init(dk_node* node, void** _device)
{
	CALLED();
	VirtioDevice* device = new(std::nothrow) VirtioDevice(node);
	if (device == NULL)
		return B_NO_MEMORY;

	status_t result = device->InitCheck();
	if (result != B_OK) {
		ERROR("failed to set up virtio device object: %s\n", strerror(result));
		delete device;
		return result;
	}

	// Publish the peripheral-facing interface so virtio drivers
	// (virtio_input, virtio_gpu, virtio_net, virtio_balloon, ...)
	// can retrieve it via get_interface(VIRTIO_DEVICE_INTERFACE_NAME).
	result = gDeviceKeeper->publish_interface(node,
		VIRTIO_DEVICE_INTERFACE_NAME, &sVirtioDeviceOps);
	if (result != B_OK) {
		delete device;
		return result;
	}

	*_device = device;
	return B_OK;
}


static void
virtio_device_uninit(void* _device)
{
	CALLED();
	VirtioDevice* device = (VirtioDevice*)_device;
	delete device;
}


//	#pragma mark - module table


static status_t
virtio_controller_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
	}
	return B_ERROR;
}


// Plain kernel module (not a dk_driver) retrieved by host transports
// (virtio_pci, virtio_mmio) via get_module(VIRTIO_FOR_CONTROLLER_MODULE_NAME).
// They call queue_interrupt_handler / config_interrupt_handler when
// hardware interrupts arrive.
static virtio_for_controller_interface sVirtioForControllerModule = {
	{
		VIRTIO_FOR_CONTROLLER_MODULE_NAME,
		0,
		virtio_controller_std_ops
	},
	virtio_queue_interrupt_handler,
	virtio_config_interrupt_handler
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{}
};


// Peripheral-facing bus manager. Attached on each virtio device node
// (registered by virtio_pci/virtio_mmio under the transport node).
// Publishes virtio_device_interface via publish_interface so peripheral
// drivers (virtio_input, virtio_gpu, ...) retrieve it by walk-up.
static dk_driver_info sVirtioDeviceDriver = {
	.info		= { VIRTIO_DEVICE_MODULE_NAME, 0, NULL },
	.attach		= virtio_device_init,
	.detach		= virtio_device_uninit,
	.node_flags	= KOSM_FIND_MULTIPLE_CHILDREN,
};


extern dk_driver_info sVirtioBalloonDriver;


module_info* modules[] = {
	(module_info*)&sVirtioDeviceDriver,
	(module_info*)&sVirtioForControllerModule,
	(module_info*)&sVirtioBalloonDriver,
	NULL
};
