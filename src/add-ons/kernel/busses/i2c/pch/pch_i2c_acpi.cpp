/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <ACPI.h>
#include <ByteOrder.h>
#include <condition_variable.h>

#include "pch_i2c.h"


struct {
	const char* name;
	pch_version version;
} pch_acpi_devices [] = {
	{"INT33C2", PCH_ATOM},
	{"INT33C3", PCH_ATOM},
	{"INT3332", PCH_ATOM},
	{"INT3433", PCH_ATOM},
	{"INT3442", PCH_ATOM},
	{"INT3443", PCH_ATOM},
	{"INT3444", PCH_ATOM},
	{"INT3445", PCH_ATOM},
	{"INT3446", PCH_ATOM},
	{"INT3447", PCH_ATOM},
	{"80860AAC", PCH_ATOM},
	{"80865AAC", PCH_ATOM},
	{"80860F41", PCH_ATOM},
	{"808662C1", PCH_ATOM},
	{"AMD0010", PCH_ATOM},
	{"AMDI0010", PCH_ATOM},
	{"AMDI0510", PCH_ATOM},
	{"APMC0D0F", PCH_EMAG},
	{NULL, PCH_NONE}
};

typedef struct {
	pch_i2c_sim_info info;
	acpi_device_module_info* acpi;
	acpi_device device;

} pch_i2c_acpi_sim_info;


static status_t
pch_i2c_acpi_set_powerstate(pch_i2c_acpi_sim_info* info, uint8 power)
{
	status_t status = info->acpi->evaluate_method(info->device,
		power == 1 ? "_PS0" : "_PS3", NULL, NULL);
	return status;
}



static acpi_status
pch_i2c_scan_parse_callback(ACPI_RESOURCE *res, void *context)
{
	struct pch_i2c_crs* crs = (struct pch_i2c_crs*)context;

	if (res->Type == ACPI_RESOURCE_TYPE_IRQ) {
		crs->irq = res->Data.Irq.Interrupts[0];
		crs->irq_triggering = res->Data.Irq.Triggering;
		crs->irq_polarity = res->Data.Irq.Polarity;
		crs->irq_shareable = res->Data.Irq.Shareable;
	} else if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
		crs->irq = res->Data.ExtendedIrq.Interrupts[0];
		crs->irq_triggering = res->Data.ExtendedIrq.Triggering;
		crs->irq_polarity = res->Data.ExtendedIrq.Polarity;
		crs->irq_shareable = res->Data.ExtendedIrq.Shareable;
	} else if (res->Type == ACPI_RESOURCE_TYPE_FIXED_MEMORY32) {
		crs->addr_bas = res->Data.FixedMemory32.Address;
		crs->addr_len = res->Data.FixedMemory32.AddressLength;
	}

	return B_OK;
}


//	#pragma mark -


static status_t
acpi_scan_bus(pch_i2c_sim_info* simInfo, dk_node* busNode)
{
	CALLED();
	pch_i2c_acpi_sim_info* bus = (pch_i2c_acpi_sim_info*)simInfo;

	// Store busNode so pch_i2c_scan_bus_callback can register
	// child device nodes on it.
	bus->info.busNode = busNode;

	bus->acpi->walk_namespace(bus->device, ACPI_TYPE_DEVICE, 1,
		pch_i2c_scan_bus_callback, NULL, &bus->info, NULL);

	return B_OK;
}



