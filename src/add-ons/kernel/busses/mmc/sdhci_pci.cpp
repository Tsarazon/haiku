/*
 * Copyright 2018-2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		B Krishnan Iyer, krishnaniyer97@gmail.com
 *		Adrien Destugues, pulkomandy@pulkomandy.tk
 *		Ron Ben Aroya, sed4906birdie@gmail.com
 */
#include <algorithm>
#include <new>
#include <stdio.h>
#include <string.h>

#include <bus/PCI.h>
#include <ACPI.h>

#include <KernelExport.h>

#include "IOSchedulerSimple.h"
#include "mmc.h"
#include "sdhci.h"


//#define TRACE_SDHCI
#ifdef TRACE_SDHCI
#	define TRACE(x...) dprintf("\33[33msdhci_pci:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[33msdhci_pci:\33[0m " x)
#define ERROR(x...)			dprintf("\33[33msdhci_pci:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define SDHCI_DEVICE_MODULE_NAME "busses/mmc/sdhci/dk_driver_v1"
#define SDHCI_PCI_MMC_BUS_MODULE_NAME "busses/mmc/sdhci/pci/device/dk_driver_v1"

#define SLOT_NUMBER				"device/slot"
#define BAR_INDEX				"device/bar"

#define SDHCI_PCI_SLOT_INFO 							0x40
#define SDHCI_PCI_SLOTS(x) 								((((x) >> 4) & 0x7) + 1)
#define SDHCI_PCI_SLOT_INFO_FIRST_BASE_INDEX(x)			((x) & 0x7)

// Ricoh specific PCI registers
// Ricoh devices start in a vendor-specific mode but can be switched
// to standard sdhci using these PCI registers
#define SDHCI_PCI_RICOH_MODE_KEY						0xf9
#define SDHCI_PCI_RICOH_MODE							0x150
#define SDHCI_PCI_RICOH_MODE_SD20						0x10


// Forward decl: defined later in this file.
extern mmc_bus_interface sSDHCIPCIBusOps;


status_t
init_bus_pci(dk_node* node, void** bus_cookie)
{
	CALLED();

	// Walk up to retrieve the PCI device interface (published by the
	// PCI bus manager on the parent PCI device node).
	pci_device_ops* pci;
	pci_device* device;

	if (gDeviceKeeper->get_interface(node, PCI_DEVICE_INTERFACE_NAME,
			KOSM_INTERFACE_ANCESTORS,
			(const void**)&pci, (void**)&device) != B_OK) {
		ERROR("Could not get PCI device interface\n");
		return B_ERROR;
	}

	uint8_t bar, slot;
	if (gDeviceKeeper->get_property_uint8(node, SLOT_NUMBER, &slot, false) < B_OK
		|| gDeviceKeeper->get_property_uint8(node, BAR_INDEX, &bar, false) < B_OK)
		return B_BAD_TYPE;

	// Ignore invalid bars
	TRACE("Register SD bus at slot %d, using bar %d\n", slot + 1, bar);

	pci_info pciInfo;
	pci->get_pci_info(device, &pciInfo);

	for (; bar < 6 && slot > 0; bar++, slot--) {
		if ((pciInfo.u.h0.base_register_flags[bar] & PCI_address_type)
			== PCI_address_type_64) {
			bar++;
		}
	}

	phys_addr_t physicalAddress = pciInfo.u.h0.base_registers[bar];
	uint64 barSize = pciInfo.u.h0.base_register_sizes[bar];
	if ((pciInfo.u.h0.base_register_flags[bar] & PCI_address_type)
			== PCI_address_type_64) {
		physicalAddress |= (uint64)pciInfo.u.h0.base_registers[bar + 1] << 32;
		barSize |= (uint64)pciInfo.u.h0.base_register_sizes[bar + 1] << 32;
	}

	if (physicalAddress == 0 || barSize == 0) {
		ERROR("No registers to map\n");
		return B_IO_ERROR;
	}

	// enable bus master and io
	uint16 pcicmd = pci->read_pci_config(device, PCI_command, 2);
	pcicmd &= ~(PCI_command_int_disable | PCI_command_io);
	pcicmd |= PCI_command_master | PCI_command_memory;
	pci->write_pci_config(device, PCI_command, 2, pcicmd);

	// map the slot registers
	area_id	regs_area;
	struct registers* _regs;
	regs_area = map_physical_memory("sdhc_regs_map",
		physicalAddress, barSize, B_ANY_KERNEL_BLOCK_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&_regs);

	if (regs_area < B_OK) {
		ERROR("Could not map registers\n");
		return B_BAD_VALUE;
	}

	// the interrupt is shared between all busses in an SDHC controller, but
	// they each register an handler. Not a problem, we will just test the
	// interrupt registers for all busses one after the other and find no
	// interrupts on the idle busses.
	uint8_t irq = pciInfo.u.h0.interrupt_line;
	TRACE("irq interrupt line: %d\n", irq);

	SdhciBus* bus = new(std::nothrow) SdhciBus(_regs, irq, false);

	status_t status = B_NO_MEMORY;
	if (bus != NULL)
		status = bus->InitCheck();

	if (status != B_OK) {
		if (bus != NULL)
			delete bus;
		else
			delete_area(regs_area);
		return status;
	}

	// Store the created object as a cookie, allowing users of the bus to
	// locate it.
	*bus_cookie = bus;

	// Publish the MMC bus interface on this slot node so the MMC bus
	// manager can retrieve it via get_interface walk-up.
	gDeviceKeeper->publish_interface(node, MMC_BUS_INTERFACE_NAME,
		&sSDHCIPCIBusOps);

	// Register the MMC bus manager as a child node of this slot.
	dk_property mmcAttrs[] = {
		DK_PROP_STRING(KOSM_LABEL, "MMC Bus"),
		DK_PROP_END
	};
	gDeviceKeeper->register_node(node, MMC_BUS_MODULE_NAME, mmcAttrs, NULL,
		NULL);

	return status;
}

