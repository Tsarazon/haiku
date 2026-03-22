/*
 * Copyright 2007-2019, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <device_manager.h>
#include <Drivers.h>
#include <KernelExport.h>

#include <string.h>
#include <unistd.h>
#include <sys/types.h>


#define DRIVER_NAME "mem"
#define DEVICE_NAME "misc/mem"

#define MEM_DRIVER_MODULE_NAME "drivers/misc/mem/driver_v1"
#define MEM_DEVICE_MODULE_NAME "drivers/misc/mem/device_v1"

/* also publish /dev/mem */
#define PUBLISH_DEV_MEM

static device_manager_info* sDeviceManager;
static bool sPublished = false;

static area_id mem_map_target(off_t position, size_t length, uint32 protection,
	void **virtualAddress);


static status_t
mem_open(void* _info, const char* name, int flags, void** cookie)
{
	*cookie = NULL;
	return B_OK;
}


static status_t
mem_close(void* cookie)
{
	return B_OK;
}


static status_t
mem_free(void* cookie)
{
	return B_OK;
}


static status_t
mem_read(void* cookie, off_t position, void* buffer, size_t* numBytes)
{
	void *virtualAddress;
	area_id area;
	status_t status = B_OK;

	/* check permissions */
	if (getuid() != 0 && geteuid() != 0) {
		*numBytes = 0;
		return EPERM;
	}

	area = mem_map_target(position, *numBytes, B_READ_AREA, &virtualAddress);
	if (area < 0) {
		*numBytes = 0;
		return area;
	}

	if (user_memcpy(buffer, virtualAddress, *numBytes) != B_OK)
		status = B_BAD_ADDRESS;

	delete_area(area);
	return status;
}


static status_t
mem_write(void* cookie, off_t position, const void* buffer, size_t* numBytes)
{
	void *virtualAddress;
	area_id area;
	status_t status = B_OK;

	/* check permissions */
	if (getuid() != 0 && geteuid() != 0) {
		*numBytes = 0;
		return EPERM;
	}

	area = mem_map_target(position, *numBytes, B_WRITE_AREA, &virtualAddress);
	if (area < 0) {
		*numBytes = 0;
		return area;
	}

	if (user_memcpy(virtualAddress, buffer, *numBytes) != B_OK)
		status = B_BAD_ADDRESS;

	delete_area(area);
	return status;
}


static area_id
mem_map_target(off_t position, size_t length, uint32 protection,
	void **virtualAddress)
{
	area_id area;
	phys_addr_t physicalAddress;
	size_t offset;
	size_t size;

	/* SIZE_MAX actually but 2G should be enough anyway */
	if (length > SSIZE_MAX - B_PAGE_SIZE)
		return EINVAL;

	/* the first page address */
	physicalAddress = (phys_addr_t)position & ~((off_t)B_PAGE_SIZE - 1);

	/* offset of target into it */
	offset = position - (off_t)physicalAddress;

	/* size of the whole mapping (page rounded) */
	size = (offset + length + B_PAGE_SIZE - 1) & ~((size_t)B_PAGE_SIZE - 1);
	area = map_physical_memory("mem_driver_temp", physicalAddress, size,
		B_ANY_KERNEL_ADDRESS, protection, virtualAddress);
	if (area < 0)
		return area;

	*virtualAddress = (void*)((addr_t)(*virtualAddress) + offset);
	return area;
}


/*	#pragma mark - driver module API */


static float
mem_supports_device(device_node* parent)
{
	const char* bus;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false))
		return -1;
	if ((strcmp(bus, "root") == 0 || strcmp(bus, "pci") == 0)
		&& !sPublished)
		return 0.01;
	return 0.0;
}


static status_t
mem_register_device(device_node* node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "Physical Memory"} },
		{ NULL }
	};
	return sDeviceManager->register_node(node, MEM_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
mem_init_driver(device_node* node, void** cookie)
{
	*cookie = node;
	return B_OK;
}

static void mem_uninit_driver(void* c) { sPublished = false; }

static status_t
mem_register_child_devices(void* _cookie)
{
	device_node* node = (device_node*)_cookie;
	if (sPublished) return B_OK;
	sPublished = true;
	sDeviceManager->publish_device(node, DEVICE_NAME, MEM_DEVICE_MODULE_NAME);
#ifdef PUBLISH_DEV_MEM
	sDeviceManager->publish_device(node, DRIVER_NAME, MEM_DEVICE_MODULE_NAME);
#endif
	return B_OK;
}

static status_t mem_init_device(void* i, void** c)
{ *c = i; return B_OK; }
static void mem_uninit_device(void* c) {}


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager },
	{ NULL }
};

struct device_module_info sMemDevice = {
	{ MEM_DEVICE_MODULE_NAME, 0, NULL },
	mem_init_device, mem_uninit_device, NULL,
	mem_open, mem_close, mem_free,
	mem_read, mem_write, NULL, NULL,
	NULL, NULL
};

struct driver_module_info sMemDriver = {
	{ MEM_DRIVER_MODULE_NAME, 0, NULL },
	mem_supports_device, mem_register_device,
	mem_init_driver, mem_uninit_driver,
	mem_register_child_devices, NULL, NULL
};

module_info* modules[] = {
	(module_info*)&sMemDriver,
	(module_info*)&sMemDevice,
	NULL
};
