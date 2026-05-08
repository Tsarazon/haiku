/*
 * Copyright 2005, Oscar Lesta. All rights reserved.
 * Copyright 2018-2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "poke.h"

#include <device_keeper.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <ISA.h>
#include <bus/PCI.h>

#include <malloc.h>
#include <string.h>

#include <team.h>
#include <vm/vm.h>

#if defined(__i386__) || defined(__x86_64__)
#include <thread.h>
#endif


#define POKE_DRIVER_MODULE_NAME "drivers/misc/poke/dk_driver_v1"


static status_t poke_open(void*, const char*, int, void**);
static status_t poke_close(void*);
static status_t poke_free(void*);
static status_t poke_control(void*, uint32, void*, size_t);
static status_t poke_read(void*, off_t, void*, size_t*);
static status_t poke_write(void*, off_t, const void*, size_t*);


static isa_module_info* isa;
static pci_module_info* pci;
static dk_keeper_info* sDeviceKeeper;
static bool sPublished = false;

typedef struct {
	dk_node* node;
} poke_driver_info;


//	#pragma mark - device module API


status_t
poke_open(void* _info, const char* name, int openMode, void** cookie)
{
	*cookie = NULL;

	if (getuid() != 0 && geteuid() != 0)
		return EPERM;

#if defined(__i386__) || defined(__x86_64__)
	/* on x86, raise the IOPL so that outb/inb will work */
	iframe* frame = x86_get_user_iframe();
	int iopl = 3;
	frame->flags &= ~X86_EFLAGS_IO_PRIVILEG_LEVEL;
	frame->flags |= (iopl << X86_EFLAGS_IO_PRIVILEG_LEVEL_SHIFT)
		& X86_EFLAGS_IO_PRIVILEG_LEVEL;
#endif

	return B_OK;
}


status_t
poke_close(void* cookie)
{
	return B_OK;
}


status_t
poke_free(void* cookie)
{
#if defined(__i386__) || defined(__x86_64__)
	iframe* frame = x86_get_user_iframe();
	int iopl = 0;
	frame->flags &= ~X86_EFLAGS_IO_PRIVILEG_LEVEL;
	frame->flags |= (iopl << X86_EFLAGS_IO_PRIVILEG_LEVEL_SHIFT)
		& X86_EFLAGS_IO_PRIVILEG_LEVEL;
#endif

	return B_OK;
}


