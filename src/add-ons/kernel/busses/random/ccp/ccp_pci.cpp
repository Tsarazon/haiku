/*
 * Copyright 2022, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <ByteOrder.h>
#include <bus/PCI.h>

#define DRIVER_NAME "ccp_rng_pci"
#include "ccp.h"


typedef struct {
	ccp_device_info info;
	pci_device_ops* pci;
	pci_device* device;
	pci_info pciinfo;
} ccp_pci_sim_info;


//	#pragma mark - driver hooks


static status_t
ccp_pci_attach(dk_node* node, void** _cookie)
{
	CALLED();

	ccp_pci_sim_info* bus = (ccp_pci_sim_info*)calloc(1,
		sizeof(ccp_pci_sim_info));
	if (bus == NULL)
		return B_NO_MEMORY;

	pci_device_ops* pci;
	pci_device* device;
	status_t status = gDeviceKeeper->get_interface(node,
		PCI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&pci, (void**)&device);
	if (status != B_OK) {
		ERROR("failed to get PCI bus ops: %s\n", strerror(status));
		free(bus);
		return status;
	}

	bus->pci = pci;
	bus->device = device;

	pci_info* pciInfo = &bus->pciinfo;
	pci->get_pci_info(device, pciInfo);

#define BAR_INDEX 2
	bus->info.base_addr = pciInfo->u.h0.base_registers[BAR_INDEX];
	bus->info.map_size = pciInfo->u.h0.base_register_sizes[BAR_INDEX];
	if ((pciInfo->u.h0.base_register_flags[BAR_INDEX] & PCI_address_type)
			== PCI_address_type_64) {
		bus->info.base_addr
			|= (uint64)pciInfo->u.h0.base_registers[BAR_INDEX + 1] << 32;
		bus->info.map_size
			|= (uint64)pciInfo->u.h0.base_register_sizes[BAR_INDEX + 1] << 32;
	}

	if (bus->info.base_addr == 0) {
		ERROR("PCI BAR not assigned\n");
		free(bus);
		return B_ERROR;
	}

	// enable bus master and memory
	uint16 pcicmd = pci->read_pci_config(device, PCI_command, 2);
	pcicmd |= PCI_command_master | PCI_command_memory;
	pci->write_pci_config(device, PCI_command, 2, pcicmd);

	// register child node for the CCP device module
	status = gDeviceKeeper->register_node(node, CCP_DEVICE_MODULE_NAME,
		NULL, NULL, NULL);
	if (status != B_OK) {
		ERROR("failed to register CCP device child: %s\n",
			strerror(status));
		free(bus);
		return status;
	}

	*_cookie = bus;
	return B_OK;
}


static void
ccp_pci_detach(void* cookie)
{
	ccp_pci_sim_info* bus = (ccp_pci_sim_info*)cookie;
	free(bus);
}


//	#pragma mark - match & probe


// Declarative match: narrow to PCI bus, AMD vendor.
// The DkDriverIndex hashes on KOSM_DEVICE_BUS, so this driver
// is only considered for PCI nodes — O(1) instead of O(N_total).
static const dk_match_rule sCcpPciMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "pci"),
	DK_MATCH_UINT16(KOSM_DEVICE_VENDOR_ID, 0x1022),
	DK_MATCH_END
};

static const dk_match_dict sCcpPciMatchDict = {
	sCcpPciMatchRules,
	0   // priority
};


// Imperative probe: check specific device IDs after match dict
// passes. Return > 0 to accept, <= 0 to reject.
static float
ccp_pci_probe(dk_node* node)
{
	uint16 deviceID;
	if (gDeviceKeeper->get_property_uint16(node, KOSM_DEVICE_ID,
			&deviceID, false) != B_OK) {
		return -1.0f;
	}

	switch (deviceID) {
		case 0x1456:	// AMD CCP/PSP - Family 17h
		case 0x1468:	// AMD CCP/PSP - Family 17h Model 10h
		case 0x1486:	// AMD CCP/PSP - Family 17h Model 30h
		case 0x14ca:	// AMD CCP/PSP - Family 19h
		case 0x1537:	// AMD CCP/PSP - Family 16h
		case 0x15df:	// AMD CCP/PSP - Family 17h Raven Ridge
		case 0x1649:	// AMD CCP/PSP - Family 19h Model 50h
			TRACE("CCP RNG device found! vendor 0x1022, device 0x%04x\n",
				deviceID);
			return 0.8f;
		default:
			return 0.0f;
	}
}


//	#pragma mark - module


dk_driver_info gCcpPciDevice = {
	.info   = { CCP_PCI_DEVICE_MODULE_NAME, 0, NULL },
	.match  = &sCcpPciMatchDict,
	.probe  = ccp_pci_probe,
	.attach = ccp_pci_attach,
	.detach = ccp_pci_detach,
};
