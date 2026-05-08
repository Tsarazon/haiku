/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <AreaKeeper.h>
#include <device_keeper.h>
#include <Drivers.h>
#include <Errors.h>
#include <KernelExport.h>
#include <bus/PCI.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pch_thermal.h"


#define PCH_THERMAL_MODULE_NAME "drivers/power/pch_thermal/dk_driver_v1"
#define PCH_THERMAL_BASENAME "power/pch_thermal/%d"
#define PCH_THERMAL_PATHID_GENERATOR "pch_thermal/path_id"

static dk_keeper_info* sDeviceKeeper;

//#define TRACE_PCH_THERMAL
#ifdef TRACE_PCH_THERMAL
#	define TRACE(x...) dprintf("pch_thermal: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("pch_thermal: " x)


#define write8(address, data)  (*((volatile uint8*)(address)) = (data))
#define read8(address)         (*((volatile uint8*)(address)))
#define write16(address, data) (*((volatile uint16*)(address)) = (data))
#define read16(address)        (*((volatile uint16*)(address)))
#define write32(address, data) (*((volatile uint32*)(address)) = (data))
#define read32(address)        (*((volatile uint32*)(address)))


typedef struct pch_thermal_device_info {
	pci_device_ops*	pci;
	pci_device*		pci_cookie;
	dk_node*		node;
	int32			path_id;
	char			publishedPath[B_DEV_NAME_LENGTH];

	addr_t			registers;
	area_id			registers_area;

	uint32			criticalTemp;
	uint32			hotTemp;
	uint32			passiveTemp;
} pch_thermal_device_info;


status_t pch_thermal_control(void* _cookie, uint32 op, void* arg,
	size_t len);


static status_t
pch_thermal_open(void* _cookie, const char* path, int flags, void** cookie)
{
	*cookie = _cookie;
	return B_OK;
}


static status_t
pch_thermal_read(void* _cookie, off_t position, void* buf,
	size_t* num_bytes)
{
	pch_thermal_device_info* device = (pch_thermal_device_info*)_cookie;
	pch_thermal_type therm_info;
	if (*num_bytes < 1)
		return B_IO_ERROR;

	if (position == 0) {
		char buffer[1024];
		size_t maxLength = sizeof(buffer);
		size_t totalCopied = 0;
		char* str = buffer;

		pch_thermal_control(device, drvOpGetThermalType, &therm_info, 0);

		int32 copied = snprintf(str, maxLength,
			"  Critical Temperature: %" B_PRIu32 ".%" B_PRIu32 " C\n",
			(therm_info.critical_temp / 10),
			(therm_info.critical_temp % 10));
		maxLength -= copied;
		str += copied;
		totalCopied += copied;

		copied = snprintf(str, maxLength,
			"  Current Temperature: %" B_PRIu32 ".%" B_PRIu32 " C\n",
			(therm_info.current_temp / 10),
			(therm_info.current_temp % 10));

		if (therm_info.hot_temp > 0) {
			maxLength -= copied;
			str += copied;
			totalCopied += copied;
			copied = snprintf(str, maxLength,
				"  Hot Temperature: %" B_PRIu32 ".%" B_PRIu32 " C\n",
				(therm_info.hot_temp / 10),
				(therm_info.hot_temp % 10));
		}

		totalCopied += copied;
		totalCopied = min_c(*num_bytes, totalCopied + 1);
		if (user_memcpy((char*)buf, buffer, totalCopied) != B_OK)
			return B_BAD_ADDRESS;
		*num_bytes = totalCopied;
	} else {
		*num_bytes = 0;
	}

	return B_OK;
}


static status_t
pch_thermal_write(void* cookie, off_t position, const void* buffer,
	size_t* num_bytes)
{
	return B_ERROR;
}


status_t
pch_thermal_control(void* _cookie, uint32 op, void* arg, size_t len)
{
	pch_thermal_device_info* device = (pch_thermal_device_info*)_cookie;

	switch (op) {
		case drvOpGetThermalType:
		{
			pch_thermal_type* att = (pch_thermal_type*)arg;
			att->critical_temp = device->criticalTemp;
			att->hot_temp = device->hotTemp;

			uint16 temp = read16(device->registers + PCH_THERMAL_TEMP);
			temp = (temp >> PCH_THERMAL_TEMP_TSR_SHIFT)
				& PCH_THERMAL_TEMP_TSR_MASK;
			att->current_temp = (uint32)temp * 10 / 2 - 500;

			return B_OK;
		}
	}
	return B_ERROR;
}


static status_t pch_thermal_close(void* c) { return B_OK; }
static status_t pch_thermal_free(void* c) { return B_OK; }


static dk_device_ops sPchThermalDeviceOps = {
	pch_thermal_open, pch_thermal_close, pch_thermal_free,
	pch_thermal_read, pch_thermal_write, NULL,
	pch_thermal_control, NULL, NULL, NULL
};


//	#pragma mark - match & driver


static const dk_match_rule sPchThermalMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "pci"),
	DK_MATCH_UINT16(KOSM_DEVICE_VENDOR_ID, 0x8086),
	DK_MATCH_END
};

static const dk_match_dict sPchThermalMatchDict = {
	sPchThermalMatchRules, 0
};


