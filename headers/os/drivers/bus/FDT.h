/*
 * Copyright 2020-2021, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DRIVERS_BUS_FDT_H
#define _DRIVERS_BUS_FDT_H


#include <device_keeper.h>


struct fdt_bus;
struct fdt_device;
struct fdt_interrupt_map;

// Bus interface names for publish_interface/get_interface. Published by
// the FDT bus manager on bus and device nodes respectively. Child drivers
// retrieve them via gDeviceKeeper->get_interface(node, NAME, ...).
#define FDT_BUS_INTERFACE_NAME		"interface/fdt/bus/v1"
#define FDT_DEVICE_INTERFACE_NAME	"interface/fdt/device/v1"

typedef struct fdt_bus_module_info {
	dk_node* (*node_by_phandle)(struct fdt_bus* bus, int phandle);
} fdt_bus_module_info;

typedef struct fdt_device_module_info {
	dk_node* (*get_bus)(struct fdt_device* dev);
	const char* (*get_name)(struct fdt_device* dev);
	const void* (*get_prop)(struct fdt_device* dev, const char* name, int* len);
	bool (*get_reg)(struct fdt_device* dev, uint32 ord, uint64* regs, uint64* len);
	bool (*get_interrupt)(struct fdt_device* dev, uint32 ord,
		dk_node** interruptController, uint64* interrupt);
	struct fdt_interrupt_map* (*get_interrupt_map)(struct fdt_device* dev);
	void (*print_interrupt_map)(struct fdt_interrupt_map* interruptMap);
	uint32 (*lookup_interrupt_map)(struct fdt_interrupt_map* interruptMap, uint32 childAddr, uint32 childIrq);
} fdt_device_module_info;


#endif // _DRIVERS_BUS_FDT_H
