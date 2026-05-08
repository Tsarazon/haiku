/*
 * Copyright 2022, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <ACPI.h>
#include <ByteOrder.h>

extern "C" {
#	include "acpi.h"
}


#define DRIVER_NAME "ccp_rng_acpi"
#include "ccp.h"


typedef struct {
	ccp_device_info info;
	acpi_device_module_info* acpi;
	acpi_device device;
} ccp_acpi_sim_info;


struct ccp_crs {
	uint32	addr_bas;
	uint32	addr_len;
};


static acpi_status
ccp_scan_parse_callback(ACPI_RESOURCE *res, void *context)
{
	struct ccp_crs* crs = (struct ccp_crs*)context;

	if (res->Type == ACPI_RESOURCE_TYPE_FIXED_MEMORY32) {
		crs->addr_bas = res->Data.FixedMemory32.Address;
		crs->addr_len = res->Data.FixedMemory32.AddressLength;
	}

	return B_OK;
}


//	#pragma mark - driver hooks


static status_t
ccp_acpi_attach(dk_node* node, void** _cookie)
{
	CALLED();

	ccp_acpi_sim_info* bus = (ccp_acpi_sim_info*)calloc(1,
		sizeof(ccp_acpi_sim_info));
	if (bus == NULL)
		return B_NO_MEMORY;

	acpi_device_module_info* acpi;
	acpi_device device;
	status_t status = gDeviceKeeper->get_interface(node,
		ACPI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&acpi, (void**)&device);
	if (status != B_OK) {
		ERROR("failed to get ACPI bus ops: %s\n", strerror(status));
		free(bus);
		return status;
	}

	bus->acpi = acpi;
	bus->device = device;

	struct ccp_crs crs = {};
	status = acpi->walk_resources(device, (ACPI_STRING)"_CRS",
		ccp_scan_parse_callback, &crs);
	if (status != B_OK) {
		ERROR("error while getting resources\n");
		free(bus);
		return status;
	}
	if (crs.addr_bas == 0 || crs.addr_len == 0) {
		TRACE("skipping non-configured CCP device\n");
		free(bus);
		return B_BAD_VALUE;
	}

	bus->info.base_addr = crs.addr_bas;
	bus->info.map_size = crs.addr_len;

	// register child node for the CCP device module.
	// Since we are inside attach() (IsAttaching is true), the child
	// is deferred until our cookie is set. ProbePendingChildren will
	// then auto-attach CCP_DEVICE_MODULE_NAME via its dk_driver_v1
	// suffix.
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
ccp_acpi_detach(void* cookie)
{
	ccp_acpi_sim_info* bus = (ccp_acpi_sim_info*)cookie;
	free(bus);
}


//	#pragma mark - match & probe


// Declarative match: narrow to ACPI bus nodes with HID "AMDI0C00".
// When both match dict and probe are present, match dict is checked
// first (fast rejection), then probe() is called for the score.
static const dk_match_rule sCcpAcpiMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "acpi"),
	DK_MATCH_STRING(KOSM_ACPI_DEVICE_HID, "AMDI0C00"),
	DK_MATCH_END
};

static const dk_match_dict sCcpAcpiMatchDict = {
	sCcpAcpiMatchRules,
	0   // priority
};


static float
ccp_acpi_probe(dk_node* node)
{
	TRACE("CCP ACPI device found!\n");
	return 0.6f;
}


//	#pragma mark - module


dk_driver_info gCcpAcpiDevice = {
	.info   = { CCP_ACPI_DEVICE_MODULE_NAME, 0, NULL },
	.match  = &sCcpAcpiMatchDict,
	.probe  = ccp_acpi_probe,
	.attach = ccp_acpi_attach,
	.detach = ccp_acpi_detach,
};