status_t
register_child_devices_pci(void* cookie)
{
	CALLED();
	SdhciDevice* context = (SdhciDevice*)cookie;
	pci_device_ops* pci;
	pci_device* device;

	if (gDeviceKeeper->get_interface(context->fNode, PCI_DEVICE_INTERFACE_NAME,
			KOSM_INTERFACE_ANCESTORS,
			(const void**)&pci, (void**)&device) != B_OK) {
		return B_ERROR;
	}
	uint8 slotsInfo = pci->read_pci_config(device, SDHCI_PCI_SLOT_INFO, 1);
	uint8 bar = SDHCI_PCI_SLOT_INFO_FIRST_BASE_INDEX(slotsInfo);
	uint8 slotsCount = SDHCI_PCI_SLOTS(slotsInfo);

	TRACE("register_child_devices: %u, %u\n", bar, slotsCount);

	char prettyName[25];

	if (slotsCount > 6 || bar > 5) {
		ERROR("Invalid slots count: %d or BAR count: %d \n", slotsCount, bar);
		return B_BAD_VALUE;
	}

	for (uint8_t slot = 0; slot < slotsCount; slot++) {
		sprintf(prettyName, "SDHC bus %" B_PRIu8, slot);
		dk_property attrs[] = {
			// properties of this controller for mmc bus manager
			DK_PROP_STRING(KOSM_LABEL, prettyName),
			DK_PROP_STRING(KOSM_DEVICE_BUS, "mmc"),

			// DMA properties
			// The high alignment is to force access only to complete sectors
			// These constraints could be removed by using ADMA which allows
			// use of the full 64bit address space and can do scatter-gather.
			DK_PROP_UINT32(KOSM_DMA_ALIGNMENT, 511),
			DK_PROP_UINT64(KOSM_DMA_HIGH_ADDRESS, 0x100000000LL),
			DK_PROP_UINT32(KOSM_DMA_BOUNDARY, (1 << 19) - 1),
			DK_PROP_UINT32(KOSM_DMA_MAX_SEGMENT_COUNT, 1),
			DK_PROP_UINT32(KOSM_DMA_MAX_SEGMENT_BLOCKS, (1 << 10) - 1),

			// private data to identify device
			DK_PROP_UINT8(SLOT_NUMBER, slot),
			DK_PROP_UINT8(BAR_INDEX, bar),
			DK_PROP_END
		};
		dk_node* node;
		if (gDeviceKeeper->register_node(context->fNode,
				SDHCI_PCI_MMC_BUS_MODULE_NAME, attrs, NULL,
				&node) != B_OK)
			return B_BAD_VALUE;
	}
	return B_OK;
}

