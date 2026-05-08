/*
 * Copyright 2025, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */


#include <device_keeper.h>
#include <Drivers.h>
#include <Errors.h>
#include <KernelExport.h>
#include <bus/PCI.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cpu.h>

#include "amd_thermal.h"


#define AMD_THERMAL_MODULE_NAME "drivers/power/amd_thermal/dk_driver_v1"
#define AMD_THERMAL_BASENAME "power/amd_thermal/%d"
#define AMD_THERMAL_PATHID_GENERATOR "amd_thermal/path_id"

static dk_keeper_info* sDeviceKeeper;

//#define TRACE_AMD_THERMAL
#ifdef TRACE_AMD_THERMAL
#	define TRACE(x...) dprintf("amd_thermal: " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf("amd_thermal: " x)


typedef struct amd_thermal_device_info {
	pci_device_ops*	pci;
	pci_device*		pci_cookie;
	dk_node*		node;
	int32			path_id;
	char			publishedPath[B_DEV_NAME_LENGTH];

	uint32			ccdOffset;
	uint32			tctlOffset;
	uint32			ccdValid;
} amd_thermal_device_info;


status_t amd_thermal_control(void* _cookie, uint32 op, void* arg, size_t len);


static uint32
ksmn_read_reg(amd_thermal_device_info* device, uint32 address)
{
	device->pci->write_pci_config(device->pci_cookie,
		AMD_SMN_17H_ADDR, 4, address);
	return device->pci->read_pci_config(device->pci_cookie,
		AMD_SMN_17H_DATA, 4);
}


static void
ksmn_ccd_init(amd_thermal_device_info* device, int ccpCount)
{
	for (int i = 0; i < ccpCount; i++) {
		uint32 reg = ksmn_read_reg(device,
			AMD_SMU_17H_CCD_THM(device->ccdOffset, i));
		if ((reg & CURTMP_CCD_VALID) == 0)
			continue;
		device->ccdValid |= (1 << i);
	}
}


static status_t
amd_thermal_open(void* _cookie, const char* path, int flags, void** cookie)
{
	*cookie = _cookie;
	return B_OK;
}


static status_t
amd_thermal_read(void* _cookie, off_t position, void* buf, size_t* num_bytes)
{
	amd_thermal_device_info* device = (amd_thermal_device_info*)_cookie;
	amd_thermal_type therm_info;
	if (*num_bytes < 1)
		return B_IO_ERROR;

	if (position == 0) {
		char buffer[128];
		amd_thermal_control(device, drvOpGetThermalType, &therm_info, 0);
		int32 copied = snprintf(buffer, sizeof(buffer),
			"  Current Temperature: %" B_PRIu32 ".%" B_PRIu32 " C\n",
			(therm_info.current_temp / 10),
			(therm_info.current_temp % 10));
		copied = min_c((int32)*num_bytes, copied + 1);
		if (user_memcpy((char*)buf, buffer, copied) != B_OK)
			return B_BAD_ADDRESS;
		*num_bytes = copied;
	} else {
		*num_bytes = 0;
	}

	return B_OK;
}


static status_t
amd_thermal_write(void* cookie, off_t position, const void* buffer,
	size_t* num_bytes)
{
	return B_ERROR;
}


status_t
amd_thermal_control(void* _cookie, uint32 op, void* arg, size_t len)
{
	amd_thermal_device_info* device = (amd_thermal_device_info*)_cookie;

	switch (op) {
		case drvOpGetThermalType:
		{
			amd_thermal_type* att = (amd_thermal_type*)arg;
			uint32 data = ksmn_read_reg(device, AMD_SMU_17H_THM);
			uint16 raw = GET_CURTMP(data);
			int32 offset = 0;
			if ((data & CURTMP_17H_RANGE_SELECTION) != 0)
				offset -= CURTMP_17H_RANGE_ADJUST;
			offset -= device->tctlOffset;
			offset *= 100000;
			uint32 value = raw * 125000 + offset;
			att->current_temp = value / 100000;
			return B_OK;
		}
	}
	return B_ERROR;
}


static status_t amd_thermal_close(void* c) { return B_OK; }
static status_t amd_thermal_free(void* c) { return B_OK; }


static dk_device_ops sAmdThermalDeviceOps = {
	amd_thermal_open, amd_thermal_close, amd_thermal_free,
	amd_thermal_read, amd_thermal_write, NULL,
	amd_thermal_control, NULL, NULL, NULL
};


