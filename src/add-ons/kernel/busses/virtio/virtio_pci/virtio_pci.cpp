/*
 * Copyright 2013, 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <bus/PCI.h>
#include <SupportDefs.h>

#include <cpu.h>
#include <kernel.h>
#include <virtio.h>

#include "virtio_pci.h"


//#define TRACE_VIRTIO
#ifdef TRACE_VIRTIO
#	define TRACE(x...) dprintf("\33[33mvirtio_pci:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[33mvirtio_pci:\33[0m " x)
#define ERROR(x...)			dprintf("\33[33mvirtio_pci:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define VIRTIO_PCI_DEVICE_MODULE_NAME "busses/virtio/virtio_pci/dk_driver_v1"
#define VIRTIO_PCI_SIM_MODULE_NAME "busses/virtio/virtio_pci/device/dk_driver_v1"

#define VIRTIO_PCI_CONTROLLER_TYPE_NAME "virtio pci controller"

typedef enum {
	VIRTIO_IRQ_LEGACY,
	VIRTIO_IRQ_MSI_X_SHARED,
	VIRTIO_IRQ_MSI_X,
} virtio_irq_type;

typedef struct {
	virtio_sim sim;
	uint16 queue;
} virtio_pci_queue_cookie;

typedef struct {
	pci_device_ops* pci;
	pci_device* device;
	bool virtio1;
	addr_t base_addr;
	area_id registersArea[6];
	addr_t commonCfgAddr;
	addr_t isrAddr;
	addr_t notifyAddr;
	uint32 notifyOffsetMultiplier;
	uint32 irq;
	virtio_irq_type irq_type;
	virtio_sim sim;
	uint16 queue_count;
	addr_t* notifyOffsets;

	dk_node* node;
	pci_info info;

	virtio_pci_queue_cookie *cookies;
} virtio_pci_sim_info;


dk_keeper_info* gDeviceKeeper;
virtio_for_controller_interface* gVirtio;


static status_t
virtio_pci_find_capability(virtio_pci_sim_info* bus, uint8 cfgType,
	void* buffer, size_t size)
{
	uint8 capabilityOffset;
	if (bus->pci->find_pci_capability(bus->device, PCI_cap_id_vendspec, &capabilityOffset) != B_OK)
		return B_ENTRY_NOT_FOUND;

	if (size < sizeof(virtio_pci_cap))
		return B_RESULT_NOT_REPRESENTABLE;
	union regs {
		uint32 reg[8];
		struct virtio_pci_cap capability;
	} * v = (union regs*)buffer;

	while (capabilityOffset != 0) {
		for (int i = 0; i < 4; i++) {
			v->reg[i] = bus->pci->read_pci_config(bus->device, capabilityOffset + i * 4, 4);
		}
		if (v->capability.cfg_type == cfgType)
			break;
		capabilityOffset = v->capability.cap_next;
	}
	if (capabilityOffset == 0)
		return B_ENTRY_NOT_FOUND;

	if (v->capability.length > sizeof(virtio_pci_cap)) {
		size_t length = min_c(ROUNDUP(v->capability.length, sizeof(uint32)), size);
		for (size_t i = 4; i < length / sizeof(uint32); i++)
			v->reg[i] = bus->pci->read_pci_config(bus->device, capabilityOffset + i * 4, 4);
	}
	return B_OK;
}


static int32
virtio_pci_interrupt(void *data)
{
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)data;
	uint8 isr;
	if (bus->virtio1) {
		uint8* isrAddr = (uint8*)bus->isrAddr;
		isr = *isrAddr;
	} else {
		isr = bus->pci->read_io_8(bus->device,
			bus->base_addr + VIRTIO_PCI_ISR);
	}
	if (isr == 0)
		return B_UNHANDLED_INTERRUPT;

	if (isr & VIRTIO_PCI_ISR_CONFIG)
		gVirtio->config_interrupt_handler(bus->sim);

	if (isr & VIRTIO_PCI_ISR_INTR)
		gVirtio->queue_interrupt_handler(bus->sim, INT16_MAX);

	return B_HANDLED_INTERRUPT;
}


static int32
virtio_pci_config_interrupt(void *data)
{
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)data;
	gVirtio->config_interrupt_handler(bus->sim);

	return B_HANDLED_INTERRUPT;
}


static int32
virtio_pci_queue_interrupt(void *data)
{
	virtio_pci_queue_cookie* cookie = (virtio_pci_queue_cookie*)data;
	gVirtio->queue_interrupt_handler(cookie->sim, cookie->queue);

	return B_HANDLED_INTERRUPT;
}


static int32
virtio_pci_queues_interrupt(void *data)
{
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)data;
	gVirtio->queue_interrupt_handler(bus->sim, INT16_MAX);

	return B_HANDLED_INTERRUPT;
}


static status_t
virtio_pci_setup_msix_interrupts(virtio_pci_sim_info* bus)
{
	CALLED();
	uint8 irq = 0; // first irq slot
	if (bus->virtio1) {
		volatile uint16 *msixVector = (uint16*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, config_msix_vector));
		*msixVector = irq;
	} else {
		bus->pci->write_io_16(bus->device, bus->base_addr
			+ VIRTIO_MSI_CONFIG_VECTOR, irq);
		if (bus->pci->read_io_16(bus->device, bus->base_addr
			+ VIRTIO_MSI_CONFIG_VECTOR) == VIRTIO_MSI_NO_VECTOR) {
			ERROR("msix config vector incorrect\n");
			return B_BAD_VALUE;
		}
	}
	if (bus->irq_type == VIRTIO_IRQ_MSI_X || bus->irq_type == VIRTIO_IRQ_MSI_X_SHARED)
		irq++;

	for (uint16 queue = 0; queue < bus->queue_count; queue++) {
		if (bus->virtio1) {
			volatile uint16* queueSelect = (uint16*)(bus->commonCfgAddr
				+ offsetof(struct virtio_pci_common_cfg, queue_select));
			*queueSelect = queue;
			volatile uint16* msixVector = (uint16*)(bus->commonCfgAddr
				+ offsetof(struct virtio_pci_common_cfg, queue_msix_vector));
			*msixVector = irq;
		} else {
			bus->pci->write_io_16(bus->device, bus->base_addr
				+ VIRTIO_PCI_QUEUE_SEL, queue);
			bus->pci->write_io_16(bus->device, bus->base_addr
				+ VIRTIO_MSI_QUEUE_VECTOR, irq);

			if (bus->pci->read_io_16(bus->device, bus->base_addr
				+ VIRTIO_MSI_QUEUE_VECTOR) == VIRTIO_MSI_NO_VECTOR) {
				ERROR("msix queue vector incorrect\n");
				return B_BAD_VALUE;
			}
		}
		if (bus->irq_type == VIRTIO_IRQ_MSI_X)
			irq++;
	}

	return B_OK;
}


static void
set_sim(void* cookie, virtio_sim sim)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;
	bus->sim = sim;
}


static status_t
read_host_features(void* cookie, uint64 *features)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;

	TRACE("read_host_features() %p node %p pci %p device %p\n", bus,
		bus->node, bus->pci, bus->device);

	if (bus->virtio1) {
		volatile uint32 *select = (uint32*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, device_feature_select));
		volatile uint32 *feature = (uint32*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, device_feature));
		*select = 0;
		*features = *feature;
		*select = 1;
		*features |= ((uint64)*feature << 32) ;
		TRACE("read_host_features() %" B_PRIx64 "\n", *features);
	} else {
		*features = bus->pci->read_io_32(bus->device,
			bus->base_addr + VIRTIO_PCI_HOST_FEATURES);
	}
	return B_OK;
}


static status_t
write_guest_features(void* cookie, uint64 features)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;
	if (bus->virtio1) {
		volatile uint32 *select = (uint32*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, device_feature_select));
		volatile uint32 *feature = (uint32*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, device_feature));
		*select = 0;
		*feature = features & 0xffffffff;
		*select = 1;
		*feature = (features >> 32) ;
	} else {
		bus->pci->write_io_32(bus->device, bus->base_addr
			+ VIRTIO_PCI_GUEST_FEATURES, features);
	}
	return B_OK;
}


static uint8
get_status(void* cookie)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;
	if (bus->virtio1) {
		uint8 *addr = (uint8*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, device_status));
		return *addr;
	} else {
		return bus->pci->read_io_8(bus->device, bus->base_addr + VIRTIO_PCI_STATUS);
	}
}


static void
set_status(void* cookie, uint8 status)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;
	if (bus->virtio1) {
		volatile uint8 *addr = (uint8*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, device_status));
		if (status == 0) {
			*addr = 0;
			while (*addr != 0)
				cpu_pause();
		} else {
			uint8 old = 0;
			if (status != 0)
				old = *addr;
			*addr = status | old;
		}
	} else {
		if (status == 0) {
			bus->pci->write_io_8(bus->device, bus->base_addr + VIRTIO_PCI_STATUS, 0);
			while (bus->pci->read_io_8(bus->device, bus->base_addr + VIRTIO_PCI_STATUS) != 0)
				cpu_pause();
		} else {
			uint8 old = bus->pci->read_io_8(bus->device, bus->base_addr + VIRTIO_PCI_STATUS);
			bus->pci->write_io_8(bus->device, bus->base_addr + VIRTIO_PCI_STATUS, status | old);
		}
	}
}


static status_t
read_device_config(void* cookie, uint8 _offset, void* _buffer,
	size_t bufferSize)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;

	addr_t offset = bus->base_addr + _offset;
	if (!bus->virtio1)
		offset += VIRTIO_PCI_CONFIG(bus);
	uint8* buffer = (uint8*)_buffer;
	while (bufferSize > 0) {
		uint8 size = 4;
		if (bufferSize == 1) {
			size = 1;
			if (bus->virtio1) {
				*buffer = *(uint8*)offset;
			} else {
				*buffer = bus->pci->read_io_8(bus->device, offset);
			}
		} else if (bufferSize <= 3) {
			size = 2;
			if (bus->virtio1) {
				*(uint16*)buffer = *(uint16*)offset;
			} else {
				*(uint16*)buffer = bus->pci->read_io_16(bus->device, offset);
			}
		} else {
			if (bus->virtio1) {
				*(uint32*)buffer = *(uint32*)offset;
			} else {
				*(uint32*)buffer = bus->pci->read_io_32(bus->device,
					offset);
			}
		}
		buffer += size;
		bufferSize -= size;
		offset += size;
	}

	return B_OK;
}


static status_t
write_device_config(void* cookie, uint8 _offset, const void* _buffer,
	size_t bufferSize)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;

	addr_t offset = bus->base_addr + _offset;
	if (!bus->virtio1)
		offset += VIRTIO_PCI_CONFIG(bus);
	const uint8* buffer = (const uint8*)_buffer;
	while (bufferSize > 0) {
		uint8 size = 4;
		if (bufferSize == 1) {
			size = 1;
			if (bus->virtio1) {
				*(uint8*)offset = *buffer;
			} else {
				bus->pci->write_io_8(bus->device, offset, *buffer);
			}
		} else if (bufferSize <= 3) {
			size = 2;
			if (bus->virtio1) {
				*(uint16*)offset = *(uint16*)buffer;
			} else {
				bus->pci->write_io_16(bus->device, offset, *(const uint16*)buffer);
			}
		} else {
			if (bus->virtio1) {
				*(uint32*)offset = *(uint32*)buffer;
			} else {
				bus->pci->write_io_32(bus->device, offset, *(const uint32*)buffer);
			}
		}
		buffer += size;
		bufferSize -= size;
		offset += size;
	}
	return B_OK;
}


static uint16
get_queue_ring_size(void* cookie, uint16 queue)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;
	if (bus->virtio1) {
		volatile uint16* queueSelect = (uint16*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, queue_select));
		*queueSelect = queue;
		volatile uint16* ringSize = (volatile uint16*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, queue_size));
		return *ringSize;
	} else {
		bus->pci->write_io_16(bus->device, bus->base_addr + VIRTIO_PCI_QUEUE_SEL,
			queue);
		return bus->pci->read_io_16(bus->device, bus->base_addr
			+ VIRTIO_PCI_QUEUE_NUM);
	}
}


static status_t
setup_queue(void* cookie, uint16 queue, phys_addr_t phy, phys_addr_t phyAvail,
	phys_addr_t phyUsed)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;
	if (bus->virtio1) {
		if (queue >= bus->queue_count)
			return B_BAD_VALUE;
		volatile uint16* queueSelect = (uint16*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, queue_select));
		*queueSelect = queue;

		volatile uint64* queueDesc = (volatile uint64*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, queue_desc));
		*queueDesc = phy;
		volatile uint64* queueAvail = (volatile uint64*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, queue_avail));
		*queueAvail = phyAvail;
		volatile uint64* queueUsed = (volatile uint64*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, queue_used));
		*queueUsed = phyUsed;
		volatile uint16* queueEnable = (volatile uint16*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, queue_enable));
		*queueEnable = 1;

		volatile uint16* queueNotifyOffset = (volatile uint16*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, queue_notify_off));
		bus->notifyOffsets[queue] = *queueNotifyOffset * bus->notifyOffsetMultiplier;
	} else {
		bus->pci->write_io_16(bus->device, bus->base_addr + VIRTIO_PCI_QUEUE_SEL, queue);
		bus->pci->write_io_32(bus->device, bus->base_addr + VIRTIO_PCI_QUEUE_PFN,
			(uint32)phy >> VIRTIO_PCI_QUEUE_ADDR_SHIFT);
	}
	return B_OK;
}


static status_t
setup_interrupt(void* cookie, uint16 queueCount)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;
	pci_info *pciInfo = &bus->info;

	bus->queue_count = queueCount;

	// try MSI-X
	uint32 msixCount = bus->pci->get_msix_count(bus->device);
	if (msixCount >= 2) {
		uint32 vectorCount = queueCount + 1;
		if (msixCount >= vectorCount) {
			uint32 vector;
			bus->cookies = new(std::nothrow)
				virtio_pci_queue_cookie[queueCount];
			if (bus->cookies != NULL
				&& bus->pci->configure_msix(bus->device, vectorCount,
					&vector) == B_OK
				&& bus->pci->enable_msix(bus->device) == B_OK) {
				TRACE_ALWAYS("using MSI-X count %" B_PRIu32 " starting at %" B_PRIu32 "\n",
					vectorCount, vector);
				bus->irq = vector;
				bus->irq_type = VIRTIO_IRQ_MSI_X;
			} else {
				ERROR("couldn't use MSI-X\n");
			}
		} else {
			uint32 vector;
			if (bus->pci->configure_msix(bus->device, 2, &vector) == B_OK
				&& bus->pci->enable_msix(bus->device) == B_OK) {
				TRACE_ALWAYS("using MSI-X vector shared %" B_PRIu32 "\n", vector);
				bus->irq = vector;
				bus->irq_type = VIRTIO_IRQ_MSI_X_SHARED;
			} else {
				ERROR("couldn't use MSI-X SHARED\n");
			}
		}
	}

	if (bus->irq_type == VIRTIO_IRQ_LEGACY) {
		bus->irq = pciInfo->u.h0.interrupt_line;
		if (bus->irq == 0xff)
			bus->irq = 0;
		TRACE_ALWAYS("using legacy interrupt %" B_PRIu32 "\n", bus->irq);
	}
	if (bus->irq == 0) {
		ERROR("PCI IRQ not assigned\n");
		delete bus;
		return B_ERROR;
	}

	if (bus->irq_type != VIRTIO_IRQ_LEGACY) {
		status_t status = install_io_interrupt_handler(bus->irq,
			virtio_pci_config_interrupt, bus, 0);
		if (status != B_OK) {
			ERROR("can't install interrupt handler\n");
			return status;
		}
		int32 irq = bus->irq + 1;
		if (bus->irq_type == VIRTIO_IRQ_MSI_X) {
			for (int32 queue = 0; queue < queueCount; queue++, irq++) {
				bus->cookies[queue].sim = bus->sim;
				bus->cookies[queue].queue = queue;
				status_t status = install_io_interrupt_handler(irq,
					virtio_pci_queue_interrupt, &bus->cookies[queue], 0);
				if (status != B_OK) {
					ERROR("can't install interrupt handler\n");
					return status;
				}
			}
		} else {
			status_t status = install_io_interrupt_handler(irq,
				virtio_pci_queues_interrupt, bus, 0);
			if (status != B_OK) {
				ERROR("can't install interrupt handler\n");
				return status;
			}
		}
		TRACE("setup_interrupt() installed MSI-X interrupt handlers\n");
		virtio_pci_setup_msix_interrupts(bus);
	} else {
		// setup interrupt handler
		status_t status = install_io_interrupt_handler(bus->irq,
			virtio_pci_interrupt, bus, 0);
		if (status != B_OK) {
			ERROR("can't install interrupt handler\n");
			return status;
		}
		TRACE("setup_interrupt() installed legacy interrupt handler\n");
	}

	return B_OK;
}


static status_t
free_interrupt(void* cookie)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;

	if (bus->irq_type != VIRTIO_IRQ_LEGACY) {
		remove_io_interrupt_handler(bus->irq, virtio_pci_config_interrupt, bus);
		int32 irq = bus->irq + 1;
		if (bus->irq_type == VIRTIO_IRQ_MSI_X) {
			for (int32 queue = 0; queue < bus->queue_count; queue++, irq++)
				remove_io_interrupt_handler(irq, virtio_pci_queue_interrupt, &bus->cookies[queue]);
			delete[] bus->cookies;
			bus->cookies = NULL;
		} else {
			remove_io_interrupt_handler(irq, virtio_pci_queues_interrupt, bus);
		}
		bus->pci->disable_msi(bus->device);
		bus->pci->unconfigure_msi(bus->device);
	} else
		remove_io_interrupt_handler(bus->irq, virtio_pci_interrupt, bus);

	return B_OK;
}


static void
notify_queue(void* cookie, uint16 queue)
{
	CALLED();
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)cookie;
	if (queue >= bus->queue_count)
		return;
	if (bus->virtio1) {
		volatile uint16* notifyAddr = (volatile uint16*)(bus->notifyAddr + bus->notifyOffsets[queue]);
		*notifyAddr = queue;
	} else {
		bus->pci->write_io_16(bus->device, bus->base_addr
			+ VIRTIO_PCI_QUEUE_NOTIFY, queue);
	}
}


//	#pragma mark -


// ops struct defined before init_bus so publish_interface can reference it
static virtio_sim_interface gVirtioPCISimOps = {
	set_sim,
	read_host_features,
	write_guest_features,
	get_status,
	set_status,
	read_device_config,
	write_device_config,
	get_queue_ring_size,
	setup_queue,
	setup_interrupt,
	free_interrupt,
	notify_queue
};


static status_t
init_bus(dk_node* node, void** bus_cookie)
{
	CALLED();

	virtio_pci_sim_info* bus = new(std::nothrow) virtio_pci_sim_info;
	if (bus == NULL)
		return B_NO_MEMORY;
	memset(bus, 0, sizeof(virtio_pci_sim_info));

	pci_device_ops* pci;
	pci_device* device;
	status_t status = gDeviceKeeper->get_interface(node,
		PCI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&pci, (void**)&device);
	if (status != B_OK) {
		ERROR("init_bus: can't get PCI interface: %s\n", strerror(status));
		delete bus;
		return status;
	}

	bus->node = node;
	bus->pci = pci;
	bus->device = device;
	bus->cookies = NULL;
	bus->irq_type = VIRTIO_IRQ_LEGACY;

	pci_info *pciInfo = &bus->info;
	pci->get_pci_info(device, pciInfo);

	bus->virtio1 = pciInfo->device_id >= VIRTIO_PCI_DEVICEID_MODERN_MIN
		&& pciInfo->device_id <= VIRTIO_PCI_DEVICEID_MODERN_MAX;

	if (bus->virtio1) {
		struct virtio_pci_cap common, isr, deviceCap;
		struct virtio_pci_notify_cap notify;
		bool deviceCfgFound = false;
		if (virtio_pci_find_capability(bus, VIRTIO_PCI_CAP_COMMON_CFG, &common,
			sizeof(common)) != B_OK) {
			delete bus;
			return B_DEVICE_NOT_FOUND;
		}
		if (virtio_pci_find_capability(bus, VIRTIO_PCI_CAP_ISR_CFG, &isr,
			sizeof(isr)) != B_OK) {
			delete bus;
			return B_DEVICE_NOT_FOUND;
		}
		if (virtio_pci_find_capability(bus, VIRTIO_PCI_CAP_DEVICE_CFG, &deviceCap,
			sizeof(deviceCap)) != B_OK) {
			memset(&deviceCap, 0, sizeof(deviceCap));
		} else {
			deviceCfgFound = true;
		}
		if (virtio_pci_find_capability(bus, VIRTIO_PCI_CAP_NOTIFY_CFG, &notify,
			sizeof(notify)) != B_OK) {
			delete bus;
			return B_DEVICE_NOT_FOUND;
		}

		size_t bars[6] = {0};
		if (common.length > 0)
			bars[common.bar] = common.offset + common.length;
		if (isr.length > 0)
			bars[isr.bar] = max_c(bars[isr.bar], isr.offset + isr.length);
		if (notify.cap.length > 0) {
			bars[notify.cap.bar] = max_c(bars[notify.cap.bar], notify.cap.offset
				+ notify.cap.length);
		}
		if (deviceCfgFound && deviceCap.length > 0)
			bars[deviceCap.bar] = max_c(bars[deviceCap.bar], deviceCap.offset + deviceCap.length);

		size_t index = 0;
		addr_t registers[B_COUNT_OF(bars)] = {0};
		for (size_t i = 0; i < B_COUNT_OF(bars); i++, index++) {
			if (bars[index] == 0) {
				bus->registersArea[index] = -1;
				continue;
			}
			phys_addr_t barAddr = pciInfo->u.h0.base_registers[i];
			size_t barSize = pciInfo->u.h0.base_register_sizes[i];
			if ((pciInfo->u.h0.base_register_flags[i] & PCI_address_type) == PCI_address_type_64
				&& i < (B_COUNT_OF(bars) - 1)) {
				i++;
				barAddr |= (uint64)pciInfo->u.h0.base_registers[i] << 32;
				barSize |= (uint64)pciInfo->u.h0.base_register_sizes[i] << 32;
			}

			bus->registersArea[index] = map_physical_memory("Virtio PCI memory mapped registers",
				barAddr, barSize, B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
				(void**)&registers[index]);
		}

		bus->commonCfgAddr = registers[common.bar] + common.offset;
		bus->isrAddr = registers[isr.bar] + isr.offset;
		bus->notifyAddr = registers[notify.cap.bar] + notify.cap.offset;
		bus->notifyOffsetMultiplier = notify.notify_off_multiplier;
		if (deviceCfgFound)
			bus->base_addr = registers[deviceCap.bar] + deviceCap.offset;

		// enable bus master and memory
		uint16 pcicmd = pci->read_pci_config(device, PCI_command, 2);
		pcicmd &= ~(PCI_command_int_disable | PCI_command_io);
		pci->write_pci_config(device, PCI_command, 2,
			pcicmd | PCI_command_master | PCI_command_memory);

		volatile uint16 *queueCount = (uint16*)(bus->commonCfgAddr
			+ offsetof(struct virtio_pci_common_cfg, num_queues));
		bus->queue_count = *queueCount;
		bus->notifyOffsets = new addr_t[bus->queue_count];

	} else {
		// legacy
		bus->base_addr = pciInfo->u.h0.base_registers[0];

		uint16 pcicmd = pci->read_pci_config(device, PCI_command, 2);
		pcicmd &= ~(PCI_command_memory | PCI_command_int_disable);
		pcicmd |= PCI_command_master | PCI_command_io;
		pci->write_pci_config(device, PCI_command, 2, pcicmd);
	}

	set_status(bus, VIRTIO_CONFIG_STATUS_RESET);
	set_status(bus, VIRTIO_CONFIG_STATUS_ACK);

	TRACE("init_bus() %p node %p pci %p device %p\n", bus, node,
		bus->pci, bus->device);

	*bus_cookie = bus;

	// publish transport interface so virtio bus manager can find it via get_interface
	gDeviceKeeper->publish_interface(node, VIRTIO_HOST_INTERFACE_NAME,
		&gVirtioPCISimOps);

	// register child node for the virtio bus manager; it will attach, retrieve
	// VIRTIO_HOST_INTERFACE_NAME from us and publish VIRTIO_DEVICE_INTERFACE_NAME
	// for peripheral drivers (virtio_input, virtio_gpu, ...).
	uint16 virtioType = 0, vringAlign = 0;
	uint8 virtioVersion = 0;
	gDeviceKeeper->get_property_uint16(node, VIRTIO_DEVICE_TYPE_ITEM,
		&virtioType, false);
	gDeviceKeeper->get_property_uint16(node, VIRTIO_VRING_ALIGNMENT_ITEM,
		&vringAlign, false);
	gDeviceKeeper->get_property_uint8(node, VIRTIO_VERSION_ITEM,
		&virtioVersion, false);

	dk_property childAttrs[] = {
		DK_PROP_STRING(KOSM_DEVICE_BUS, "virtio"),
		DK_PROP_UINT16(VIRTIO_DEVICE_TYPE_ITEM, virtioType),
		DK_PROP_UINT16(VIRTIO_VRING_ALIGNMENT_ITEM, vringAlign),
		DK_PROP_UINT8(VIRTIO_VERSION_ITEM, virtioVersion),
		DK_PROP_END
	};
	status_t childStatus = gDeviceKeeper->register_node(node,
		VIRTIO_DEVICE_MODULE_NAME, childAttrs, NULL, NULL);
	if (childStatus != B_OK) {
		ERROR("register_node(virtio/device) failed: %s\n",
			strerror(childStatus));
	}

	return B_OK;
}


static void
uninit_bus(void* bus_cookie)
{
	virtio_pci_sim_info* bus = (virtio_pci_sim_info*)bus_cookie;
	if (bus->irq_type != VIRTIO_IRQ_LEGACY) {
		int32 irq = bus->irq + 1;
		if (bus->irq_type == VIRTIO_IRQ_MSI_X) {
			for (int32 queue = 0; queue < bus->queue_count; queue++, irq++)
				remove_io_interrupt_handler(irq, virtio_pci_queue_interrupt, &bus->cookies[queue]);
			delete[] bus->cookies;
		} else {
			remove_io_interrupt_handler(irq, virtio_pci_queues_interrupt, bus);
		}
		remove_io_interrupt_handler(bus->irq, virtio_pci_config_interrupt, bus);

		bus->pci->disable_msi(bus->device);
		bus->pci->unconfigure_msi(bus->device);
	} else
		remove_io_interrupt_handler(bus->irq, virtio_pci_interrupt, bus);

	if (bus->virtio1) {
		for (int i = 0; i < 6; i++) {
			if (bus->registersArea[i] >= 0)
				delete_area(bus->registersArea[i]);
			else
				break;
		}
		delete[] bus->notifyOffsets;
	}

	delete bus;
}


//	#pragma mark -


static status_t
init_device(dk_node* node, void** device_cookie)
{
	CALLED();
	*device_cookie = node;

	pci_device_ops* pci;
	pci_device* device;
	status_t status = gDeviceKeeper->get_interface(node,
		PCI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&pci, (void**)&device);
	if (status != B_OK)
		return status;

	uint16 pciSubDeviceId = pci->read_pci_config(device, PCI_subsystem_id, 2);
	uint16 pciDeviceId = pci->read_pci_config(device, PCI_device_id, 2);

	// reject legacy devices with non-zero PCI revision
	uint8 pciRevision = pci->read_pci_config(device, PCI_revision, 1);
	if (pciDeviceId >= VIRTIO_PCI_DEVICEID_MIN
		&& pciDeviceId <= VIRTIO_PCI_DEVICEID_LEGACY_MAX
		&& pciRevision != 0) {
		return B_NOT_SUPPORTED;
	}

	uint8 virtioVersion = 0;
	uint16 virtioDeviceId = pciSubDeviceId;
	if (pciDeviceId >= VIRTIO_PCI_DEVICEID_MODERN_MIN) {
		virtioVersion = 1;
		virtioDeviceId = pciDeviceId - VIRTIO_PCI_DEVICEID_MODERN_MIN;
	}

	char prettyName[25];
	sprintf(prettyName, "Virtio Device %" B_PRIu16, virtioDeviceId);

	dk_property attrs[] = {
		DK_PROP_STRING(KOSM_LABEL, prettyName),
		DK_PROP_UINT16(VIRTIO_DEVICE_TYPE_ITEM, virtioDeviceId),
		DK_PROP_UINT16(VIRTIO_VRING_ALIGNMENT_ITEM, VIRTIO_PCI_VRING_ALIGN),
		DK_PROP_UINT8(VIRTIO_VERSION_ITEM, virtioVersion),
		DK_PROP_END
	};

	return gDeviceKeeper->register_node(node, VIRTIO_PCI_SIM_MODULE_NAME,
		attrs, NULL, NULL);
}


// Match rules: bus="pci", vendor=0x1AF4.
static const dk_match_rule kVirtioPCIMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "pci"),
	DK_MATCH_UINT16(KOSM_DEVICE_VENDOR_ID, VIRTIO_PCI_VENDORID),
	DK_MATCH_END
};

static const dk_match_dict kVirtioPCIMatch = {
	kVirtioPCIMatchRules,
	0
};


static float
supports_device(dk_node* node)
{
	CALLED();
	uint16 deviceID;

	// bus and vendor already filtered by match rules
	if (gDeviceKeeper->get_property_uint16(node, KOSM_DEVICE_ID, &deviceID,
			false) != B_OK) {
		return -1.0f;
	}

	if (deviceID < VIRTIO_PCI_DEVICEID_MIN
		|| deviceID > VIRTIO_PCI_DEVICEID_MODERN_MAX) {
		return 0.0f;
	}

	TRACE("Virtio device found! device 0x%04x\n", deviceID);
	return 0.8f;
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ VIRTIO_FOR_CONTROLLER_MODULE_NAME, (module_info**)&gVirtio },
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{}
};


static dk_driver_info gVirtioPCISimDriver = {
	.info = { VIRTIO_PCI_SIM_MODULE_NAME, 0, NULL },
	.attach = init_bus,
	.detach = uninit_bus,
};


static dk_driver_info sVirtioDevice = {
	.info  = { VIRTIO_PCI_DEVICE_MODULE_NAME, 0, NULL },
	.match = &kVirtioPCIMatch,
	.probe = supports_device,
	.attach = init_device,
};

module_info* modules[] = {
	(module_info*)&sVirtioDevice,
	(module_info*)&gVirtioPCISimDriver,
	NULL
};