status_t
poke_control(void* cookie, uint32 op, void* arg, size_t length)
{
	if (!IS_USER_ADDRESS(arg))
		return B_BAD_ADDRESS;

	switch (op) {
		case POKE_PORT_READ:
		{
			status_t result = B_OK;
			port_io_args ioctl;
			if (user_memcpy(&ioctl, arg, sizeof(port_io_args)) != B_OK)
				return B_BAD_ADDRESS;
			if (ioctl.signature != POKE_SIGNATURE)
				return B_BAD_VALUE;

			switch (ioctl.size) {
				case 1:
					ioctl.value = isa->read_io_8(ioctl.port);
				break;
				case 2:
					ioctl.value = isa->read_io_16(ioctl.port);
				break;
				case 4:
					ioctl.value = isa->read_io_32(ioctl.port);
				break;
				default:
					result = B_BAD_VALUE;
			}

			if (user_memcpy(arg, &ioctl, sizeof(port_io_args)) != B_OK)
				return B_BAD_ADDRESS;
			return result;
		}

		case POKE_PORT_WRITE:
		{
			status_t result = B_OK;
			port_io_args ioctl;
			if (user_memcpy(&ioctl, arg, sizeof(port_io_args)) != B_OK)
				return B_BAD_ADDRESS;
			if (ioctl.signature != POKE_SIGNATURE)
				return B_BAD_VALUE;

			switch (ioctl.size) {
				case 1:
					isa->write_io_8(ioctl.port, ioctl.value);
					break;
				case 2:
					isa->write_io_16(ioctl.port, ioctl.value);
					break;
				case 4:
					isa->write_io_32(ioctl.port, ioctl.value);
					break;
				default:
					result = B_BAD_VALUE;
			}

			return result;
		}

		case POKE_PORT_INDEXED_READ:
		{
			port_io_args ioctl;
			if (user_memcpy(&ioctl, arg, sizeof(port_io_args)) != B_OK)
				return B_BAD_ADDRESS;
			if (ioctl.signature != POKE_SIGNATURE)
				return B_BAD_VALUE;

			isa->write_io_8(ioctl.port, ioctl.size);
			ioctl.value = isa->read_io_8(ioctl.port + 1);

			if (user_memcpy(arg, &ioctl, sizeof(port_io_args)) != B_OK)
				return B_BAD_ADDRESS;
			return B_OK;
		}

		case POKE_PORT_INDEXED_WRITE:
		{
			port_io_args ioctl;
			if (user_memcpy(&ioctl, arg, sizeof(port_io_args)) != B_OK)
				return B_BAD_ADDRESS;
			if (ioctl.signature != POKE_SIGNATURE)
				return B_BAD_VALUE;

			isa->write_io_8(ioctl.port, ioctl.size);
			isa->write_io_8(ioctl.port + 1, ioctl.value);
			return B_OK;
		}

		case POKE_PCI_READ_CONFIG:
		{
			pci_io_args ioctl;
			if (user_memcpy(&ioctl, arg, sizeof(pci_io_args)) != B_OK)
				return B_BAD_ADDRESS;
			if (ioctl.signature != POKE_SIGNATURE)
				return B_BAD_VALUE;

			ioctl.value = pci->read_pci_config(ioctl.bus, ioctl.device,
				ioctl.function, ioctl.offset, ioctl.size);
			if (user_memcpy(arg, &ioctl, sizeof(pci_io_args)) != B_OK)
				return B_BAD_ADDRESS;
			return B_OK;
		}

		case POKE_PCI_WRITE_CONFIG:
		{
			pci_io_args ioctl;
			if (user_memcpy(&ioctl, arg, sizeof(pci_io_args)) != B_OK)
				return B_BAD_ADDRESS;
			if (ioctl.signature != POKE_SIGNATURE)
				return B_BAD_VALUE;

			pci->write_pci_config(ioctl.bus, ioctl.device, ioctl.function,
				ioctl.offset, ioctl.size, ioctl.value);
			return B_OK;
		}

		case POKE_GET_NTH_PCI_INFO:
		{
			pci_info_args ioctl;
			if (user_memcpy(&ioctl, arg, sizeof(pci_info_args)) != B_OK)
				return B_BAD_ADDRESS;
			if (ioctl.signature != POKE_SIGNATURE)
				return B_BAD_VALUE;
			if (!IS_USER_ADDRESS(ioctl.info))
				return B_BAD_ADDRESS;

			pci_info info;
			ioctl.status = pci->get_nth_pci_info(ioctl.index, &info);

			if (user_memcpy(ioctl.info, &info, sizeof(pci_info)) != B_OK)
				return B_BAD_ADDRESS;
			if (user_memcpy(arg, &ioctl, sizeof(pci_info_args)) != B_OK)
				return B_BAD_ADDRESS;
			return B_OK;
		}

		case POKE_GET_PHYSICAL_ADDRESS:
		{
			mem_map_args ioctl;
			if (user_memcpy(&ioctl, arg, sizeof(mem_map_args)) != B_OK)
				return B_BAD_ADDRESS;
			physical_entry table;
			status_t result;

			if (ioctl.signature != POKE_SIGNATURE)
				return B_BAD_VALUE;

			result = get_memory_map(ioctl.address, ioctl.size, &table, 1);
			ioctl.physical_address = table.address;
			ioctl.size = table.size;
			if (user_memcpy(arg, &ioctl, sizeof(mem_map_args)) != B_OK)
				return B_BAD_ADDRESS;
			return result;
		}

		case POKE_MAP_MEMORY:
		{
			mem_map_args ioctl;
			if (user_memcpy(&ioctl, arg, sizeof(mem_map_args)) != B_OK)
				return B_BAD_ADDRESS;
			if (ioctl.signature != POKE_SIGNATURE)
				return B_BAD_VALUE;

			char name[B_OS_NAME_LENGTH];
			if (user_strlcpy(name, ioctl.name, B_OS_NAME_LENGTH) < B_OK)
				return B_BAD_ADDRESS;

			ioctl.area = vm_map_physical_memory(team_get_current_team_id(),
				name, (void**)&ioctl.address, ioctl.flags, ioctl.size,
				ioctl.protection, ioctl.physical_address, false);

			if (user_memcpy(arg, &ioctl, sizeof(mem_map_args)) != B_OK)
				return B_BAD_ADDRESS;
			return ioctl.area;
		}

		case POKE_UNMAP_MEMORY:
		{
			mem_map_args ioctl;
			if (user_memcpy(&ioctl, arg, sizeof(mem_map_args)) != B_OK)
				return B_BAD_ADDRESS;
			if (ioctl.signature != POKE_SIGNATURE)
				return B_BAD_VALUE;

			return _user_delete_area(ioctl.area);
		}
	}

	return B_BAD_VALUE;
}


