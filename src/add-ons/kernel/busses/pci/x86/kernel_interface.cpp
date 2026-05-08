/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "X86PCIController.h"


dk_keeper_info* gDeviceKeeper;


static pci_controller_ops sPciControllerOps = {
	.read_pci_config = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32* value) {
		return static_cast<X86PCIController*>(cookie)
			->ReadConfig(bus, device, function, offset, size, *value);
	},
	.write_pci_config = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 value) {
		return static_cast<X86PCIController*>(cookie)
			->WriteConfig(bus, device, function, offset, size, value);
	},
	.get_max_bus_devices = [](void* cookie, int32* count) {
		return static_cast<X86PCIController*>(cookie)->GetMaxBusDevices(*count);
	},
	.read_pci_irq = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint8 pin, uint8 *irq) {
		return static_cast<X86PCIController*>(cookie)->ReadIrq(bus, device, function, pin, *irq);
	},
	.write_pci_irq = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint8 pin, uint8 irq) {
		return static_cast<X86PCIController*>(cookie)->WriteIrq(bus, device, function, pin, irq);
	},
	.get_range = [](void *cookie, uint32 index, pci_resource_range* range) {
		return static_cast<X86PCIController*>(cookie)->GetRange(index, range);
	},
	.finalize = [](void *cookie) {
		return static_cast<X86PCIController*>(cookie)->Finalize();
	}
};


static const dk_match_rule sPciRootMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "root"),
	DK_MATCH_END
};

static const dk_match_dict sPciRootMatch = {
	sPciRootMatchRules,
	0
};

static dk_driver_info gPciControllerDriver = {
	.info  = { PCI_X86_DRIVER_MODULE_NAME, 0, NULL },
	.match = &sPciRootMatch,
	.probe = X86PCIController::SupportsDevice,
	.attach = [](dk_node* node, void** driverCookie) {
		status_t status = X86PCIController::InitDriver(node,
			*(X86PCIController**)driverCookie);
		if (status != B_OK)
			return status;

		// Publish the PCI controller interface on this node so the PCI
		// root bus manager can retrieve it via get_interface walk-up.
		gDeviceKeeper->publish_interface(node,
			PCI_CONTROLLER_INTERFACE_NAME, &sPciControllerOps);

		// Register PCI root bus manager as child.
		dk_property attrs[] = {
			DK_PROP_STRING(KOSM_LABEL, "PCI Root Bus"),
			DK_PROP_END
		};
		gDeviceKeeper->register_node(node,
			"bus_managers/pci/root/dk_driver_v1", attrs, NULL, NULL);

		return B_OK;
	},
	.detach = [](void* driverCookie) {
		static_cast<X86PCIController*>(driverCookie)->UninitDriver();
	},
};


_EXPORT module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{}
};

_EXPORT module_info *modules[] = {
	(module_info *)&gPciControllerDriver,
	NULL
};
