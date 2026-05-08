/*
 * Copyright 2007-2019, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <device_keeper.h>
#include <Drivers.h>
#include <KernelExport.h>

#include <string.h>
#include <unistd.h>
#include <sys/types.h>


#define DRIVER_NAME "mem"
#define DEVICE_NAME "misc/mem"

#define MEM_DRIVER_MODULE_NAME "drivers/misc/mem/dk_driver_v1"

/* also publish /dev/mem */
#define PUBLISH_DEV_MEM

static dk_keeper_info* sDeviceKeeper;
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


/*	#pragma mark - dk_device_ops / dk_driver_info */


static dk_device_ops sDeviceOps = {
	mem_open,
	mem_close,
	mem_free,
	mem_read,
	mem_write,
	NULL,	/* io */
	NULL,	/* control */
	NULL,	/* select */
	NULL,	/* deselect */
	NULL,	/* device_removed */
};


static const dk_match_rule sMemMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "generic" } },
	{}
};

static const dk_match_dict sMemMatchDict = {
	sMemMatchRules,
	0
};


static float
mem_probe(dk_node* node)
{
	if (sPublished)
		return 0.0;
	return 0.01;
}


static status_t
mem_attach(dk_node* node, void** cookie)
{
	sPublished = true;
	sDeviceKeeper->publish_device(node, DEVICE_NAME, &sDeviceOps);
#ifdef PUBLISH_DEV_MEM
	sDeviceKeeper->publish_device(node, DRIVER_NAME, &sDeviceOps);
#endif
	*cookie = node;
	return B_OK;
}


static void
mem_detach(void* _cookie)
{
	sPublished = false;
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info**)&sDeviceKeeper },
	{ NULL }
};

struct dk_driver_info sMemDriver = {
	.info	= { MEM_DRIVER_MODULE_NAME, 0, NULL },
	.match	= &sMemMatchDict,
	.probe	= mem_probe,
	.attach	= mem_attach,
	.detach	= mem_detach,
	.ops	= &sDeviceOps,
};

module_info* modules[] = {
	(module_info*)&sMemDriver,
	NULL
};
