/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "ECAMPCIController.h"


dk_keeper_info* gDeviceKeeper;
pci_module_info* gPCI;


static pci_controller_ops sPciControllerOps = {
	.read_pci_config = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32* value) {
		return static_cast<ECAMPCIController*>(cookie)
			->ReadConfig(bus, device, function, offset, size, *value);
	},
	.write_pci_config = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 value) {
		return static_cast<ECAMPCIController*>(cookie)
			->WriteConfig(bus, device, function, offset, size, value);
	},
	.get_max_bus_devices = [](void* cookie, int32* count) {
		return static_cast<ECAMPCIController*>(cookie)->GetMaxBusDevices(*count);
	},
	.read_pci_irq = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint8 pin, uint8 *irq) {
		return static_cast<ECAMPCIController*>(cookie)->ReadIrq(bus, device, function, pin, *irq);
	},
	.write_pci_irq = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint8 pin, uint8 irq) {
		return static_cast<ECAMPCIController*>(cookie)->WriteIrq(bus, device, function, pin, irq);
	},
	.get_range = [](void *cookie, uint32 index, pci_resource_range* range) {
		return static_cast<ECAMPCIController*>(cookie)->GetRange(index, range);
	},
	.finalize = [](void *cookie) {
		return static_cast<ECAMPCIController*>(cookie)->Finalize();
	}
};


static dk_driver_info gPciControllerDriver = {
	.info  = { ECAM_PCI_DRIVER_MODULE_NAME, 0, NULL },
	.probe = ECAMPCIController::SupportsDevice,
	.attach = [](dk_node* node, void** driverCookie) {
		status_t status = ECAMPCIController::InitDriver(node,
			*(ECAMPCIController**)driverCookie);
		if (status != B_OK)
			return status;

		// Publish PCI controller interface so the PCI root bus manager
		// can retrieve it via get_interface walk-up.
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
		static_cast<ECAMPCIController*>(driverCookie)->UninitDriver();
	},
};


_EXPORT module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&gDeviceKeeper },
	{ B_PCI_MODULE_NAME, (module_info**)&gPCI },
	{}
};

_EXPORT module_info *modules[] = {
	(module_info *)&gPciControllerDriver,
	NULL
};
