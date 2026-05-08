/*
 * Copyright 2013, 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2026, KosmOS Project. DeviceKeeper migration.
 * Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <ByteOrder.h>
#include <KernelExport.h>
#include <device_keeper.h>

#include <drivers/ACPI.h>
#include <drivers/bus/FDT.h>

#include <debug.h>
#include <AutoDeleterDrivers.h>
#include <AutoDeleterOS.h>

#include <acpi.h>

#include <virtio.h>
#include <virtio_defs.h>


#define VIRTIO_MMIO_DEVICE_MODULE_NAME "busses/virtio/virtio_mmio/dk_driver_v1"

//#define TRACE_VIRTIO_MMIO
#ifdef TRACE_VIRTIO_MMIO
#	define TRACE(x...) dprintf("virtio_mmio: " x)
#else
#	define TRACE(x...) ;
#endif
#define ERROR(x...)	dprintf("virtio_mmio: " x)


dk_keeper_info* gDeviceKeeper;
virtio_for_controller_interface* gVirtio;


// Per-transport bus state. One instance per virtio-mmio controller.
struct virtio_mmio_sim_info {
	dk_node*			node;
	area_id				regsArea;
	volatile VirtioRegs*	regs;
	uint32				irq;
	virtio_sim			sim;
	bool				interruptInstalled;
};


//	#pragma mark - interrupt handler


static int32
virtio_mmio_interrupt(void* data)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)data;
	uint32 isr = bus->regs->interruptStatus;
	if (isr == 0)
		return B_UNHANDLED_INTERRUPT;

	if ((isr & kVirtioIntQueue) != 0)
		gVirtio->queue_interrupt_handler(bus->sim, INT16_MAX);

	if ((isr & kVirtioIntConfig) != 0)
		gVirtio->config_interrupt_handler(bus->sim);

	bus->regs->interruptAck = isr;
	return B_HANDLED_INTERRUPT;
}


//	#pragma mark - virtio_sim_interface ops


static void
set_sim(void* cookie, virtio_sim sim)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	bus->sim = sim;
}


static status_t
read_host_features(void* cookie, uint64* features)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	*features = bus->regs->deviceFeatures;
	return B_OK;
}


static status_t
write_guest_features(void* cookie, uint64 features)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	bus->regs->driverFeatures = (uint32)features;
	if (bus->regs->version == 1)
		bus->regs->guestPageSize = B_PAGE_SIZE;
	return B_OK;
}


static uint8
get_status(void* cookie)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	return (uint8)bus->regs->status;
}


static void
set_status(void* cookie, uint8 status)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	if (status == 0) {
		bus->regs->status = 0;
	} else {
		bus->regs->status |= status;
	}
}


static status_t
read_device_config(void* cookie, uint8 offset, void* buffer, size_t bufferSize)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;

	// TODO: ARM seems to support only 32-bit aligned MMIO access.
	vuint8* src = &bus->regs->config[offset];
	uint8* dst = (uint8*)buffer;
	while (bufferSize > 0) {
		uint8 size = 4;
		if (bufferSize == 1) {
			size = 1;
			*dst = *src;
		} else if (bufferSize <= 3) {
			size = 2;
			*(uint16*)dst = *(vuint16*)src;
		} else
			*(uint32*)dst = *(vuint32*)src;

		dst += size;
		bufferSize -= size;
		src += size;
	}

	return B_OK;
}


static status_t
write_device_config(void* cookie, uint8 offset, const void* buffer,
	size_t bufferSize)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;

	const uint8* src = (const uint8*)buffer;
	vuint8* dst = &bus->regs->config[offset];
	while (bufferSize > 0) {
		uint8 size = 4;
		if (bufferSize == 1) {
			size = 1;
			*dst = *src;
		} else if (bufferSize <= 3) {
			size = 2;
			*(vuint16*)dst = *(const uint16*)src;
		} else
			*(vuint32*)dst = *(const uint32*)src;

		src += size;
		bufferSize -= size;
		dst += size;
	}
	return B_OK;
}


static uint16
get_queue_ring_size(void* cookie, uint16 queue)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	bus->regs->queueSel = queue;
	return (uint16)bus->regs->queueNumMax;
}


static status_t
setup_queue(void* cookie, uint16 queue, phys_addr_t phy, phys_addr_t phyAvail,
	phys_addr_t phyUsed)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	bus->regs->queueSel = queue;

	uint16 queueNum = (uint16)bus->regs->queueNumMax;
	bus->regs->queueNum = queueNum;

	if (bus->regs->version >= 2) {
		bus->regs->queueDescLow  = (uint32)phy;
		bus->regs->queueDescHi   = (uint32)(phy >> 32);
		bus->regs->queueAvailLow = (uint32)phyAvail;
		bus->regs->queueAvailHi  = (uint32)(phyAvail >> 32);
		bus->regs->queueUsedLow  = (uint32)phyUsed;
		bus->regs->queueUsedHi   = (uint32)(phyUsed >> 32);
		bus->regs->queueReady    = 1;
	} else {
		// Legacy: PFN points at the descriptor area, avail/used follow
		// contiguously with B_PAGE_SIZE alignment (established by the
		// virtio bus manager's VirtioQueue layout).
		bus->regs->queueAlign = B_PAGE_SIZE;
		bus->regs->queuePfn   = (uint32)(phy / B_PAGE_SIZE);
	}
	return B_OK;
}


static status_t
setup_interrupt(void* cookie, uint16 queueCount)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	if (bus->interruptInstalled)
		return B_OK;

	status_t status = install_io_interrupt_handler(bus->irq,
		virtio_mmio_interrupt, bus, 0);
	if (status != B_OK) {
		ERROR("install_io_interrupt_handler failed: %s\n", strerror(status));
		return status;
	}
	bus->interruptInstalled = true;
	return B_OK;
}


static status_t
free_interrupt(void* cookie)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	if (!bus->interruptInstalled)
		return B_OK;

	remove_io_interrupt_handler(bus->irq, virtio_mmio_interrupt, bus);
	bus->interruptInstalled = false;
	return B_OK;
}


static void
notify_queue(void* cookie, uint16 queue)
{
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	bus->regs->queueNotify = queue;
}


// Transport ops published on the virtio-mmio controller node. Defined
// before init_bus so publish_interface can reference it without a
// forward declaration.
static virtio_sim_interface gVirtioMmioSimOps = {
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
	notify_queue,
};


//	#pragma mark - probe / ACPI CRS


static float
virtio_mmio_supports_device(dk_node* node)
{
	TRACE("supports_device(%p)\n", node);

	// detect virtio MMIO from FDT compatible string
	char compatible[128];
	if (gDeviceKeeper->get_property_string(node, KOSM_DEVICE_COMPATIBLE,
			compatible, sizeof(compatible), NULL, false) == B_OK) {
		if (strcmp(compatible, "virtio,mmio") == 0)
			return 1.0f;
		return 0.0f;
	}

	// detect virtio MMIO from ACPI HID
	char hid[64];
	if (gDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_HID,
			hid, sizeof(hid), NULL, false) == B_OK) {
		if (strcmp(hid, "LNRO0005") == 0)
			return 1.0f;
		return 0.0f;
	}

	return -1.0f;
}


struct virtio_mmio_crs {
	uint64	addr_base;
	uint32	addr_len;
	uint32	irq;
};


static acpi_status
virtio_crs_parse_callback(acpi_resource* res, void* context)
{
	virtio_mmio_crs* crs = (virtio_mmio_crs*)context;
	switch (res->type) {
		case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
			crs->addr_base = res->data.fixed_memory32.address;
			crs->addr_len = res->data.fixed_memory32.address_length;
			break;
		case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
			if (res->data.extended_irq.interrupt_count > 0)
				crs->irq = res->data.extended_irq.interrupts[0];
			break;
	}
	return B_OK;
}


//	#pragma mark - attach / detach


static status_t
virtio_mmio_read_resources(dk_node* node, uint64& regs, uint64& regsLen,
	uint64& interrupt)
{
	regs = 0;
	regsLen = 0;
	interrupt = 0;

	if (gDeviceKeeper->get_property_uint64(node,
			KOSM_DEVICE_REG_BASE, &regs, false) == B_OK) {
		// FDT path: all three properties set by the FDT bus manager.
		gDeviceKeeper->get_property_uint64(node, KOSM_DEVICE_REG_SIZE,
			&regsLen, false);
		gDeviceKeeper->get_property_uint64(node, KOSM_DEVICE_IRQ,
			&interrupt, false);
		return B_OK;
	}

	// ACPI path (LNRO0005): resources come from _CRS, not node properties.
	char acpiPath[256] = {};
	if (gDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_PATH,
			acpiPath, sizeof(acpiPath), NULL, false) != B_OK) {
		ERROR("init_bus: no reg base or ACPI path\n");
		return B_ERROR;
	}

	acpi_module_info* acpi = NULL;
	if (get_module(B_ACPI_MODULE_NAME, (module_info**)&acpi) != B_OK) {
		ERROR("init_bus: can't get ACPI module\n");
		return B_ERROR;
	}

	acpi_handle handle = NULL;
	status_t s = acpi->get_handle(NULL, acpiPath, &handle);
	if (s != B_OK) {
		put_module(B_ACPI_MODULE_NAME);
		ERROR("init_bus: ACPI get_handle failed\n");
		return B_ERROR;
	}

	virtio_mmio_crs crs = {};
	acpi->walk_resources(handle, (char*)"_CRS",
		virtio_crs_parse_callback, &crs);
	put_module(B_ACPI_MODULE_NAME);

	regs = crs.addr_base;
	regsLen = crs.addr_len;
	interrupt = crs.irq;

	if (regs == 0 || regsLen == 0) {
		ERROR("init_bus: ACPI _CRS gave no memory resource\n");
		return B_ERROR;
	}

	return B_OK;
}


static status_t
virtio_mmio_init_bus(dk_node* node, void** cookie)
{
	TRACE("init_bus(%p)\n", node);

	uint64 regs = 0, regsLen = 0, interrupt = 0;
	status_t s = virtio_mmio_read_resources(node, regs, regsLen, interrupt);
	if (s != B_OK)
		return s;

	TRACE("  regs: (0x%" B_PRIx64 ", 0x%" B_PRIx64 ")\n", regs, regsLen);
	TRACE("  interrupt: 0x%" B_PRIx64 "\n", interrupt);

	virtio_mmio_sim_info* bus = new(std::nothrow) virtio_mmio_sim_info;
	if (bus == NULL)
		return B_NO_MEMORY;
	memset(bus, 0, sizeof(*bus));
	bus->node = node;
	bus->irq = (uint32)interrupt;
	bus->regsArea = -1;

	volatile VirtioRegs* mappedRegs = NULL;
	bus->regsArea = map_physical_memory("Virtio MMIO",
		regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&mappedRegs);
	if (bus->regsArea < 0) {
		ERROR("can't map regs: %s\n", strerror(bus->regsArea));
		status_t err = bus->regsArea;
		delete bus;
		return err;
	}
	bus->regs = mappedRegs;

	if (bus->regs->signature != kVirtioSignature) {
		ERROR("bad signature: 0x%08" B_PRIx32 ", should be 0x%08" B_PRIx32 "\n",
			bus->regs->signature, (uint32)kVirtioSignature);
		delete_area(bus->regsArea);
		delete bus;
		return B_ERROR;
	}

	uint16 virtioType = (uint16)bus->regs->deviceId;
	if (virtioType == 0) {
		// No backend attached — nothing to expose.
		TRACE("virtio_mmio: no device on this transport\n");
		delete_area(bus->regsArea);
		delete bus;
		return B_ERROR;
	}

	uint32 version = bus->regs->version;
	TRACE("  version: 0x%08" B_PRIx32 "\n", version);
	TRACE("  deviceId: 0x%08" B_PRIx32 "\n", bus->regs->deviceId);
	TRACE("  vendorId: 0x%08" B_PRIx32 "\n", bus->regs->vendorId);

	// Reset the device and acknowledge. The virtio bus manager will
	// push it to DRIVER state via set_status during attach.
	bus->regs->status = 0;
	bus->regs->status = kVirtioConfigSAcknowledge;

	*cookie = bus;

	// Publish transport interface so the virtio bus manager can find it
	// via get_interface walk-up from the child device node we register
	// below.
	gDeviceKeeper->publish_interface(node, VIRTIO_HOST_INTERFACE_NAME,
		&gVirtioMmioSimOps);

	// Register a virtio device child node; the virtio bus manager
	// (VIRTIO_DEVICE_MODULE_NAME) attaches to it, looks up our sim
	// interface via KOSM_INTERFACE_ANCESTORS, and publishes
	// VIRTIO_DEVICE_INTERFACE_NAME for peripheral drivers.
	//
	// virtio version byte: 0 for legacy spec, 1 for 1.0+. In MMIO, the
	// register "version" field is 1 for legacy, 2+ for modern.
	uint8 virtioVersion = (version >= 2) ? 1 : 0;
	uint16 vringAlignment = B_PAGE_SIZE;

	char prettyName[32];
	snprintf(prettyName, sizeof(prettyName), "Virtio MMIO Device %u",
		(unsigned)virtioType);

	dk_property childAttrs[] = {
		DK_PROP_STRING(KOSM_LABEL, prettyName),
		DK_PROP_STRING(KOSM_DEVICE_BUS, "virtio"),
		DK_PROP_UINT16(VIRTIO_DEVICE_TYPE_ITEM, virtioType),
		DK_PROP_UINT16(VIRTIO_VRING_ALIGNMENT_ITEM, vringAlignment),
		DK_PROP_UINT8(VIRTIO_VERSION_ITEM, virtioVersion),
		DK_PROP_END
	};
	status_t childStatus = gDeviceKeeper->register_node(node,
		VIRTIO_DEVICE_MODULE_NAME, childAttrs, NULL, NULL);
	if (childStatus != B_OK) {
		ERROR("register_node(virtio/device) failed: %s\n",
			strerror(childStatus));
		// non-fatal: transport is up, peripherals just won't attach
	}

	return B_OK;
}


static void
virtio_mmio_uninit_bus(void* cookie)
{
	TRACE("uninit_bus(%p)\n", cookie);
	virtio_mmio_sim_info* bus = (virtio_mmio_sim_info*)cookie;
	if (bus == NULL)
		return;

	if (bus->interruptInstalled) {
		remove_io_interrupt_handler(bus->irq, virtio_mmio_interrupt, bus);
		bus->interruptInstalled = false;
	}

	if (bus->regs != NULL)
		bus->regs->status = 0;

	if (bus->regsArea >= 0)
		delete_area(bus->regsArea);

	delete bus;
}


//	#pragma mark - module boilerplate


module_dependency module_dependencies[] = {
	{ VIRTIO_FOR_CONTROLLER_MODULE_NAME, (module_info**)&gVirtio },
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ NULL }
};


static dk_driver_info sVirtioMmioDriver = {
	.info   = { VIRTIO_MMIO_DEVICE_MODULE_NAME, 0, NULL },
	.probe  = virtio_mmio_supports_device,
	.attach = virtio_mmio_init_bus,
	.detach = virtio_mmio_uninit_bus,
};


module_info* modules[] = {
	(module_info*)&sVirtioMmioDriver,
	NULL
};