static float
pch_thermal_support(dk_node* parent)
{
	uint16 deviceID;
	if (sDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_ID,
		&deviceID, false) != B_OK) {
		return -1.0f;
	}

	const uint16 devices[] = {
		0x9c24, 0x8c24, 0x9ca4, 0x9d31, 0xa131, 0x9df9,
		0xa379, 0x06f9, 0x02f9, 0xa1b1, 0x8d24, 0
	};
	for (const uint16* d = devices; *d != 0; d++) {
		if (*d == deviceID)
			return 0.6f;
	}
	return 0.0f;
}


static status_t
pch_thermal_attach(dk_node* node, void** _driverCookie)
{
	pch_thermal_device_info* device;
	device = (pch_thermal_device_info*)calloc(1, sizeof(*device));
	if (device == NULL)
		return B_NO_MEMORY;

	status_t status = sDeviceKeeper->get_interface(node,
		PCI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void**)&device->pci, (void**)&device->pci_cookie);
	if (status != B_OK) {
		free(device);
		return status;
	}
	device->node = node;
	device->path_id = -1;
	device->publishedPath[0] = '\0';

	struct pci_info info;
	device->pci->get_pci_info(device->pci_cookie, &info);

	// map the registers (64-bit BAR support)
	phys_addr_t physicalAddress = info.u.h0.base_registers[0];
	if ((info.u.h0.base_register_flags[0] & PCI_address_type)
			== PCI_address_type_64) {
		physicalAddress |= (uint64)info.u.h0.base_registers[1] << 32;
	}

	size_t mapSize = info.u.h0.base_register_sizes[0];

	AreaKeeper mmioMapper;
	device->registers_area = mmioMapper.Map("intel PCH thermal mmio",
		physicalAddress, mapSize, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&device->registers);

	if (mmioMapper.InitCheck() < B_OK) {
		ERROR("could not map memory I/O!\n");
		status = device->registers_area;
		goto err;
	}

	{
		bool enabled = false;
		uint8 tsel = read8(device->registers + PCH_THERMAL_TSEL);
		if ((tsel & PCH_THERMAL_TSEL_ETS) != 0)
			enabled = true;
		if (!enabled) {
			if ((tsel & PCH_THERMAL_TSEL_PLDB) != 0) {
				status = B_DEVICE_NOT_FOUND;
				goto err;
			}
			write8(device->registers + PCH_THERMAL_TSEL,
				tsel | PCH_THERMAL_TSEL_ETS);
			if ((tsel & PCH_THERMAL_TSEL_ETS) == 0) {
				status = B_DEVICE_NOT_FOUND;
				goto err;
			}
		}

		uint16 ctt = read16(device->registers + PCH_THERMAL_CTT);
		ctt = (ctt >> PCH_THERMAL_CTT_CTRIP_SHIFT)
			& PCH_THERMAL_CTT_CTRIP_MASK;
		device->criticalTemp = (ctt != 0)
			? (uint32)ctt * 10 / 2 - 500 : 0;

		uint16 phl = read16(device->registers + PCH_THERMAL_PHL);
		phl = (phl >> PCH_THERMAL_PHL_PHLL_SHIFT)
			& PCH_THERMAL_PHL_PHLL_MASK;
		device->hotTemp = (phl != 0)
			? (uint32)phl * 10 / 2 - 500 : 0;
	}

	TRACE("pch_thermal_attach %p\n", device);

	{
		int path_id = sDeviceKeeper->create_id(
			PCH_THERMAL_PATHID_GENERATOR);
		if (path_id < 0) {
			ERROR("attach: couldn't create a path_id\n");
			status = B_ERROR;
			goto err;
		}
		device->path_id = path_id;

		snprintf(device->publishedPath, sizeof(device->publishedPath),
			PCH_THERMAL_BASENAME, path_id);
		status = sDeviceKeeper->publish_device(node, device->publishedPath,
			&sPchThermalDeviceOps);
		if (status != B_OK) {
			sDeviceKeeper->free_id(PCH_THERMAL_PATHID_GENERATOR, path_id);
			goto err;
		}
	}

	mmioMapper.Detach();
	*_driverCookie = device;
	return B_OK;

err:
	free(device);
	return status;
}


static void
pch_thermal_detach(void* _cookie)
{
	pch_thermal_device_info* device = (pch_thermal_device_info*)_cookie;
	TRACE("pch_thermal_detach %p\n", device);
	if (device == NULL)
		return;

	if (device->publishedPath[0] != '\0')
		sDeviceKeeper->unpublish_device(device->node, device->publishedPath);
	if (device->path_id >= 0)
		sDeviceKeeper->free_id(PCH_THERMAL_PATHID_GENERATOR, device->path_id);

	delete_area(device->registers_area);
	free(device);
}


static status_t
pch_thermal_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return get_module(KOSM_DEVICE_KEEPER_MODULE_NAME,
				(module_info**)&sDeviceKeeper);
		case B_MODULE_UNINIT:
			put_module(KOSM_DEVICE_KEEPER_MODULE_NAME);
			return B_OK;
		default:
			return B_ERROR;
	}
}


static dk_driver_info pch_thermal_driver = {
	.info   = { PCH_THERMAL_MODULE_NAME, 0, pch_thermal_std_ops },
	.match  = &sPchThermalMatchDict,
	.probe  = pch_thermal_support,
	.attach = pch_thermal_attach,
	.detach = pch_thermal_detach,
};

module_info* modules[] = {
	(module_info*)&pch_thermal_driver,
	NULL
};
