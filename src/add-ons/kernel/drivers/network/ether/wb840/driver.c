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

#define WB840_DRIVER_MODULE_NAME "drivers/network/wb840/driver_v1"
#define WB840_DEVICE_MODULE_NAME "drivers/network/wb840/device_v1"
#define WB840_DEVICE_ID_GENERATOR "wb840/device_id"

pci_device_module_info* gPci;
pci_device* gPciDev;
device_manager_info* gDeviceManager;
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
wb840_supports_device(device_node *parent)
{
	const char *bus;
	uint16 vendorId, deviceId;

	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;
	if (strcmp(bus, "pci"))
		return 0.0;

	if (gDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID,
			&vendorId, false) != B_OK)
		return 0.0;
	if (gDeviceManager->get_attr_uint16(parent, B_DEVICE_ID,
			&deviceId, false) != B_OK)
		return 0.0;

	if ((vendorId == WB_VENDORID && deviceId == WB_DEVICEID_840F)
		|| (vendorId == CP_VENDORID && deviceId == CP_DEVICEID_RL100)) {
		LOG((DEVICE_NAME ": WB840 device found!\n"));
		return 0.6;
	}

	return 0.0;
}


static status_t
wb840_register_device(device_node *node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ .string = "WinBond W89C840F Ethernet" } },
		{ NULL }
	};

	return gDeviceManager->register_node(node, WB840_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
wb840_init_driver(device_node *node, void **cookie)
{
	LOG((DEVICE_NAME ": init_driver\n"));

#ifdef DEBUG
	set_dprintf_enabled(true);
#endif

	/* Get pci_device_module_info from parent node */
	device_node *parent = gDeviceManager->get_parent_node(node);
	gDeviceManager->get_driver(parent, (driver_module_info **)&gPci,
		(void **)&gPciDev);
	gDeviceManager->put_node(parent);

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

	*cookie = node;
	return B_OK;
}


static void
wb840_uninit_driver(void *_cookie)
{
	int32 i = 0;

	LOG((DEVICE_NAME ": uninit_driver()\n"));
	while (gDevNameList[i] != NULL) {
		free(gDevList[i]);
		free(gDevNameList[i]);
		gDevList[i] = NULL;
		gDevNameList[i] = NULL;
		i++;
	}
}


static status_t
wb840_register_child_devices(void *_cookie)
{
	device_node *node = (device_node *)_cookie;

	if (gDevNameList[0] == NULL)
		return B_ERROR;

	return gDeviceManager->publish_device(node, gDevNameList[0],
		WB840_DEVICE_MODULE_NAME);
}


//	#pragma mark - device init/uninit


static status_t
wb840_init_device(void *_info, void **_cookie)
{
	*_cookie = _info;
	return B_OK;
}


static void
wb840_uninit_device(void *_cookie)
{
}


//	#pragma mark - module exports


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&gDeviceManager },
	{ NULL }
};

/* device hooks from device.c */
extern status_t wb840_open(void*, const char*, int, void**);
extern status_t wb840_close(void*);
extern status_t wb840_free(void*);
extern status_t wb840_read(void*, off_t, void*, size_t*);
extern status_t wb840_write(void*, off_t, const void*, size_t*);
extern status_t wb840_control(void*, uint32, void*, size_t);

struct device_module_info sWb840Device = {
	{
		WB840_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	wb840_init_device,
	wb840_uninit_device,
	NULL,	/* device_removed */

	wb840_open,
	wb840_close,
	wb840_free,
	wb840_read,
	wb840_write,
	NULL,	/* io */
	wb840_control,

	NULL,	/* select */
	NULL,	/* deselect */
};

struct driver_module_info sWb840Driver = {
	{
		WB840_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	wb840_supports_device,
	wb840_register_device,
	wb840_init_driver,
	wb840_uninit_driver,
	wb840_register_child_devices,
	NULL,	/* rescan */
	NULL,	/* removed */
};

module_info *modules[] = {
	(module_info *)&sWb840Driver,
	(module_info *)&sWb840Device,
	NULL
};