//	#pragma mark - match & driver


static const dk_match_rule sAmdThermalMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "pci"),
	DK_MATCH_UINT16(KOSM_DEVICE_VENDOR_ID, 0x1022),
	DK_MATCH_END
};

static const dk_match_dict sAmdThermalMatchDict = {
	sAmdThermalMatchRules, 0
};


static float
amd_thermal_support(dk_node* parent)
{
	uint16 deviceID;
	if (sDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_ID,
		&deviceID, false) != B_OK) {
		return -1.0f;
	}

	const uint16 devices[] = {
		0x1450, 0x1480, 0x15d0, 0x1630, 0x14a4,
		0x14b5, 0x14d8, 0x14e8, 0x1507, 0
	};
	for (const uint16* d = devices; *d != 0; d++) {
		if (*d == deviceID)
			return 0.6f;
	}
	return 0.0f;
}


static status_t
amd_thermal_attach(dk_node* node, void** _driverCookie)
{
	amd_thermal_device_info* device;
	device = (amd_thermal_device_info*)calloc(1, sizeof(*device));
	if (device == NULL)
		return B_NO_MEMORY;

	device->tctlOffset = 0;
	device->ccdValid = 0;

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

	if (gCPU[0].arch.family == 0x17 || gCPU[0].arch.family == 0x18) {
		switch (gCPU[0].arch.model) {
			case 0x1: case 0x8: case 0x11: case 0x18:
				device->ccdOffset = 0x154;
				ksmn_ccd_init(device, 4);
				break;
			case 0x31: case 0x60: case 0x68: case 0x71:
				device->ccdOffset = 0x154;
				ksmn_ccd_init(device, 8);
				break;
			case 0xa0 ... 0xaf:
				device->ccdOffset = 0x300;
				ksmn_ccd_init(device, 8);
				break;
		}
	} else if (gCPU[0].arch.family == 0x19) {
		switch (gCPU[0].arch.model) {
			case 0x0 ... 0x1: case 0x8: case 0x21: case 0x50 ... 0x5f:
				device->ccdOffset = 0x154;
				ksmn_ccd_init(device, 8);
				break;
			case 0x60 ... 0x6f: case 0x70 ... 0x7f:
				device->ccdOffset = 0x308;
				ksmn_ccd_init(device, 8);
				break;
			case 0x10 ... 0x1f: case 0xa0 ... 0xaf:
				device->ccdOffset = 0x300;
				ksmn_ccd_init(device, 12);
				break;
		}
	}
	TRACE("amd_thermal_attach %p\n", device);

	int path_id = sDeviceKeeper->create_id(AMD_THERMAL_PATHID_GENERATOR);
	if (path_id < 0) {
		ERROR("attach: couldn't create a path_id\n");
		free(device);
		return B_ERROR;
	}
	device->path_id = path_id;

	snprintf(device->publishedPath, sizeof(device->publishedPath),
		AMD_THERMAL_BASENAME, path_id);
	status = sDeviceKeeper->publish_device(node, device->publishedPath,
		&sAmdThermalDeviceOps);
	if (status != B_OK) {
		sDeviceKeeper->free_id(AMD_THERMAL_PATHID_GENERATOR, path_id);
		free(device);
		return status;
	}

	*_driverCookie = device;
	return B_OK;
}


static void
amd_thermal_detach(void* _cookie)
{
	amd_thermal_device_info* device = (amd_thermal_device_info*)_cookie;
	TRACE("amd_thermal_detach %p\n", device);
	if (device == NULL)
		return;

	if (device->publishedPath[0] != '\0')
		sDeviceKeeper->unpublish_device(device->node, device->publishedPath);
	if (device->path_id >= 0)
		sDeviceKeeper->free_id(AMD_THERMAL_PATHID_GENERATOR, device->path_id);

	free(device);
}


static status_t
amd_thermal_std_ops(int32 op, ...)
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


static dk_driver_info amd_thermal_driver = {
	.info   = { AMD_THERMAL_MODULE_NAME, 0, amd_thermal_std_ops },
	.match  = &sAmdThermalMatchDict,
	.probe  = amd_thermal_support,
	.attach = amd_thermal_attach,
	.detach = amd_thermal_detach,
};

module_info* modules[] = {
	(module_info*)&amd_thermal_driver,
	NULL
};
