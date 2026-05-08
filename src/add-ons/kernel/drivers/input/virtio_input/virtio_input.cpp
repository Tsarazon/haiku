/*
 * Copyright 2013, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2021, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <virtio.h>
#include <virtio_defs.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <new>

#include <kernel.h>
#include <fs/devfs.h>

#include <device_keeper.h>
#include <virtio_input_driver.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <debug.h>


//#define TRACE_VIRTIO_INPUT
#ifdef TRACE_VIRTIO_INPUT
#	define TRACE(x...) dprintf("virtio_input: " x)
#else
#	define TRACE(x...) ;
#endif
#define ERROR(x...)			dprintf("virtio_input: " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

#define VIRTIO_INPUT_DRIVER_MODULE_NAME "drivers/input/virtio_input/dk_driver_v1"
#define VIRTIO_INPUT_DEVICE_ID_GENERATOR "virtio_input/device_id"


struct Packet {
	VirtioInputPacket data;
	int32 next;
};


struct VirtioInputDevice {
	dk_node* node;
	::virtio_device virtio_device;
	virtio_device_interface* virtio;
	::virtio_queue virtio_queue;

	uint64 features;

	uint32 packetCnt;
	int32 freePackets;
	int32 readyPackets, lastReadyPacket;
	AreaDeleter packetArea;
	phys_addr_t physAdr;
	Packet* packets;

	SemDeleter sem_cb;

	int32 id;
	char publishedPath[64];
};


struct VirtioInputHandle {
	VirtioInputDevice*		info;
};


static dk_keeper_info* sDeviceKeeper;

#ifdef TRACE_VIRTIO_INPUT
static void
WriteInputPacket(const VirtioInputPacket &pkt)
{
	switch (pkt.type) {
		case kVirtioInputEvSyn:
			TRACE("syn");
			break;
		case kVirtioInputEvKey:
			TRACE("key, ");
			switch (pkt.code) {
				case kVirtioInputBtnLeft:
					TRACE("left");
					break;
				case kVirtioInputBtnRight:
					TRACE("middle");
					break;
				case kVirtioInputBtnMiddle:
					TRACE("right");
					break;
				case kVirtioInputBtnGearDown:
					TRACE("gearDown");
					break;
				case kVirtioInputBtnGearUp:
					TRACE("gearUp");
					break;
				default:
					TRACE("%d", pkt.code);
			}
			break;
		case kVirtioInputEvRel:
			TRACE("rel, ");
			switch (pkt.code) {
				case kVirtioInputRelX:
					TRACE("relX");
					break;
				case kVirtioInputRelY:
					TRACE("relY");
					break;
				case kVirtioInputRelZ:
					TRACE("relZ");
					break;
				case kVirtioInputRelWheel:
					TRACE("relWheel");
					break;
				default:
					TRACE("%d", pkt.code);
			}
			break;
		case kVirtioInputEvAbs:
			TRACE("abs, ");
			switch (pkt.code) {
				case kVirtioInputAbsX:
					TRACE("absX");
					break;
				case kVirtioInputAbsY:
					TRACE("absY");
					break;
				case kVirtioInputAbsZ:
					TRACE("absZ");
					break;
				default:
					TRACE("%d", pkt.code);
			}
			break;
		case kVirtioInputEvRep:
			TRACE("rep");
			break;
		default:
			TRACE("?(%d)", pkt.type);
	}
	switch (pkt.type) {
		case kVirtioInputEvSyn:
			break;
		case kVirtioInputEvKey:
			TRACE(", ");
			if (pkt.value == 0) {
				TRACE("up");
			} else if (pkt.value == 1) {
				TRACE("down");
			} else {
				TRACE("%" B_PRId32, pkt.value);
			}
			break;
		default:
			TRACE(", %" B_PRId32, pkt.value);
	}
}
#endif

static void
InitPackets(VirtioInputDevice* dev, uint32 count)
{
	TRACE("InitPackets(%p, %" B_PRIu32 ")\n", dev, count);
	size_t size = ROUNDUP(sizeof(Packet)*count, B_PAGE_SIZE);

	dev->packetArea.SetTo(create_area("VirtIO input packets",
		(void**)&dev->packets, B_ANY_KERNEL_ADDRESS, size,
		B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA));
	if (!dev->packetArea.IsSet()) {
		ERROR("Unable to set packet area!");
		return;
	}

	physical_entry pe;
	if (get_memory_map(dev->packets, size, &pe, 1) < B_OK) {
		ERROR("Unable to get memory map for input packets!");
		return;
	}
	dev->physAdr = pe.address;
	memset(dev->packets, 0, size);
	dprintf("  size: 0x%" B_PRIxSIZE "\n", size);
	dprintf("  virt: %p\n", dev->packets);
	dprintf("  phys: %p\n", (void*)dev->physAdr);

	dev->packetCnt = count;

	dev->freePackets = 0;
	for (uint32 i = 0; i < dev->packetCnt - 1; i++)
		dev->packets[i].next = i + 1;
	dev->packets[dev->packetCnt - 1].next = -1;

	dev->readyPackets = -1;
	dev->lastReadyPacket = -1;
}


static const physical_entry
PacketPhysEntry(VirtioInputDevice* dev, Packet* pkt)
{
	physical_entry pe;
	pe.address = dev->physAdr + (uint8*)pkt - (uint8*)dev->packets;
	pe.size = sizeof(VirtioInputPacket);
	return pe;
}


static void
ScheduleReadyPacket(VirtioInputDevice* dev, Packet* pkt)
{
	if (dev->readyPackets < 0)
		dev->readyPackets = pkt - dev->packets;
	else
		dev->packets[dev->lastReadyPacket].next = pkt - dev->packets;

	dev->lastReadyPacket = pkt - dev->packets;
}


static Packet*
ConsumeReadyPacket(VirtioInputDevice* dev)
{
	if (dev->readyPackets < 0)
		return NULL;
	Packet* pkt = &dev->packets[dev->readyPackets];
	dev->readyPackets = pkt->next;
	if (dev->readyPackets < 0)
		dev->lastReadyPacket = -1;
	return pkt;
}


static void
virtio_input_callback(void* driverCookie, void* cookie)
{
	CALLED();
	VirtioInputDevice* dev = (VirtioInputDevice*)cookie;

	Packet* pkt;
	while (dev->virtio->queue_dequeue(dev->virtio_queue, (void**)&pkt, NULL)) {
#ifdef TRACE_VIRTIO_INPUT
		TRACE("%" B_PRIdSSIZE ": ", pkt - dev->packets);
		WriteInputPacket(pkt->data);
		TRACE("\n");
#endif
		ScheduleReadyPacket(dev, pkt);
		release_sem_etc(dev->sem_cb.Get(), 1, B_DO_NOT_RESCHEDULE);
	}
}


//	#pragma mark - dk_device_ops


static status_t
virtio_input_open(void* deviceCookie, const char* path, int openMode,
	void** _cookie)
{
	CALLED();
	// deviceCookie is the VirtioInputDevice* set as node cookie in attach().
	VirtioInputDevice* info = (VirtioInputDevice*)deviceCookie;
	if (info == NULL)
		return ENODEV;

	ObjectDeleter<VirtioInputHandle>
		handle(new(std::nothrow) (VirtioInputHandle));

	if (!handle.IsSet())
		return B_NO_MEMORY;

	handle->info = info;

	*_cookie = handle.Detach();
	return B_OK;
}


static status_t
virtio_input_close(void* cookie)
{
	CALLED();
	return B_OK;
}


static status_t
virtio_input_free(void* cookie)
{
	CALLED();
	ObjectDeleter<VirtioInputHandle> handle((VirtioInputHandle*)cookie);
	return B_OK;
}


static status_t
virtio_input_read(void* cookie, off_t pos, void* buffer, size_t* _length)
{
	return B_ERROR;
}


static status_t
virtio_input_write(void* cookie, off_t pos, const void* buffer,
	size_t* _length)
{
	*_length = 0;
	return B_ERROR;
}


static status_t
virtio_input_ioctl(void* cookie, uint32 op, void* buffer, size_t length)
{
	CALLED();

	VirtioInputHandle* handle = (VirtioInputHandle*)cookie;
	VirtioInputDevice* info = handle->info;
	(void)info;

	TRACE("ioctl(op = %" B_PRIu32 ")\n", op);

	switch (op) {
		case virtioInputRead: {
			TRACE("virtioInputRead\n");
			if (buffer == NULL || length < sizeof(VirtioInputPacket))
				return B_BAD_VALUE;

			status_t res = acquire_sem_etc(info->sem_cb.Get(), 1, B_CAN_INTERRUPT,
				B_INFINITE_TIMEOUT);
			if (res < B_OK)
				return res;

			Packet* pkt = ConsumeReadyPacket(info);
			TRACE("  pkt: %" B_PRIdSSIZE "\n", pkt - info->packets);

			physical_entry pe = PacketPhysEntry(info, pkt);
			info->virtio->queue_request(info->virtio_queue, NULL, &pe, pkt);

			res = user_memcpy(buffer, pkt, sizeof(Packet));
			if (res < B_OK)
				return res;

			return B_OK;
		}
	}

	return B_DEV_INVALID_IOCTL;
}


static dk_device_ops sVirtioInputDeviceOps = {
	virtio_input_open,
	virtio_input_close,
	virtio_input_free,
	virtio_input_read,
	virtio_input_write,
	NULL,	// io
	virtio_input_ioctl,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


//	#pragma mark - dk_driver_info


static const dk_match_rule sVirtioInputMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "virtio"),
	DK_MATCH_END
};

static const dk_match_dict sVirtioInputMatchDict = {
	sVirtioInputMatchRules,
	0
};


static float
virtio_input_probe(dk_node* node)
{
	CALLED();

	// check whether it's really a VirtIO input device
	uint16 deviceType = 0;
	if (sDeviceKeeper->get_property_uint16(node, VIRTIO_DEVICE_TYPE_ITEM,
			&deviceType, true) != B_OK || deviceType != kVirtioDevInput)
		return 0.0f;

	TRACE("Virtio input device found!\n");
	return 0.6f;
}


static status_t
virtio_input_attach(dk_node* node, void** _cookie)
{
	CALLED();

	ObjectDeleter<VirtioInputDevice>
		info(new(std::nothrow) VirtioInputDevice());
	if (!info.IsSet())
		return B_NO_MEMORY;

	memset((void*)info.Get(), 0, sizeof(*info.Get()));
	info->id = -1;

	info->sem_cb.SetTo(create_sem(0, "virtio_input_cb"));
	if (!info->sem_cb.IsSet())
		return info->sem_cb.Get();

	info->node = node;

	// Walk up to the virtio bus manager ancestor for the peripheral-facing
	// interface and our virtio_device cookie.
	status_t status = sDeviceKeeper->get_interface(node,
		VIRTIO_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&info->virtio, (void**)&info->virtio_device);
	if (status != B_OK) {
		ERROR("get_interface(virtio device) failed: %s\n", strerror(status));
		return status;
	}
	if (info->virtio == NULL || info->virtio_device == NULL) {
		ERROR("virtio bus manager returned NULL ops/cookie\n");
		return B_ERROR;
	}

	info->virtio->negotiate_features(info->virtio_device, 0,
		&info->features, NULL);

	InitPackets(info.Get(), 8);
	if (!info->packetArea.IsSet())
		return B_NO_MEMORY;

	status = info->virtio->alloc_queues(info->virtio_device, 1,
		&info->virtio_queue, NULL);
	if (status != B_OK) {
		ERROR("queue allocation failed (%s)\n", strerror(status));
		return status;
	}
	TRACE("  queue: %p\n", info->virtio_queue);

	status = info->virtio->queue_setup_interrupt(info->virtio_queue,
		virtio_input_callback, info.Get());
	if (status < B_OK)
		return status;

	for (uint32 i = 0; i < info->packetCnt; i++) {
		Packet* pkt = &info->packets[i];
		physical_entry pe = PacketPhysEntry(info.Get(), pkt);
		info->virtio->queue_request(info->virtio_queue, NULL, &pe, pkt);
	}

	// Publish the devfs entry.
	info->id = sDeviceKeeper->create_id(VIRTIO_INPUT_DEVICE_ID_GENERATOR);
	if (info->id < 0)
		return info->id;

	snprintf(info->publishedPath, sizeof(info->publishedPath),
		"input/virtio/%" B_PRId32 "/raw", info->id);

	status = sDeviceKeeper->publish_device(node, info->publishedPath,
		&sVirtioInputDeviceOps);
	if (status < B_OK) {
		ERROR("publish_device(%s) failed: %s\n", info->publishedPath,
			strerror(status));
		sDeviceKeeper->free_id(VIRTIO_INPUT_DEVICE_ID_GENERATOR, info->id);
		info->id = -1;
		info->publishedPath[0] = '\0';
		return status;
	}

	*_cookie = info.Detach();
	return B_OK;
}


static void
virtio_input_detach(void* _cookie)
{
	CALLED();
	VirtioInputDevice* info = (VirtioInputDevice*)_cookie;
	if (info == NULL)
		return;

	if (info->publishedPath[0] != '\0')
		sDeviceKeeper->unpublish_device(info->node, info->publishedPath);
	if (info->id >= 0)
		sDeviceKeeper->free_id(VIRTIO_INPUT_DEVICE_ID_GENERATOR, info->id);

	// Free any allocated virtio queues / interrupts before the cookie
	// is destroyed — the virtio device interface pointer is still valid
	// here (detach runs without DK locks).
	if (info->virtio != NULL && info->virtio_device != NULL) {
		info->virtio->free_interrupts(info->virtio_device);
		info->virtio->free_queues(info->virtio_device);
	}

	// AreaDeleter and SemDeleter clean up via destructors.
	delete info;
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&sDeviceKeeper },
	{ NULL }
};


static dk_driver_info sVirtioInputDriver = {
	.info = {
		VIRTIO_INPUT_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.match   = &sVirtioInputMatchDict,
	.probe   = virtio_input_probe,
	.attach  = virtio_input_attach,
	.detach  = virtio_input_detach,
	.ops     = &sVirtioInputDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sVirtioInputDriver,
	NULL
};
