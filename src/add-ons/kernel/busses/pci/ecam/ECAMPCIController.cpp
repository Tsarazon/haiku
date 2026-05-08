/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "ECAMPCIController.h"
#include <acpi.h>

#include <AutoDeleterDrivers.h>

#include "acpi_irq_routing_table.h"

#include <string.h>
#include <new>


//#pragma mark - driver


float
ECAMPCIController::SupportsDevice(dk_node* parent)
{
	char bus[64];
	status_t status = gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
		sizeof(bus), NULL, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(bus, "fdt") == 0) {
		char compatible[128];
		status = gDeviceKeeper->get_property_string(parent, "fdt/compatible", compatible,
			sizeof(compatible), NULL, false);
		if (status < B_OK)
			return -1.0f;

		if (strcmp(compatible, "pci-host-ecam-generic") != 0)
			return 0.0f;

		return 1.0f;
	}

	if (strcmp(bus, "acpi") == 0) {
		char hid[64];
		if (gDeviceKeeper->get_property_string(parent, KOSM_ACPI_DEVICE_HID, hid,
				sizeof(hid), NULL, false) < B_OK)
			return -1.0f;

		if (strcmp(hid, "PNP0A03") != 0 && strcmp(hid, "PNP0A08") != 0)
			return 0.0f;

		return 1.0f;
	}

	return 0.0f;
}


#if !defined(ECAM_PCI_CONTROLLER_NO_INIT)
status_t
ECAMPCIController::InitDriver(dk_node* node, ECAMPCIController*& outDriver)
{
	dprintf("+ECAMPCIController::InitDriver()\n");
	DkNodePutter<&gDeviceKeeper> parentNode(gDeviceKeeper->get_parent_node(node));

	ObjectDeleter<ECAMPCIController> driver;

	char bus[64];
	CHECK_RET(gDeviceKeeper->get_property_string(parentNode.Get(), KOSM_DEVICE_BUS, bus,
		sizeof(bus), NULL, false));
	if (strcmp(bus, "fdt") == 0)
		driver.SetTo(new(std::nothrow) ECAMPCIControllerFDT());
	else if (strcmp(bus, "acpi") == 0)
		driver.SetTo(new(std::nothrow) ECAMPCIControllerACPI());
	else
		return B_ERROR;

	if (!driver.IsSet())
		return B_NO_MEMORY;

	driver->fNode = node;

	CHECK_RET(driver->ReadResourceInfo());
	outDriver = driver.Detach();

	dprintf("-ECAMPCIController::InitDriver()\n");
	return B_OK;
}


void
ECAMPCIController::UninitDriver()
{
	delete this;
}
#endif


/** Compute the virtual address for accessing a PCI ECAM register.
 *
 * \returns NULL if the address is out of bounds.
 */
addr_t
ECAMPCIController::ConfigAddress(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	PciAddressEcam address {
		.offset = offset,
		.function = function,
		.device = device,
		.bus = bus
	};
	if ((ROUNDDOWN(address.val, 4) + 4) > fRegsLen)
		return 0;

	return (addr_t)fRegs + address.val;
}


//#pragma mark - PCI controller


status_t
ECAMPCIController::ReadConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32& value)
{
	addr_t address = ConfigAddress(bus, device, function, offset);
	if (address == 0)
		return ERANGE;

	switch (size) {
		case 1: value = *(vuint8*)address; break;
		case 2: value = *(vuint16*)address; break;
		case 4: value = *(vuint32*)address; break;
		default:
			return B_BAD_VALUE;
	}

	return B_OK;
}


status_t
ECAMPCIController::WriteConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	addr_t address = ConfigAddress(bus, device, function, offset);
	if (address == 0)
		return ERANGE;

	switch (size) {
		case 1: *(vuint8*)address = value; break;
		case 2: *(vuint16*)address = value; break;
		case 4: *(vuint32*)address = value; break;
		default:
			return B_BAD_VALUE;
	}

	return B_OK;
}


status_t
ECAMPCIController::GetMaxBusDevices(int32& count)
{
	count = 32;
	return B_OK;
}


status_t
ECAMPCIController::ReadIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8& irq)
{
	return B_UNSUPPORTED;
}


status_t
ECAMPCIController::WriteIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8 irq)
{
	return B_UNSUPPORTED;
}


status_t
ECAMPCIController::GetRange(uint32 index, pci_resource_range* range)
{
	if (index >= (uint32)fResourceRanges.Count())
		return B_BAD_INDEX;

	*range = fResourceRanges[index];
	return B_OK;
}
