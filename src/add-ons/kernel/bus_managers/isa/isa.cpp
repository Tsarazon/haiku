/*
 * Copyright 2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2002/03, Thomas Kurschel. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */

/*
	ISA bus manager

	Implementation.
*/


#include <bus/ISA.h>
#include <KernelExport.h>
#include <device_keeper.h>
#include <arch/cpu.h>

#include <stdlib.h>
#include <string.h>

#include "isa_arch.h"

//#define TRACE_ISA
#ifdef TRACE_ISA
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

// ToDo: this is architecture dependent and should be made differently!
//	(for example, the Pegasos (PPC based) also has an ISA bus)


#define ISA_MODULE_NAME "bus_managers/isa/root/dk_driver_v1"

dk_keeper_info *pnp;


static long
make_isa_dma_table(const void *buffer, long buffer_size, ulong num_bits,
	isa_dma_entry *table, long num_entries)
{
	// ToDo: implement this?!
	return ENOSYS;
}


static long
start_scattered_isa_dma(long channel, const isa_dma_entry *table,
	uchar mode, uchar emode)
{
	// ToDo: implement this?!
	return ENOSYS;
}


static status_t
lock_isa_dma_channel(long channel)
{
	// ToDo: implement this?!
	return B_NOT_ALLOWED;
}


static status_t
unlock_isa_dma_channel(long channel)
{
	// ToDo: implement this?!
	return B_ERROR;
}


//	#pragma mark - DeviceKeeper driver API


static status_t
isa_init_driver(dk_node *node, void **cookie)
{
	// ISA peripherals consume the bus via the classical
	// get_module(B_ISA_MODULE_NAME, ...) singleton API exported below.
	*cookie = node;
	return B_OK;
}


static void
isa_uninit_driver(void *cookie)
{
}


static float
isa_supports_device(dk_node *parent)
{
	char bus[64];

	// make sure parent is really device root
	if (pnp->get_property_string(parent, KOSM_DEVICE_BUS, bus,
			sizeof(bus), NULL, false) != B_OK)
		return -1.0f;

	if (strcmp(bus, "root") != 0)
		return 0.0f;

	return 1.0f;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return arch_isa_init();
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			return B_ERROR;
	}
}


module_dependency module_dependencies[] = {
	{ KOSM_DEVICE_KEEPER_MODULE_NAME, (module_info **)&pnp },
	{}
};


// Legacy bus_manager-style isa_module_info — still exposed via
// get_module(B_ISA_MODULE_NAME, ...) for callers that haven't migrated
// off the original Haiku bus_manager API.
static isa_module_info isa_module = {
	{
		{
			B_ISA_MODULE_NAME,
			B_KEEP_LOADED,
			std_ops
		},
		NULL	// rescan
	},
	&arch_isa_read_io_8,
	&arch_isa_write_io_8,
	&arch_isa_read_io_16,
	&arch_isa_write_io_16,
	&arch_isa_read_io_32,
	&arch_isa_write_io_32,
	&arch_isa_ram_address,
	&make_isa_dma_table,
	&arch_start_isa_dma,
	&start_scattered_isa_dma,
	&lock_isa_dma_channel,
	&unlock_isa_dma_channel
};


static const dk_match_rule sIsaMatchRules[] = {
	DK_MATCH_STRING(KOSM_DEVICE_BUS, "root"),
	DK_MATCH_END
};

static const dk_match_dict sIsaMatchDict = {
	sIsaMatchRules,
	0
};


static dk_driver_info isa2_driver = {
	.info       = { ISA_MODULE_NAME, 0, std_ops },
	.match      = &sIsaMatchDict,
	.probe      = isa_supports_device,
	.attach     = isa_init_driver,
	.detach     = isa_uninit_driver,
	.node_flags = KOSM_FIND_MULTIPLE_CHILDREN,
};

module_info *modules[] = {
	(module_info *)&isa_module,
	(module_info *)&isa2_driver,
	NULL
};
