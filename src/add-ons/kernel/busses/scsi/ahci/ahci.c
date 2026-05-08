/*
 * Copyright 2007, Marcus Overhagen. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ahci_defs.h"

#include <stdlib.h>
#include <string.h>


#define TRACE(a...) dprintf("ahci: " a)
//#define FLOW(a...)	dprintf("ahci: " a)
#define FLOW(a...)

#define AHCI_ID_GENERATOR "ahci/id"
#define AHCI_ID_ITEM "ahci/id"
#define AHCI_BRIDGE_PRETTY_NAME "AHCI Bridge"
#define AHCI_CONTROLLER_PRETTY_NAME "AHCI Controller"


const device_info kSupportedDevices[] = {
	{ 0x1002, 0x4380, "ATI SB600" },
	{ 0x1002, 0x4390, "ATI SB700/800" },
	{ 0x1002, 0x4391, "ATI IXP700" },
	{ 0x1002, 0x4392, "ATI SB700/800" },
	{ 0x1002, 0x4393, "ATI SB700/800" },
	{ 0x1002, 0x4394, "ATI SB700/800" },
	{ 0x1002, 0x4395, "ATI SB700/800" },
	{ 0x1039, 0x1184, "SiS 966" },
	{ 0x1039, 0x1185, "SiS 966" },
	{ 0x1039, 0x0186, "SiS 968" },
	{ 0x10b9, 0x5288, "Acer Labs M5288" },
	{ 0x10de, 0x044c, "NVIDIA MCP65" },
	{ 0x10de, 0x044d, "NVIDIA MCP65" },
	{ 0x10de, 0x044e, "NVIDIA MCP65" },
	{ 0x10de, 0x044f, "NVIDIA MCP65" },
	{ 0x10de, 0x045c, "NVIDIA MCP65" },
	{ 0x10de, 0x045d, "NVIDIA MCP65" },
	{ 0x10de, 0x045e, "NVIDIA MCP65" },
	{ 0x10de, 0x045f, "NVIDIA MCP65" },
	{ 0x10de, 0x0550, "NVIDIA MCP67" },
	{ 0x10de, 0x0551, "NVIDIA MCP67" },
	{ 0x10de, 0x0552, "NVIDIA MCP67" },
	{ 0x10de, 0x0553, "NVIDIA MCP67" },
	{ 0x10de, 0x0554, "NVIDIA MCP67" },
	{ 0x10de, 0x0555, "NVIDIA MCP67" },
	{ 0x10de, 0x0556, "NVIDIA MCP67" },
	{ 0x10de, 0x0557, "NVIDIA MCP67" },
	{ 0x10de, 0x0558, "NVIDIA MCP67" },
	{ 0x10de, 0x0559, "NVIDIA MCP67" },
	{ 0x10de, 0x055a, "NVIDIA MCP67" },
	{ 0x10de, 0x055b, "NVIDIA MCP67" },
	{ 0x10de, 0x07f0, "NVIDIA MCP73" },
	{ 0x10de, 0x07f1, "NVIDIA MCP73" },
	{ 0x10de, 0x07f2, "NVIDIA MCP73" },
	{ 0x10de, 0x07f3, "NVIDIA MCP73" },
	{ 0x10de, 0x07f4, "NVIDIA MCP73" },
	{ 0x10de, 0x07f5, "NVIDIA MCP73" },
	{ 0x10de, 0x07f6, "NVIDIA MCP73" },
	{ 0x10de, 0x07f7, "NVIDIA MCP73" },
	{ 0x10de, 0x07f8, "NVIDIA MCP73" },
	{ 0x10de, 0x07f9, "NVIDIA MCP73" },
	{ 0x10de, 0x07fa, "NVIDIA MCP73" },
	{ 0x10de, 0x07fb, "NVIDIA MCP73" },
	{ 0x10de, 0x0ad0, "NVIDIA MCP77" },
	{ 0x10de, 0x0ad1, "NVIDIA MCP77" },
	{ 0x10de, 0x0ad2, "NVIDIA MCP77" },
	{ 0x10de, 0x0ad3, "NVIDIA MCP77" },
	{ 0x10de, 0x0ad4, "NVIDIA MCP77" },
	{ 0x10de, 0x0ad5, "NVIDIA MCP77" },
	{ 0x10de, 0x0ad6, "NVIDIA MCP77" },
	{ 0x10de, 0x0ad7, "NVIDIA MCP77" },
	{ 0x10de, 0x0ad8, "NVIDIA MCP77" },
	{ 0x10de, 0x0ad9, "NVIDIA MCP77" },
	{ 0x10de, 0x0ada, "NVIDIA MCP77" },
	{ 0x10de, 0x0adb, "NVIDIA MCP77" },
	{ 0x10de, 0x0ab4, "NVIDIA MCP79" },
	{ 0x10de, 0x0ab5, "NVIDIA MCP79" },
	{ 0x10de, 0x0ab6, "NVIDIA MCP79" },
	{ 0x10de, 0x0ab7, "NVIDIA MCP79" },
	{ 0x10de, 0x0ab8, "NVIDIA MCP79" },
	{ 0x10de, 0x0ab9, "NVIDIA MCP79" },
	{ 0x10de, 0x0aba, "NVIDIA MCP79" },
	{ 0x10de, 0x0abb, "NVIDIA MCP79" },
	{ 0x10de, 0x0abc, "NVIDIA MCP79" },
	{ 0x10de, 0x0abd, "NVIDIA MCP79" },
	{ 0x10de, 0x0abe, "NVIDIA MCP79" },
	{ 0x10de, 0x0abf, "NVIDIA MCP79" },
	{ 0x10de, 0x0d84, "NVIDIA MCP89" },
	{ 0x10de, 0x0d85, "NVIDIA MCP89" },
	{ 0x10de, 0x0d86, "NVIDIA MCP89" },
	{ 0x10de, 0x0d87, "NVIDIA MCP89" },
	{ 0x10de, 0x0d88, "NVIDIA MCP89" },
	{ 0x10de, 0x0d89, "NVIDIA MCP89" },
	{ 0x10de, 0x0d8a, "NVIDIA MCP89" },
	{ 0x10de, 0x0d8b, "NVIDIA MCP89" },
	{ 0x10de, 0x0d8c, "NVIDIA MCP89" },
	{ 0x10de, 0x0d8d, "NVIDIA MCP89" },
	{ 0x10de, 0x0d8e, "NVIDIA MCP89" },
	{ 0x10de, 0x0d8f, "NVIDIA MCP89" },
	{ 0x1106, 0x3349, "VIA VT8251" },
	{ 0x1106, 0x6287, "VIA VT8251" },
	{ 0x11ab, 0x6121, "Marvell 6121" },
	{ 0x11ab, 0x6145, "Marvell 6145" },
	{ 0x197b, 0x2360, "JMicron JMB360" },
	{ 0x197b, 0x2361, "JMicron JMB361" },
	{ 0x197b, 0x2362, "JMicron JMB362" },
	{ 0x197b, 0x2363, "JMicron JMB363" },
	{ 0x197b, 0x2366, "JMicron JMB366" },
	{ 0x8086, 0x2652, "Intel ICH6R" },
	{ 0x8086, 0x2653, "Intel ICH6-M" },
	{ 0x8086, 0x2681, "Intel 63xxESB" },
	{ 0x8086, 0x2682, "Intel ESB2" },
	{ 0x8086, 0x2683, "Intel ESB2" },
	{ 0x8086, 0x27c1, "Intel ICH7R (AHCI mode)" },
	{ 0x8086, 0x27c3, "Intel ICH7R (RAID mode)" },
	{ 0x8086, 0x27c5, "Intel ICH7-M (AHCI mode)" },
	{ 0x8086, 0x27c6, "Intel ICH7-M DH (RAID mode)" },
	{ 0x8086, 0x2821, "Intel ICH8 (AHCI mode)" },
	{ 0x8086, 0x2822, "Intel ICH8R / ICH9 (RAID mode)" },
	{ 0x8086, 0x2824, "Intel ICH8 (AHCI mode)" },
	{ 0x8086, 0x2829, "Intel ICH8M (AHCI mode)" },
	{ 0x8086, 0x282a, "Intel ICH8M (RAID mode)" },
	{ 0x8086, 0x2922, "Intel ICH9 (AHCI mode)" },
	{ 0x8086, 0x2923, "Intel ICH9 (AHCI mode)" },
	{ 0x8086, 0x2924, "Intel ICH9" },
	{ 0x8086, 0x2925, "Intel ICH9" },
	{ 0x8086, 0x2927, "Intel ICH9" },
	{ 0x8086, 0x2929, "Intel ICH9M" },
	{ 0x8086, 0x292a, "Intel ICH9M" },
	{ 0x8086, 0x292b, "Intel ICH9M" },
	{ 0x8086, 0x292c, "Intel ICH9M" },
	{ 0x8086, 0x292f, "Intel ICH9M" },
	{ 0x8086, 0x294d, "Intel ICH9" },
	{ 0x8086, 0x294e, "Intel ICH9M" },
	{ 0x8086, 0x3a05, "Intel ICH10" },
	{ 0x8086, 0x3a22, "Intel ICH10" },
	{ 0x8086, 0x3a25, "Intel ICH10" },
	// Skylake (6th Gen - 100 Series / Sunrise Point)
	{ 0x8086, 0x9d03, "Intel Sunrise Point-LP SATA (AHCI)" },
	{ 0x8086, 0x9d05, "Intel Sunrise Point-LP SATA (RAID)" },
	{ 0x8086, 0x9d07, "Intel Sunrise Point-LP SATA (RAID)" },
	{ 0x8086, 0xa102, "Intel Sunrise Point-H SATA (AHCI)" },
	{ 0x8086, 0xa103, "Intel Sunrise Point-H SATA (AHCI)" },
	{ 0x8086, 0xa105, "Intel Sunrise Point-H SATA (RAID)" },
	{ 0x8086, 0xa106, "Intel Sunrise Point-H SATA (RAID)" },
	{ 0x8086, 0xa107, "Intel Sunrise Point-H SATA (RAID)" },
	{ 0x8086, 0xa10f, "Intel Sunrise Point-H SATA (RAID)" },
	// Kaby Lake (7th Gen - 200 Series / Union Point)
	{ 0x8086, 0xa282, "Intel 200 Series PCH SATA (AHCI)" },
	{ 0x8086, 0x9dd3, "Intel 200/300 Series PCH SATA (Mobile)" },
	// Coffee Lake (8th/9th Gen - 300 Series / Cannon Point)
	{ 0x8086, 0xa352, "Intel Cannon Lake PCH SATA (AHCI)" },
	{ 0x8086, 0xa353, "Intel Cannon Lake Mobile PCH SATA (AHCI)" },
	{}
};


dk_keeper_info *gDeviceKeeper;
scsi_for_sim_interface *gSCSI;


status_t
get_device_info(uint16 vendorID, uint16 deviceID, const char **name,
	uint32 *flags)
{
	const device_info *info;
	for (info = kSupportedDevices; info->vendor; info++) {
		if (info->vendor == vendorID && info->device == deviceID) {
			if (name)
				*name = info->name;
			if (flags)
				*flags = info->flags;
			return B_OK;
		}
	}
	return B_ERROR;
}


static status_t
register_sim(dk_node *parent)
{
	int32 id = gDeviceKeeper->create_id(AHCI_ID_GENERATOR);
	if (id < 0)
		return id;

	{
		dk_property attrs[] = {
			// The SCSI bus manager (gSCSIBusDriver) matches nodes with
			// bus="scsi-host" and attaches as a child, creating the
			// scsi_bus_node. Our ahci_sim driver publishes
			// SCSI_SIM_INTERFACE_NAME on this node so the SCSI bus
			// manager can retrieve our ops via walk-up.
			{ KOSM_DEVICE_BUS, B_STRING_TYPE,
				{ .string = "scsi-host" }},
			{ KOSM_LABEL, B_STRING_TYPE,
				{ .string = AHCI_CONTROLLER_PRETTY_NAME }},

			{ SCSI_DESCRIPTION_CONTROLLER_NAME, B_STRING_TYPE,
				{ .string = AHCI_DEVICE_MODULE_NAME }},

			// DMA properties — originally on the device node
			// (ahci_register_device), merged here after collapsing
			// the two-phase registration into a single attach.
			// data must be word-aligned (AHCI PRD DBA bit 0 must be 0)
			{ KOSM_DMA_ALIGNMENT, B_UINT32_TYPE, { .ui32 = 1 }},
			// one S/G block must not cross 64K boundary
			{ KOSM_DMA_BOUNDARY, B_UINT32_TYPE, { .ui32 = 0xffff }},
			// max size of S/G block is 16 bits with zero being 64K
			{ KOSM_DMA_MAX_SEGMENT_BLOCKS, B_UINT32_TYPE,
				{ .ui32 = 0x10000 }},
			{ KOSM_DMA_MAX_SEGMENT_COUNT, B_UINT32_TYPE, { .ui32 = 32 }},
			{ KOSM_DMA_HIGH_ADDRESS, B_UINT64_TYPE,
				{ .ui64 = 0x100000000LL }},
			// max 255 sectors per ATA READ/WRITE DMA command
			{ KOSM_DMA_MAX_TRANSFER_BLOCKS, B_UINT32_TYPE, { .ui32 = 255 }},
			{ KOSM_DMA_BLOCK_SIZE, B_UINT32_TYPE, { .ui32 = 512 }},

			{ SCSI_DEVICE_MAX_TARGET_COUNT, B_UINT32_TYPE, { .ui32 = 33 }},
			{ AHCI_ID_ITEM, B_UINT32_TYPE, { .ui32 = (uint32)id }},

			{ NULL }
		};

		status_t status = gDeviceKeeper->register_node(parent,
			AHCI_SIM_MODULE_NAME, attrs, NULL, NULL);
		if (status < B_OK)
			gDeviceKeeper->free_id(AHCI_ID_GENERATOR, id);

		return status;
	}
}


//	#pragma mark -


static const dk_match_rule sAhciPciMatchRules[] = {
	{ KOSM_DEVICE_BUS,        B_STRING_TYPE, { .string = "pci" } },
	{ KOSM_DEVICE_TYPE,       B_UINT16_TYPE, { .ui16 = PCI_mass_storage } },
	{ KOSM_DEVICE_SUB_TYPE,   B_UINT16_TYPE, { .ui16 = PCI_sata } },
	{ KOSM_DEVICE_INTERFACE,  B_UINT16_TYPE, { .ui16 = PCI_sata_ahci } },
	{ 0 }
};

static const dk_match_dict sAhciPciMatchDict = {
	sAhciPciMatchRules, 0
};


static float
ahci_supports_device(dk_node *parent)
{
	uint16 vendorID;
	uint16 deviceID;
	const char *name;

	FLOW("ahci_supports_device\n");

	if (gDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_VENDOR_ID,
				&vendorID, false) < B_OK
		|| gDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_ID, &deviceID,
				false) < B_OK)
		return 0.0f;

	if (get_device_info(vendorID, deviceID, &name, NULL) < B_OK) {
		/* Match dict already verified PCI class/subclass/interface,
		 * so this is a generic AHCI controller not in our table. */
		TRACE("generic AHCI controller found! vendor 0x%04x, device 0x%04x\n", vendorID, deviceID);
		return 0.8f;
	}

	if (vendorID == PCI_VENDOR_JMICRON) {
		/* JMicron uses the same device ID for SATA and PATA controllers,
		 * check if BAR5 exists to determine if it's an AHCI controller */
		pci_device_ops *pci;
		pci_device *device;
		pci_info info;

		if (gDeviceKeeper->get_interface(parent, PCI_DEVICE_INTERFACE_NAME,
				KOSM_INTERFACE_SELF,
				(const void **)&pci, (void **)&device) != B_OK)
			return 0.0f;
		pci->get_pci_info(device, &info);

		if (info.u.h0.base_register_sizes[5] == 0)
			return 0.0f;
	}

	TRACE("AHCI controller %s found!\n", name);
	return 1.0f;
}