status_t
poke_read(void* cookie, off_t position, void* buffer, size_t* numBytes)
{
	*numBytes = 0;
	return B_NOT_ALLOWED;
}


status_t
poke_write(void* cookie, off_t position, const void* buffer, size_t* numBytes)
{
	*numBytes = 0;
	return B_NOT_ALLOWED;
}


//	#pragma mark - dk_device_ops / dk_driver_info


static dk_device_ops sDeviceOps = {
	poke_open,
	poke_close,
	poke_free,
	poke_read,
	poke_write,
	NULL,	// io
	poke_control,
	NULL,	// select
	NULL,	// deselect
	NULL,	// device_removed
};


static const dk_match_rule sPokeMatchRules[] = {
	{ KOSM_DEVICE_BUS, B_STRING_TYPE, { .string = "generic" } },
	{}
};

static const dk_match_dict sPokeMatchDict = {
	sPokeMatchRules,
	0
};


static float
poke_probe(dk_node *node)
{
	if (sPublished)
		return 0.0;
	return 0.01;
}


static status_t
poke_attach(dk_node *node, void **cookie)
{
	if (get_module(B_ISA_MODULE_NAME, (module_info**)&isa) < B_OK)
		return ENOSYS;

	if (get_module(B_PCI_MODULE_NAME, (module_info**)&pci) < B_OK) {
		put_module(B_ISA_MODULE_NAME);
		return ENOSYS;
	}

	poke_driver_info *info = (poke_driver_info *)malloc(
		sizeof(poke_driver_info));
	if (info == NULL) {
		put_module(B_PCI_MODULE_NAME);
		put_module(B_ISA_MODULE_NAME);
		return B_NO_MEMORY;
	}

	info->node = node;

	sPublished = true;
	sDeviceKeeper->publish_device(node, "misc/" POKE_DEVICE_NAME,
		&sDeviceOps);

	*cookie = info;
	return B_OK;
}


static void
poke_detach(void *_cookie)
{
	poke_driver_info *info = (poke_driver_info *)_cookie;

	sPublished = false;
	put_module(B_ISA_MODULE_NAME);
	put_module(B_PCI_MODULE_NAME);
	free(info);
}


//	#pragma mark -


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&sDeviceKeeper },
	{ NULL }
};

struct dk_driver_info sPokeDriver = {
	.info = {
		POKE_DRIVER_MODULE_NAME,
		0,
		NULL
	},
	.match	= &sPokeMatchDict,
	.probe	= poke_probe,
	.attach	= poke_attach,
	.detach	= poke_detach,
	.ops	= &sDeviceOps,
};

module_info *modules[] = {
	(module_info *)&sPokeDriver,
	NULL
};
