/*
 * Copyright 2018-2025 Haiku, Inc. All rights reserved.
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
#include "acpi.h"

#include <KernelExport.h>

#include "IOSchedulerSimple.h"
#include "mmc.h"
#include "sdhci.h"


//#define TRACE_SDHCI
#ifdef TRACE_SDHCI
#	define TRACE(x...) dprintf("\33[33msdhci:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("\33[33msdhci:\33[0m " x)
#define ERROR(x...)			dprintf("\33[33msdhci:\33[0m " x)
#define CALLED(x...)		TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define SDHCI_DEVICE_MODULE_NAME "busses/mmc/sdhci/dk_driver_v1"
#define SDHCI_ACPI_MMC_BUS_MODULE_NAME "busses/mmc/sdhci/acpi/device/dk_driver_v1"

static acpi_status
sdhci_acpi_scan_parse_callback(ACPI_RESOURCE *res, void *context)
{
	struct sdhci_crs* crs = (struct sdhci_crs*)context;

	if (res->Type == ACPI_RESOURCE_TYPE_FIXED_MEMORY32) {
		crs->addr_bas = res->Data.FixedMemory32.Address;
		crs->addr_len = res->Data.FixedMemory32.AddressLength;
	} else if (res->Type == ACPI_RESOURCE_TYPE_IRQ) {
		crs->irq = res->Data.Irq.Interrupt;
		//crs->irq_triggering = res->Data.Irq.Triggering;
		//crs->irq_polarity = res->Data.Irq.Polarity;
		//crs->irq_shareable = res->Data.Irq.Shareable;
	} else if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
		crs->irq = res->Data.ExtendedIrq.Interrupt;
		//crs->irq_triggering = res->Data.ExtendedIrq.Triggering;
		//crs->irq_polarity = res->Data.ExtendedIrq.Polarity;
		//crs->irq_shareable = res->Data.ExtendedIrq.Shareable;
	}

	return B_OK;
}

// Forward decl: defined later in this file.
extern mmc_bus_interface sSDHCIACPIBusOps;


status_t
init_bus_acpi(dk_node* node, void** bus_cookie)
{
	CALLED();

	// Walk up to retrieve the ACPI device interface published by the
	// ACPI bus manager on the parent ACPI device node.
	acpi_device_module_info* acpi;
	acpi_device device;

	if (gDeviceKeeper->get_interface(node, ACPI_DEVICE_INTERFACE_NAME,
			KOSM_INTERFACE_ANCESTORS,
			(const void**)&acpi, (void**)&device) != B_OK) {
		ERROR("Could not get ACPI device interface\n");
		return B_ERROR;
	}

	// Ignore invalid bars
	TRACE("Register SD bus\n");

	struct sdhci_crs crs;
	if(acpi->walk_resources(device, (ACPI_STRING)"_CRS",
			sdhci_acpi_scan_parse_callback, &crs) != B_OK) {
		ERROR("Couldn't scan ACPI register set\n");
		return B_IO_ERROR;
	}

	TRACE("addr: %" B_PRIx32 " len: %" B_PRIx32 "\n", crs.addr_bas, crs.addr_len);

	if (crs.addr_bas == 0 || crs.addr_len == 0) {
		ERROR("No registers to map\n");
		return B_IO_ERROR;
	}

	// map the slot registers
	area_id	regs_area;
	struct registers* _regs;
	regs_area = map_physical_memory("sdhc_regs_map",
		crs.addr_bas, crs.addr_len, B_ANY_KERNEL_BLOCK_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&_regs);

	if (regs_area < B_OK) {
		ERROR("Could not map registers\n");
		return B_BAD_VALUE;
	}

	// the interrupt is shared between all busses in an SDHC controller, but
	// they each register an handler. Not a problem, we will just test the
	// interrupt registers for all busses one after the other and find no
	// interrupts on the idle busses.
	uint8_t irq = crs.irq;
	TRACE("irq interrupt line: %d\n", irq);

	SdhciBus* bus = new(std::nothrow) SdhciBus(_regs, irq, true);

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
		&sSDHCIACPIBusOps);

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
register_child_devices_acpi(void* cookie)
{
	CALLED();
	SdhciDevice* context = (SdhciDevice*)cookie;

	TRACE("register_child_devices\n");

	char prettyName[25];

	sprintf(prettyName, "SDHC bus");
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
		DK_PROP_END
	};
	dk_node* node;
	if (gDeviceKeeper->register_node(context->fNode,
			SDHCI_ACPI_MMC_BUS_MODULE_NAME, attrs, NULL,
			&node) != B_OK)
		return B_BAD_VALUE;
	return B_OK;
}

float
supports_device_acpi(dk_node* parent)
{
	char hid[64];
	char uid[64];
	uint32 type;

	if (gDeviceKeeper->get_property_uint32(parent, KOSM_ACPI_DEVICE_TYPE, &type, false)
		|| type != ACPI_TYPE_DEVICE) {
		return 0.0f;
	}

	if (gDeviceKeeper->get_property_string(parent, KOSM_ACPI_DEVICE_HID,
			hid, sizeof(hid), NULL, false)) {
		TRACE("No hid attribute\n");
		return 0.0f;
	}

	if (gDeviceKeeper->get_property_string(parent, KOSM_ACPI_DEVICE_UID,
			uid, sizeof(uid), NULL, false)) {
		TRACE("No uid attribute\n");
		return 0.0f;
	}

	// CID is stored as KOSM_DEVICE_COMPATIBLE stringlist.
	// Check if it contains "PNP0D40" (standard ACPI CID for SD host).
	bool cidMatch = false;
	{
		dk_property* prop = NULL;
		uint64 version = 0;
		while (true) {
			status_t s = gDeviceKeeper->get_next_property(parent, &prop,
				&version);
			if (s == B_INTERRUPTED) {
				// store mutated mid-iteration — restart
				prop = NULL;
				continue;
			}
			if (s != B_OK)
				break;
			if (strcmp(prop->name, KOSM_DEVICE_COMPATIBLE) == 0
				&& prop->type == KOSM_STRINGLIST_TYPE) {
				for (uint32 i = 0; i < prop->value.stringlist.count; i++) {
					if (strcmp(prop->value.stringlist.items[i],
							"PNP0D40") == 0) {
						cidMatch = true;
						break;
					}
				}
				break;
			}
		}
	}

	if (!cidMatch) {
		TRACE("No matching CID\n");
		return 0.0f;
	}

	acpi_device_module_info* acpi;
	acpi_device* device;
	gDeviceKeeper->get_interface(parent, ACPI_DEVICE_INTERFACE_NAME,
		KOSM_INTERFACE_SELF | KOSM_INTERFACE_ANCESTORS,
		(const void**)&acpi, (void**)&device);
	TRACE("SDHCI Device found! hid: %s, uid: %s\n", hid, uid);

	return 0.8f;
}

// MMC bus ops for the ACPI SD slot
mmc_bus_interface sSDHCIACPIBusOps = {
	.set_clock = set_clock,
	.execute_command = execute_command,
	.do_io = do_io,
	.set_scan_semaphore = set_scan_semaphore,
	.set_bus_width = set_bus_width,
};

// Device node registered for each SD slot. Publishes the MMC bus
// interface in init_bus_acpi for the MMC bus manager to retrieve via
// get_interface walk-up.
dk_driver_info gSDHCIACPIDeviceModule = {
	.info       = { SDHCI_ACPI_MMC_BUS_MODULE_NAME, 0, NULL },
	.attach     = init_bus_acpi,
	.detach     = uninit_bus,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};