static status_t
init_device(dk_node* node, void** device_cookie)
{
	CALLED();
	status_t status = B_OK;

	pch_i2c_acpi_sim_info* bus = (pch_i2c_acpi_sim_info*)calloc(1,
		sizeof(pch_i2c_acpi_sim_info));
	if (bus == NULL)
		return B_NO_MEMORY;

	acpi_device_module_info* acpi;
	acpi_device device;
	status = gDeviceKeeper->get_interface(node,
		ACPI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&acpi, (void**)&device);
	if (status != B_OK) {
		ERROR("init_device: get_interface(acpi) failed: %s\n",
			strerror(status));
		free(bus);
		return status;
	}

	{
		char name[64];
		if (gDeviceKeeper->get_property_string(node,
				KOSM_ACPI_DEVICE_HID, name, sizeof(name),
				NULL, true) == B_OK) {
			for (size_t dev = 0; pch_acpi_devices[dev].name != NULL; dev++) {
				if (strcmp(name, pch_acpi_devices[dev].name) == 0) {
					bus->info.version = pch_acpi_devices[dev].version;
					break;
				}
			}
		}
	}

	bus->acpi = acpi;
	bus->device = device;
	bus->info.driver_node = node;
	bus->info.scan_bus = acpi_scan_bus;

	// Attach devices for I2C resources
	struct pch_i2c_crs crs;
	memset(&crs, 0, sizeof(crs));
	status = acpi->walk_resources(device, (ACPI_STRING)"_CRS",
		pch_i2c_scan_parse_callback, &crs);
	if (status != B_OK) {
		ERROR("Error while getting I2C devices\n");
		free(bus);
		return status;
	}
	if (crs.addr_bas == 0 || crs.addr_len == 0) {
		TRACE("skipping non configured I2C devices\n");
		free(bus);
		return B_BAD_VALUE;
	}

	bus->info.base_addr = crs.addr_bas;
	bus->info.map_size = crs.addr_len;
	bus->info.irq = crs.irq;

	pch_i2c_acpi_set_powerstate(bus, 1);

	// Register the controller child. Auto-attach by module name — the
	// sPchI2cControllerDriver probes, walks up and maps MMIO via
	// bus->info. The I2C bus manager (I2C_BUS_MODULE_NAME) is then
	// auto-attached as a child of that controller node.
	{
		dk_property attrs[] = {
			DK_PROP_STRING(KOSM_LABEL, "PCH I2C Controller"),
			DK_PROP_UINT32(KOSM_DEVICE_FLAGS, KOSM_FIND_MULTIPLE_CHILDREN),
			DK_PROP_END
		};

		status = gDeviceKeeper->register_node(node, PCH_I2C_SIM_MODULE_NAME,
			attrs, NULL, NULL);
		if (status != B_OK) {
			ERROR("init_device: register_node(sim) failed: %s\n",
				strerror(status));
			pch_i2c_acpi_set_powerstate(bus, 0);
			free(bus);
			return status;
		}
	}

	*device_cookie = bus;
	return B_OK;
}


static void
uninit_device(void* device_cookie)
{
	pch_i2c_acpi_sim_info* bus = (pch_i2c_acpi_sim_info*)device_cookie;
	free(bus);
}


static float
supports_device(dk_node* node)
{
	CALLED();

	// bus=acpi already filtered by the match dict. Extra check: must
	// be an ACPI_TYPE_DEVICE (not a processor, power resource, etc.)
	// with an HID we recognise.
	uint32 device_type;
	if (gDeviceKeeper->get_property_uint32(node, KOSM_ACPI_DEVICE_TYPE,
			&device_type, false) != B_OK
		|| device_type != ACPI_TYPE_DEVICE) {
		return 0.0f;
	}

	char name[64];
	if (gDeviceKeeper->get_property_string(node, KOSM_ACPI_DEVICE_HID,
			name, sizeof(name), NULL, false) != B_OK) {
		return 0.0f;
	}

	for (size_t device = 0; pch_acpi_devices[device].name != NULL;
			device++) {
		if (strcmp(name, pch_acpi_devices[device].name) == 0) {
			TRACE("PCH I2C device found! name %s\n", name);
			return 0.6f;
		}
	}

	return 0.0f;
}


//	#pragma mark -


static const dk_match_rule sPchI2cAcpiMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "acpi"),
	DK_MATCH_END
};

static const dk_match_dict sPchI2cAcpiMatchDict = {
	sPchI2cAcpiMatchRules,
	0
};


dk_driver_info gPchI2cAcpiDevice = {
	.info = {
		PCH_I2C_ACPI_DEVICE_MODULE_NAME,
		0,
		NULL
	},
	.match	= &sPchI2cAcpiMatchDict,
	.probe	= supports_device,
	.attach	= init_device,
	.detach	= uninit_device,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};
