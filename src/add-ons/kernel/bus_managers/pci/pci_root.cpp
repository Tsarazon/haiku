/*
 * Copyright 2005-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2003, Marcus Overhagen. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */


#include <device_keeper.h>
#include <bus/PCI.h>
#include <drivers/ACPI.h>
#include <drivers/bus/FDT.h>

#include <string.h>

#include <AutoDeleterDrivers.h>

#include "pci_private.h"
#include "pci.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


// name of PCI root module
#define PCI_ROOT_MODULE_NAME "bus_managers/pci/root/dk_driver_v1"


static void
pci_root_traverse(dk_node* node, PCIBus* bus)
{
	for (PCIDev* dev = bus->child; dev != NULL; dev = dev->next) {
		const pci_info& info = dev->info;

		dk_property attrs[] = {
			// info about device
			DK_PROP_STRING(KOSM_DEVICE_BUS, "pci"),

			// location on PCI bus
			DK_PROP_UINT8(KOSM_PCI_DEVICE_DOMAIN, dev->domain),
			DK_PROP_UINT8(KOSM_PCI_DEVICE_BUS, dev->bus),
			DK_PROP_UINT8(KOSM_PCI_DEVICE_DEVICE, info.device),
			DK_PROP_UINT8(KOSM_PCI_DEVICE_FUNCTION, info.function),

			// info about the device
			DK_PROP_UINT16(KOSM_DEVICE_VENDOR_ID, info.vendor_id),
			DK_PROP_UINT16(KOSM_DEVICE_ID, info.device_id),

			DK_PROP_UINT16(KOSM_DEVICE_TYPE, info.class_base),
			DK_PROP_UINT16(KOSM_DEVICE_SUB_TYPE, info.class_sub),
			DK_PROP_UINT16(KOSM_DEVICE_INTERFACE, info.class_api),

			DK_PROP_UINT32(KOSM_DEVICE_FLAGS, KOSM_FIND_CHILD_ON_DEMAND),
			DK_PROP_END
		};

		gDeviceKeeper->register_node(node, PCI_DEVICE_MODULE_NAME, attrs,
			NULL, NULL);

		if (dev->child != NULL)
			pci_root_traverse(node, dev->child);
	}
}


static status_t
pci_root_attach(dk_node* node, void** _cookie)
{
	// Retrieve pci_controller_ops published by the PCI host
	// bridge driver (x86, ecam, designware) on our parent node.
	DkNodePutter<&gDeviceKeeper> pciHostNode(
		gDeviceKeeper->get_parent_node(node));

	pci_controller_ops* pciHostModule;
	void* pciHostDev;
	CHECK_RET(gDeviceKeeper->get_interface(pciHostNode.Get(),
		PCI_CONTROLLER_INTERFACE_NAME, KOSM_INTERFACE_SELF,
		(const void**)&pciHostModule, &pciHostDev));

	module_info* module;
	status_t res = get_module(B_PCI_MODULE_NAME, &module);
	if (res < B_OK)
		return res;

	domain_data* domainData = NULL;
	CHECK_RET(gPCI->AddController(pciHostModule, pciHostDev, node,
		&domainData));

	// Publish the PCI root interface so internal pci.cpp helpers can
	// retrieve read/write_pci_config from the root node.
	gDeviceKeeper->publish_interface(node, PCI_ROOT_INTERFACE_NAME,
		&gPCIRootInterface);

	// Enumerate child PCI devices as fresh child nodes.
	pci_root_traverse(node, domainData->bus);

	*_cookie = domainData;
	return B_OK;
}


static void
pci_root_detach(void* cookie)
{
	// TODO: tear down domain_data
}


// pci_root must be attached under a PCI host controller node (x86 PCI
// host, ecam, designware). The host controller publishes
// PCI_CONTROLLER_INTERFACE_NAME; we retrieve it in attach().
// Matching is done via auto-attach from host controller register_node
// rather than declarative match rules.
struct dk_driver_info gPCIRootDriver = {
	.info		= { PCI_ROOT_MODULE_NAME, 0, NULL },
	.attach		= pci_root_attach,
	.detach		= pci_root_detach,
	.node_flags	= KOSM_KEEP_DRIVER_LOADED | KOSM_FIND_MULTIPLE_CHILDREN,
};


struct pci_root_module_info gPCIRootInterface = {
	&pci_read_config,
	&pci_write_config,
};