status_t
init_device_pci(dk_node* node, SdhciDevice* context)
{
	// Walk up to retrieve the PCI device interface.
	pci_device_ops* pci;
	pci_device* device;
	uint16 vendorId, deviceId;

	if (gDeviceKeeper->get_interface(context->fNode, PCI_DEVICE_INTERFACE_NAME,
			KOSM_INTERFACE_ANCESTORS,
			(const void**)&pci, (void**)&device) != B_OK) {
		return B_ERROR;
	}

	if (gDeviceKeeper->get_property_uint16(node, KOSM_DEVICE_VENDOR_ID,
			&vendorId, true) != B_OK
		|| gDeviceKeeper->get_property_uint16(node, KOSM_DEVICE_ID, &deviceId,
			true) != B_OK) {
		panic("No vendor or device id attribute\n");
		return B_OK; // Let's hope it didn't need the quirk?
	}

	if (vendorId == 0x1180 && deviceId == 0xe823) {
		// Switch the device to SD2.0 mode
		// This should change its device id to 0xe822 if succesful.
		// The device then remains in SD2.0 mode even after reboot.
		pci->write_pci_config(device, SDHCI_PCI_RICOH_MODE_KEY, 1, 0xfc);
		context->fRicohOriginalMode = pci->read_pci_config(device,
			SDHCI_PCI_RICOH_MODE, 1);
		pci->write_pci_config(device, SDHCI_PCI_RICOH_MODE, 1,
			SDHCI_PCI_RICOH_MODE_SD20);
		pci->write_pci_config(device, SDHCI_PCI_RICOH_MODE_KEY, 1, 0);

		deviceId = pci->read_pci_config(device, 2, 2);
		TRACE("Device ID after Ricoh quirk: %x\n", deviceId);
	}

	return B_OK;
}

void
uninit_device_pci(SdhciDevice* context, dk_node* pciParent)
{
	// Walk up to retrieve the PCI device interface.
	pci_device_ops* pci;
	pci_device* device;
	uint16 vendorId, deviceId;

	if (gDeviceKeeper->get_interface(context->fNode, PCI_DEVICE_INTERFACE_NAME,
			KOSM_INTERFACE_ANCESTORS,
			(const void**)&pci, (void**)&device) != B_OK) {
		return;
	}

	if (gDeviceKeeper->get_property_uint16(context->fNode, KOSM_DEVICE_VENDOR_ID,
			&vendorId, true) != B_OK
		|| gDeviceKeeper->get_property_uint16(context->fNode, KOSM_DEVICE_ID,
			&deviceId, true) != B_OK) {
		ERROR("No vendor or device id attribute\n");
	} else if (vendorId == 0x1180 && deviceId == 0xe823) {
		pci->write_pci_config(device, SDHCI_PCI_RICOH_MODE_KEY, 1, 0xfc);
		pci->write_pci_config(device, SDHCI_PCI_RICOH_MODE, 1,
			context->fRicohOriginalMode);
		pci->write_pci_config(device, SDHCI_PCI_RICOH_MODE_KEY, 1, 0);
	}
}

float
supports_device_pci(dk_node* parent)
{
	uint16 type, subType;
	uint16 vendorId, deviceId;

	if (gDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_VENDOR_ID, &vendorId,
			false) != B_OK
		|| gDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_ID, &deviceId,
			false) != B_OK) {
		TRACE("No vendor or device id attribute\n");
		return 0.0f;
	}

	TRACE("supports_device(vid:%04x pid:%04x)\n", vendorId, deviceId);

	if (gDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_SUB_TYPE, &subType,
			false) < B_OK
		|| gDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_TYPE, &type,
			false) < B_OK) {
		TRACE("Could not find type/subtype attributes\n");
		return -1;
	}

	if (type == PCI_base_peripheral) {
		if (subType != PCI_sd_host) {
			// Also accept some compliant devices that do not advertise
			// themselves as such.
			if (vendorId != 0x1180
				|| (deviceId != 0xe823 && deviceId != 0xe822)) {
				TRACE("Not the right subclass, and not a Ricoh device\n");
				return 0.0f;
			}
		}

		pci_device_ops* pci;
		pci_device* device;
		gDeviceKeeper->get_interface(parent, PCI_DEVICE_INTERFACE_NAME,
			KOSM_INTERFACE_SELF | KOSM_INTERFACE_ANCESTORS,
			(const void**)&pci, (void**)&device);
		TRACE("SDHCI Device found! Subtype: 0x%04x, type: 0x%04x\n",
			subType, type);

		return 0.8f;
	}

	return 0.0f;
}

// MMC bus ops for the PCI SD slot
mmc_bus_interface sSDHCIPCIBusOps = {
	.set_clock = set_clock,
	.execute_command = execute_command,
	.do_io = do_io,
	.set_scan_semaphore = set_scan_semaphore,
	.set_bus_width = set_bus_width,
};

// Device node registered for each SD slot. It implements the MMC bus
// interface, retrieved by the MMC bus manager (mmc_bus.cpp) via
// get_interface walk-up.
dk_driver_info gSDHCIPCIDeviceModule = {
	.info       = { SDHCI_PCI_MMC_BUS_MODULE_NAME, 0, NULL },
	.attach     = init_bus_pci,
	.detach     = uninit_bus,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};