static status_t
ahci_init_driver(dk_node *node, void **_cookie)
{
	TRACE("ahci_init_driver\n");

	*_cookie = node;

	// Register SIM for every controller we find
	return register_sim(node);
}


static void
ahci_uninit_driver(void *cookie)
{
	TRACE("ahci_uninit_driver, cookie %p\n", cookie);
}


static status_t
ahci_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		{
			status_t s = get_module(KOSM_DEVICE_KEEPER_MODULE_NAME,
				(module_info **)&gDeviceKeeper);
			if (s != B_OK)
				return s;
			s = get_module(SCSI_FOR_SIM_MODULE_NAME,
				(module_info **)&gSCSI);
			if (s != B_OK) {
				put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
				return s;
			}
			return B_OK;
		}
		case B_MODULE_UNINIT:
			put_module(SCSI_FOR_SIM_MODULE_NAME);
			put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
			return B_OK;
		default:
			return B_ERROR;
	}
}


dk_driver_info sAHCIDevice = {
	{
		AHCI_DEVICE_MODULE_NAME,
		0,
		ahci_std_ops
	},

	.match  = &sAhciPciMatchDict,
	.probe  = ahci_supports_device,
	.attach = ahci_init_driver,
	.detach = ahci_uninit_driver,
};


module_info *modules[] = {
	(module_info *)&sAHCIDevice,
	(module_info *)&gAHCISimInterface,
	NULL
};
