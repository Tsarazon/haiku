/*
 * Copyright 2014, Ithamar R. Adema <ithamar@upgrade-android.com>
 * Copyright 2015-2022, Haiku, Inc. All rights reserved.
 * Copyright 2026, KosmOS Project. DeviceKeeper migration.
 * Distributed under the terms of the MIT License.
 */


#include <drivers/bus/FDT.h>
#include <KernelExport.h>
#include <util/kernel_cpp.h>
#include <util/Vector.h>
#include <device_keeper.h>

#include <AutoDeleter.h>
#include <AutoDeleterDrivers.h>
#include <HashMap.h>
#include <debug.h>

extern "C" {
#include <libfdt_env.h>
#include <fdt.h>
#include <libfdt.h>
};


//#define TRACE_FDT
#ifdef TRACE_FDT
#define TRACE(x...) dprintf(x)
#else
#define TRACE(x...)
#endif


#define GIC_INTERRUPT_CELL_TYPE     0
#define GIC_INTERRUPT_CELL_ID       1
#define GIC_INTERRUPT_CELL_FLAGS    2
#define GIC_INTERRUPT_TYPE_SPI      0
#define GIC_INTERRUPT_TYPE_PPI      1
#define GIC_INTERRUPT_BASE_SPI      32
#define GIC_INTERRUPT_BASE_PPI      16


#define FDT_BUS_MODULE_NAME		"bus_managers/fdt/root/dk_driver_v1"
#define FDT_DEVICE_MODULE_NAME	"bus_managers/fdt/dk_driver_v1"
#define FDT_DEVFS_NODE_NAME		"bus/fdt/blob"

// Legacy string property stored on every FDT node; the FDT bus manager
// uses it internally to resolve back to the libfdt node offset.
#define FDT_NODE_ATTR			"fdt/node"
#define FDT_NAME_ATTR			"fdt/name"
#define FDT_DEVICE_TYPE_ATTR	"fdt/device_type"
#define FDT_COMPATIBLE_ATTR		"fdt/compatible"


// gFDT lives in the kernel (arch_platform.cpp on arm/arm64/riscv64).
// On architectures where the kernel does not define it (x86), resolve
// the reference as NULL via a weak attribute so the module still links,
// and fdt_bus_probe will simply refuse to attach.
extern void* gFDT __attribute__((weak));

dk_keeper_info* gDeviceKeeper;

extern dk_driver_info gFdtBusDriver;
extern dk_driver_info gFdtDeviceDriver;
extern fdt_bus_module_info gFdtBusInterface;
extern fdt_device_module_info gFdtDeviceInterface;


//#pragma mark - types


struct fdt_bus {
	dk_node* node;
	HashMap<HashKey32<int32>, dk_node*> phandles;
};


struct fdt_device {
	dk_node* node;
	dk_node* bus;
};


struct fdt_interrupt_map_entry {
	uint32_t childAddr;
	uint32_t childIrq;
	uint32_t parentIrqCtrl;
	uint32_t parentIrq;
};


struct fdt_interrupt_map {
	uint32_t childAddrMask;
	uint32_t childIrqMask;

	Vector<fdt_interrupt_map_entry> fInterruptMap;
};


//#pragma mark - bus tree construction


static status_t
fdt_register_node(fdt_bus* bus, int node, dk_node* parentDev,
	dk_node*& curDev)
{
	TRACE("%s('%s', %p)\n", __func__, fdt_get_name(gFDT, node, NULL),
		parentDev);

	const void* prop; int propLen;
	Vector<dk_property> attrs;
	int nameLen = 0;
	const char* name = fdt_get_name(gFDT, node, &nameLen);

	if (name == NULL) {
		dprintf("%s ERROR: fdt_get_name: %s\n", __func__,
			fdt_strerror(nameLen));
		return B_ERROR;
	}

	attrs.Add(DK_PROP_STRING(KOSM_DEVICE_BUS, "fdt"));
	attrs.Add(DK_PROP_STRING(KOSM_LABEL,
		(strcmp(name, "") != 0) ? name : "Root"));
	attrs.Add(DK_PROP_UINT32(FDT_NODE_ATTR, (uint32)node));
	attrs.Add(DK_PROP_STRING(FDT_NAME_ATTR, name));

	prop = fdt_getprop(gFDT, node, "device_type", &propLen);
	if (prop != NULL)
		attrs.Add(DK_PROP_STRING(FDT_DEVICE_TYPE_ATTR, (const char*)prop));

	prop = fdt_getprop(gFDT, node, "compatible", &propLen);
	if (prop != NULL) {
		const char* propStr = (const char*)prop;
		const char* propEnd = propStr + propLen;
		while (propEnd - propStr > 0) {
			int curLen = strlen(propStr);
			attrs.Add(DK_PROP_STRING(FDT_COMPATIBLE_ATTR, propStr));
			propStr += curLen + 1;
		}
	}

	attrs.Add(DK_PROP_END);

	status_t res = gDeviceKeeper->register_node(parentDev,
		FDT_DEVICE_MODULE_NAME, &attrs[0], NULL, &curDev);

	if (res < B_OK)
		return res;

	prop = fdt_getprop(gFDT, node, "phandle", &propLen);
	if (prop != NULL)
		bus->phandles.Put(fdt32_to_cpu(*(uint32_t*)prop), curDev);

	return B_OK;
}


