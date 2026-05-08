/* Copyright (c) 2003-2011
 * Stefano Ceccherini <stefano.ceccherini@gmail.com>. All rights reserved.
 */
#include "debug.h"
#include <Debug.h>

#include <KernelExport.h>
#include <Errors.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wb840.h"
#include "device.h"
#include "driver.h"

#define MAX_CARDS 4

#define WB840_DRIVER_MODULE_NAME "drivers/network/wb840/dk_driver_v1"
#define WB840_DEVICE_ID_GENERATOR "wb840/device_id"

pci_device_ops* gPci;
pci_device* gPciDev;
dk_keeper_info* gDeviceKeeper;
char* gDevNameList[MAX_CARDS + 1];
pci_info* gDevList[MAX_CARDS];


static bool __attribute__((unused))
probe(pci_info* item)
{
	if ((item->vendor_id == WB_VENDORID && item->device_id == WB_DEVICEID_840F)
			|| (item->vendor_id == CP_VENDORID && item->device_id == CP_DEVICEID_RL100))
		return true;
	return false;
}


//	#pragma mark - driver module API


static float
wb840_supports_device(dk_node *parent)
{
	char bus[64];
	uint16 vendorId, deviceId;

	if (gDeviceKeeper->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false))
		return -1;
	if (strcmp(bus, "pci"))
		return 0.0;

	if (gDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_VENDOR_ID,
			&vendorId, false) != B_OK)
		return 0.0;
	if (gDeviceKeeper->get_property_uint16(parent, KOSM_DEVICE_ID,
			&deviceId, false) != B_OK)
		return 0.0;

	if ((vendorId == WB_VENDORID && deviceId == WB_DEVICEID_840F)
		|| (vendorId == CP_VENDORID && deviceId == CP_DEVICEID_RL100)) {
		LOG((DEVICE_NAME ": WB840 device found!\n"));
		return 0.6;
	}

	return 0.0;
}



/* device hooks from device.c */
extern status_t wb840_open(void*, const char*, int, void**);
extern status_t wb840_close(void*);
extern status_t wb840_free(void*);
extern status_t wb840_read(void*, off_t, void*, size_t*);
extern status_t wb840_write(void*, off_t, const void*, size_t*);
extern status_t wb840_control(void*, uint32, void*, size_t);

static dk_device_ops sWb840DeviceOps = {
	wb840_open,
	wb840_close,
	wb840_free,
	wb840_read,
	wb840_write,
	NULL,	/* io */
	wb840_control,
	NULL,	/* select */
	NULL,	/* deselect */
	NULL,	/* device_removed */
};


static status_t
wb840_init_driver(dk_node *node, void **cookie)
{
	LOG((DEVICE_NAME ": init_driver\n"));

#ifdef DEBUG
	set_dprintf_enabled(true);
#endif

	/* Get PCI bus ops from parent bus manager */
	status_t status = gDeviceKeeper->get_interface(node,
		PCI_DEVICE_INTERFACE_NAME, KOSM_INTERFACE_ANCESTORS,
		(const void **)&gPci, (void **)&gPciDev);
	if (status != B_OK)
		return status;

	pci_info *item = (pci_info *)malloc(sizeof(pci_info));
	if (item == NULL)
		return B_NO_MEMORY;

	gPci->get_pci_info(gPciDev, item);

	/* enable bus mastering */
	uint32 cmd = gPci->read_pci_config(gPciDev, PCI_command, 2);
	gPci->write_pci_config(gPciDev, PCI_command, 2,
		cmd | PCI_command_master);

	dprintf(DEVICE_NAME ": revision = %x\n", item->revision);

	gDevList[0] = item;
	gDevList[1] = NULL;

	char devName[64];
	sprintf(devName, DEVICE_NAME "/%d", 0);
	gDevNameList[0] = strdup(devName);
	gDevNameList[1] = NULL;

	LOG((DEVICE_NAME ": enabled %s\n", devName));

	status = gDeviceKeeper->publish_device(node, devName, &sWb840DeviceOps);
	if (status != B_OK) {
		free(gDevNameList[0]);
		gDevNameList[0] = NULL;
		free(item);
		gDevList[0] = NULL;
		return status;
	}

	*cookie = node;
	return B_OK;
}


static void
wb840_uninit_driver(void *_cookie)
{
	dk_node *node = (dk_node *)_cookie;
	int32 i = 0;

	LOG((DEVICE_NAME ": uninit_driver()\n"));
	while (gDevNameList[i] != NULL) {
		gDeviceKeeper->unpublish_device(node, gDevNameList[i]);
		free(gDevList[i]);
		free(gDevNameList[i]);
		gDevList[i] = NULL;
		gDevNameList[i] = NULL;
		i++;
	}
}


//	#pragma mark - module exports


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&gDeviceKeeper },
	{ NULL }
};

static dk_driver_info sWb840Driver = {
	.info   = { WB840_DRIVER_MODULE_NAME, 0, NULL },
	.probe  = wb840_supports_device,
	.attach = wb840_init_driver,
	.detach = wb840_uninit_driver,
	.ops    = &sWb840DeviceOps,
};

module_info *modules[] = {
	(module_info *)&sWb840Driver,
	NULL
};