static void
fdt_traverse(fdt_bus* bus, int& node, int& depth, dk_node* parentDev)
{
	int curDepth = depth;
#if 0
	for (int i = 0; i < depth; i++) dprintf("  ");
	dprintf("node('%s')\n", fdt_get_name(gFDT, node, NULL));
#endif
	dk_node* curDev = NULL;
	fdt_register_node(bus, node, parentDev, curDev);

	node = fdt_next_node(gFDT, node, &depth);
	while (node >= 0 && depth == curDepth + 1) {
		fdt_traverse(bus, node, depth, curDev);
	}
}


//#pragma mark - bus driver attach/detach


static float
fdt_bus_probe(dk_node* parent)
{
	TRACE("fdt_bus_probe\n");

	// only attach to the root node
	char bus[64];
	if (gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1.0f;

	if (strcmp(bus, "root") != 0)
		return 0.0f;

	if (gFDT == NULL)
		return 0.0f;

	return 1.0f;
}


// devfs read handler for /dev/bus/fdt/blob — exposes the raw flattened
// device tree blob to userland.
static status_t
fdt_devfs_read(void* cookie, off_t pos, void* buffer, size_t* _length)
{
	if (pos < 0)
		return B_BAD_VALUE;

	size_t size = fdt_totalsize(gFDT);
	if ((uint64)pos >= size) {
		*_length = 0;
		return B_OK;
	}

	size_t readSize = *_length;
	if (pos + readSize > size)
		readSize = size - pos;

	status_t res = user_memcpy(buffer, (uint8*)gFDT + pos, readSize);
	if (res < B_OK)
		return res;

	*_length = readSize;
	return B_OK;
}


static status_t
fdt_devfs_open(void* /*driverCookie*/, const char* /*path*/,
	int /*openMode*/, void** _cookie)
{
	*_cookie = NULL;
	return B_OK;
}


static status_t
fdt_devfs_close(void* /*cookie*/)
{
	return B_OK;
}


static status_t
fdt_devfs_free(void* /*cookie*/)
{
	return B_OK;
}


static status_t
fdt_devfs_control(void* /*cookie*/, uint32 /*op*/, void* /*buffer*/,
	size_t /*length*/)
{
	return B_DEV_INVALID_IOCTL;
}


static dk_device_ops gFdtDevfsOps = {
	.open    = fdt_devfs_open,
	.close   = fdt_devfs_close,
	.free    = fdt_devfs_free,
	.read    = fdt_devfs_read,
	.write   = NULL,
	.io      = NULL,
	.control = fdt_devfs_control,
};


static status_t
fdt_bus_attach(dk_node* node, void** _cookie)
{
	TRACE("fdt_bus_attach\n");

	if (gFDT == NULL) {
		TRACE("FDT is NULL!\n");
		return B_DEVICE_NOT_FOUND;
	}

	ObjectDeleter<fdt_bus> bus(new(std::nothrow) fdt_bus());
	if (!bus.IsSet())
		return B_NO_MEMORY;

	// gFDT is stored in kernel_args and will be freed, so copy it to
	// the kernel heap.
	size_t size = fdt_totalsize(gFDT);
	void* newFDT = malloc(size);
	if (newFDT == NULL)
		return B_NO_MEMORY;

	memcpy(newFDT, gFDT, size);
	gFDT = newFDT;

	bus->node = node;

	// Publish the FDT bus interface on this node so child drivers can
	// walk up via get_interface(FDT_BUS_INTERFACE_NAME).
	status_t res = gDeviceKeeper->publish_interface(node,
		FDT_BUS_INTERFACE_NAME, &gFdtBusInterface);
	if (res != B_OK)
		return res;

	// Publish the raw FDT blob as a devfs node.
	res = gDeviceKeeper->publish_device(node, FDT_DEVFS_NODE_NAME,
		&gFdtDevfsOps);
	if (res != B_OK)
		return res;

	// Walk the flattened device tree and register every node as a child
	// attached to FDT_DEVICE_MODULE_NAME.
	int dtNode = -1, depth = -1;
	dtNode = fdt_next_node(gFDT, dtNode, &depth);
	fdt_traverse(bus.Get(), dtNode, depth, node);

	*_cookie = bus.Detach();
	return B_OK;
}


static void
fdt_bus_detach(void* cookie)
{
	TRACE("fdt_bus_detach\n");
	ObjectDeleter<fdt_bus> bus((fdt_bus*)cookie);
}


//#pragma mark - device driver attach/detach


static status_t
fdt_device_attach(dk_node* node, void** _cookie)
{
	TRACE("fdt_device_attach()\n");

	ObjectDeleter<fdt_device> dev(new(std::nothrow) fdt_device());
	if (!dev.IsSet())
		return B_NO_MEMORY;

	dev->node = node;

	// Resolve our containing fdt_bus. Walk up the parent chain until we
	// hit a node that carries the FDT bus interface (published once on
	// the bus root). Intermediate ancestors along the way are other
	// fdt_device nodes, so we follow them via their driver cookie.
	dk_node* parent = gDeviceKeeper->get_parent_node(node);
	while (parent != NULL) {
		dk_driver_info* parentDriver = NULL;
		void* parentCookie = NULL;
		status_t s = gDeviceKeeper->get_node_driver(parent,
			&parentDriver, &parentCookie);
		if (s != B_OK) {
			gDeviceKeeper->put_node(parent);
			return s;
		}

		if (parentDriver == &gFdtBusDriver) {
			dev->bus = parent;
			gDeviceKeeper->put_node(parent);
			break;
		}
		if (parentDriver == &gFdtDeviceDriver) {
			fdt_device* parentFdt = (fdt_device*)parentCookie;
			dev->bus = parentFdt->bus;
			gDeviceKeeper->put_node(parent);
			break;
		}

		dk_node* nextParent = gDeviceKeeper->get_parent_node(parent);
		gDeviceKeeper->put_node(parent);
		parent = nextParent;
	}

	if (dev->bus == NULL) {
		dprintf("fdt_device_attach: bus root not found in ancestry\n");
		return B_ERROR;
	}

	// Publish the FDT device interface on this node so child drivers of
	// this FDT node can retrieve it via get_interface walk-up.
	status_t res = gDeviceKeeper->publish_interface(node,
		FDT_DEVICE_INTERFACE_NAME, &gFdtDeviceInterface);
	if (res != B_OK)
		return res;

	*_cookie = dev.Detach();
	return B_OK;
}


static void
fdt_device_detach(void* cookie)
{
	TRACE("fdt_device_detach()\n");
	ObjectDeleter<fdt_device> dev((fdt_device*)cookie);
}


//#pragma mark - fdt_bus_module_info implementation


static dk_node*
fdt_bus_node_by_phandle(fdt_bus* bus, int phandle)
{
	ASSERT(bus != NULL);

	dk_node** devNode;
	if (!bus->phandles.Get(phandle, devNode))
		return NULL;

	return *devNode;
}


//#pragma mark - fdt_device_module_info implementation


static dk_node*
fdt_device_get_bus(fdt_device* dev)
{
	ASSERT(dev != NULL);
	return dev->bus;
}


static const char*
fdt_device_get_name(fdt_device* dev)
{
	ASSERT(dev != NULL);

	uint32 fdtNode = 0;
	ASSERT(gDeviceKeeper->get_property_uint32(
		dev->node, FDT_NODE_ATTR, &fdtNode, false) >= B_OK);

	return fdt_get_name(gFDT, (int)fdtNode, NULL);
}


static const void*
fdt_device_get_prop(fdt_device* dev, const char* name, int* len)
{
	ASSERT(dev != NULL);

	uint32 fdtNode = 0;
	ASSERT(gDeviceKeeper->get_property_uint32(
		dev->node, FDT_NODE_ATTR, &fdtNode, false) >= B_OK);

	return fdt_getprop(gFDT, (int)fdtNode, name, len);
}


static uint32
fdt_get_address_cells(const void* fdt, int node)
{
	uint32 res = 2;

	int parent = fdt_parent_offset(fdt, node);
	if (parent < 0)
		return res;

	uint32* prop = (uint32*)fdt_getprop(fdt, parent, "#address-cells", NULL);
	if (prop == NULL)
		return res;

	res = fdt32_to_cpu(*prop);
	return res;
}


static uint32
fdt_get_size_cells(const void* fdt, int node)
{
	uint32 res = 1;

	int parent = fdt_parent_offset(fdt, node);
	if (parent < 0)
		return res;

	uint32* prop = (uint32*)fdt_getprop(fdt, parent, "#size-cells", NULL);
	if (prop == NULL)
		return res;

	res = fdt32_to_cpu(*prop);
	return res;
}


static bool
fdt_device_get_reg(fdt_device* dev, uint32 ord, uint64* regs, uint64* len)
{
	ASSERT(dev != NULL);

	uint32 fdtNode = 0;
	ASSERT(gDeviceKeeper->get_property_uint32(
		dev->node, FDT_NODE_ATTR, &fdtNode, false) >= B_OK);

	int propLen;
	const void* prop = fdt_getprop(gFDT, (int)fdtNode, "reg", &propLen);
	if (prop == NULL)
		return false;

	uint32 addressCells = fdt_get_address_cells(gFDT, fdtNode);
	uint32 sizeCells = fdt_get_size_cells(gFDT, fdtNode);
	size_t entrySize = 4 * (addressCells + sizeCells);

	if ((ord + 1) * entrySize > (uint32)propLen)
		return false;

	const void* addressPtr = (const uint8*)prop + ord * entrySize;
	const void* sizePtr = (const uint32*)addressPtr + addressCells;

	switch (addressCells) {
		case 1:
			*regs = fdt32_to_cpu(*(const uint32*)addressPtr);
			break;
		case 2:
			*regs = fdt64_to_cpu(*(const uint64*)addressPtr);
			break;
		default:
			return false;
	}
	switch (sizeCells) {
		case 1:
			*len = fdt32_to_cpu(*(const uint32*)sizePtr);
			break;
		case 2:
			*len = fdt64_to_cpu(*(const uint64*)sizePtr);
			break;
		default:
			return false;
	}

	return true;
}


static uint32
fdt_get_interrupt_parent(fdt_device* /*dev*/, int node)
{
	while (node >= 0) {
		uint32* prop;
		int propLen;
		prop = (uint32*)fdt_getprop(gFDT, node, "interrupt-parent", &propLen);
		if (prop != NULL && propLen == 4) {
			return fdt32_to_cpu(*prop);
		}

		node = fdt_parent_offset(gFDT, node);
	}

	return 0;
}


static uint32
fdt_get_interrupt_cells(uint32 interrupt_parent_phandle)
{
	if (interrupt_parent_phandle > 0) {
		int node = fdt_node_offset_by_phandle(gFDT, interrupt_parent_phandle);
		if (node >= 0) {
			uint32* prop;
			int propLen;
			prop  = (uint32*)fdt_getprop(gFDT, node, "#interrupt-cells", &propLen);
			if (prop != NULL && propLen == 4) {
				return fdt32_to_cpu(*prop);
			}
		}
	}
	return 1;
}


static bool
fdt_device_get_interrupt(fdt_device* dev, uint32 index,
	dk_node** interruptController, uint64* interrupt)
{
	ASSERT(dev != NULL);

	uint32 fdtNode = 0;
	ASSERT(gDeviceKeeper->get_property_uint32(
		dev->node, FDT_NODE_ATTR, &fdtNode, false) >= B_OK);

	int propLen;
	const uint32* prop = (uint32*)fdt_getprop(gFDT, (int)fdtNode,
		"interrupts-extended", &propLen);
	if (prop == NULL) {
		uint32 interruptParent = fdt_get_interrupt_parent(dev, fdtNode);
		uint32 interruptCells = fdt_get_interrupt_cells(interruptParent);

		prop = (uint32*)fdt_getprop(gFDT, (int)fdtNode, "interrupts",
			&propLen);
		if (prop == NULL)
			return false;

		if ((index + 1) * interruptCells * sizeof(uint32) > (uint32)propLen)
			return false;

		uint32 offset = interruptCells * index;
		uint32 interruptNumber = 0;

		if ((interruptCells == 1) || (interruptCells == 2)) {
			interruptNumber = fdt32_to_cpu(*(prop + offset));
		} else if (interruptCells == 3) {
			uint32 interruptType = fdt32_to_cpu(prop[offset + GIC_INTERRUPT_CELL_TYPE]);
			interruptNumber = fdt32_to_cpu(prop[offset + GIC_INTERRUPT_CELL_ID]);

			if (interruptType == GIC_INTERRUPT_TYPE_SPI)
				interruptNumber += GIC_INTERRUPT_BASE_SPI;
			else if (interruptType == GIC_INTERRUPT_TYPE_PPI)
				interruptNumber += GIC_INTERRUPT_BASE_PPI;
		} else {
			panic("unsupported interruptCells");
		}

		if (interrupt != NULL)
			*interrupt = interruptNumber;

		if (interruptController != NULL && interruptParent != 0) {
			dk_driver_info* busDriver = NULL;
			fdt_bus* bus = NULL;
			ASSERT(gDeviceKeeper->get_node_driver(dev->bus, &busDriver,
				(void**)&bus) >= B_OK);
			*interruptController = fdt_bus_node_by_phandle(bus, interruptParent);
		}

		return true;
	}

	if ((index + 1) * 8 > (uint32)propLen)
		return false;

	if (interruptController != NULL) {
		uint32 phandle = fdt32_to_cpu(*(prop + 2 * index));

		dk_driver_info* busDriver = NULL;
		fdt_bus* bus = NULL;
		ASSERT(gDeviceKeeper->get_node_driver(
			dev->bus, &busDriver, (void**)&bus) >= B_OK);

		*interruptController = fdt_bus_node_by_phandle(bus, phandle);
	}

	if (interrupt != NULL)
		*interrupt = fdt32_to_cpu(*(prop + 2 * index + 1));

	return true;
}


static struct fdt_interrupt_map*
fdt_device_get_interrupt_map(struct fdt_device* dev)
{
	int fdtNode = 0;
	ASSERT(gDeviceKeeper->get_property_uint32(
		dev->node, FDT_NODE_ATTR, (uint32*)&fdtNode, false) >= B_OK);

	ObjectDeleter<struct fdt_interrupt_map> interrupt_map(
		new struct fdt_interrupt_map());

	int intMapMaskLen;
	const void* intMapMask = fdt_getprop(gFDT, fdtNode, "interrupt-map-mask",
		&intMapMaskLen);

	if (intMapMask == NULL || intMapMaskLen != 4 * 4) {
		dprintf("  interrupt-map-mask property not found or invalid\n");
		return NULL;
	}

	interrupt_map->childAddrMask = B_BENDIAN_TO_HOST_INT32(*((uint32*)intMapMask + 0));
	interrupt_map->childIrqMask = B_BENDIAN_TO_HOST_INT32(*((uint32*)intMapMask + 3));

	int intMapLen;
	const void* intMapAddr = fdt_getprop(gFDT, fdtNode, "interrupt-map", &intMapLen);
	if (intMapAddr == NULL) {
		dprintf("  interrupt-map property not found\n");
		return NULL;
	}

	int addressCells = 3;
	int interruptCells = 1;
	int phandleCells = 1;

	const void* property;

	property = fdt_getprop(gFDT, fdtNode, "#address-cells", NULL);
	if (property != NULL)
		addressCells = B_BENDIAN_TO_HOST_INT32(*(uint32*)property);

	property = fdt_getprop(gFDT, fdtNode, "#interrupt-cells", NULL);
	if (property != NULL)
		interruptCells = B_BENDIAN_TO_HOST_INT32(*(uint32*)property);

	uint32_t* it = (uint32_t*)intMapAddr;
	while ((uint8_t*)it - (uint8_t*)intMapAddr < intMapLen) {
		struct fdt_interrupt_map_entry irqEntry;

		irqEntry.childAddr = B_BENDIAN_TO_HOST_INT32(*it);
		it += addressCells;

		irqEntry.childIrq = B_BENDIAN_TO_HOST_INT32(*it);
		it += interruptCells;

		irqEntry.parentIrqCtrl = B_BENDIAN_TO_HOST_INT32(*it);
		it += phandleCells;

		int parentAddressCells = 0;
		int parentInterruptCells = 1;

		int interruptParent = fdt_node_offset_by_phandle(gFDT, irqEntry.parentIrqCtrl);
		if (interruptParent >= 0) {
			property = fdt_getprop(gFDT, interruptParent, "#address-cells", NULL);
			if (property != NULL)
				parentAddressCells = B_BENDIAN_TO_HOST_INT32(*(uint32*)property);

			property = fdt_getprop(gFDT, interruptParent, "#interrupt-cells", NULL);
			if (property != NULL)
				parentInterruptCells = B_BENDIAN_TO_HOST_INT32(*(uint32*)property);
		}

		it += parentAddressCells;

		if ((parentInterruptCells == 1) || (parentInterruptCells == 2)) {
			irqEntry.parentIrq = B_BENDIAN_TO_HOST_INT32(*it);
		} else if (parentInterruptCells == 3) {
			uint32 interruptType = fdt32_to_cpu(it[GIC_INTERRUPT_CELL_TYPE]);
			uint32 interruptNumber = fdt32_to_cpu(it[GIC_INTERRUPT_CELL_ID]);

			if (interruptType == GIC_INTERRUPT_TYPE_SPI)
				irqEntry.parentIrq = interruptNumber + GIC_INTERRUPT_BASE_SPI;
			else if (interruptType == GIC_INTERRUPT_TYPE_PPI)
				irqEntry.parentIrq = interruptNumber + GIC_INTERRUPT_BASE_PPI;
			else
				irqEntry.parentIrq = interruptNumber;
		}
		it += parentInterruptCells;

		interrupt_map->fInterruptMap.PushBack(irqEntry);
	}

	return interrupt_map.Detach();
}


static void
fdt_device_print_interrupt_map(struct fdt_interrupt_map* interruptMap)
{
	if (interruptMap == NULL)
		return;

	dprintf("interrupt_map_mask: 0x%08" PRIx32 ", 0x%08" PRIx32 "\n",
		interruptMap->childAddrMask, interruptMap->childIrqMask);
	dprintf("interrupt_map:\n");

	for (Vector<struct fdt_interrupt_map_entry>::Iterator it = interruptMap->fInterruptMap.Begin();
		it != interruptMap->fInterruptMap.End();
		it++) {

		dprintf("childAddr=0x%08" PRIx32 ", childIrq=%" PRIu32
			", parentIrqCtrl=%" PRIu32 ", parentIrq=%" PRIu32 "\n",
			it->childAddr, it->childIrq, it->parentIrqCtrl, it->parentIrq);
	}
}


static uint32
fdt_device_lookup_interrupt_map(struct fdt_interrupt_map* interruptMap,
	uint32 childAddr, uint32 childIrq)
{
	if (interruptMap == NULL)
		return 0xffffffff;

	childAddr &= interruptMap->childAddrMask;
	childIrq &= interruptMap->childIrqMask;

	for (Vector<struct fdt_interrupt_map_entry>::Iterator it = interruptMap->fInterruptMap.Begin();
			it != interruptMap->fInterruptMap.End(); it++) {
		if ((it->childAddr == childAddr) && (it->childIrq == childIrq))
			return it->parentIrq;
	}

	return 0xffffffff;
}


//#pragma mark - module tables


fdt_bus_module_info gFdtBusInterface = {
	fdt_bus_node_by_phandle,
};


fdt_device_module_info gFdtDeviceInterface = {
	fdt_device_get_bus,
	fdt_device_get_name,
	fdt_device_get_prop,
	fdt_device_get_reg,
	fdt_device_get_interrupt,
	fdt_device_get_interrupt_map,
	fdt_device_print_interrupt_map,
	fdt_device_lookup_interrupt_map,
};


static const dk_match_rule gFdtBusMatch[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "root"),
	DK_MATCH_END
};

static const dk_match_dict gFdtBusMatchDict = {
	gFdtBusMatch,
	0
};


dk_driver_info gFdtBusDriver = {
	.info       = { FDT_BUS_MODULE_NAME, 0, NULL },
	.match      = &gFdtBusMatchDict,
	.probe      = fdt_bus_probe,
	.attach     = fdt_bus_attach,
	.detach     = fdt_bus_detach,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};


dk_driver_info gFdtDeviceDriver = {
	.info       = { FDT_DEVICE_MODULE_NAME, 0, NULL },
	.attach     = fdt_device_attach,
	.detach     = fdt_device_detach,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ NULL }
};


module_info* modules[] = {
	(module_info*)&gFdtBusDriver,
	(module_info*)&gFdtDeviceDriver,
	NULL
};
