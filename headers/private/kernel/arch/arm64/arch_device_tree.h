/*
 * Copyright 2024-2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Device Tree Support Header
 * 
 * This header provides device tree parsing and configuration interfaces
 * for ARM64 systems. It supports FDT (Flattened Device Tree) parsing
 * and hardware detection.
 */

#ifndef _KERNEL_ARCH_ARM64_DEVICE_TREE_H
#define _KERNEL_ARCH_ARM64_DEVICE_TREE_H

#include <SupportDefs.h>

// Device Tree Node Structure
typedef struct device_tree_node {
	const void* fdt;             // Pointer to FDT blob
	int node_offset;             // Node offset in FDT
	const char* name;            // Node name
	const char* compatible;      // Compatible string
	int compatible_len;          // Compatible string length
} device_tree_node_t;

// Device Tree Property Access
status_t dt_get_property(device_tree_node_t* node, const char* name,
	const void** value, int* length);

// Device Tree String Utilities
bool dt_has_compatible(device_tree_node_t* node, const char* compat);
const char* dt_get_string_property(device_tree_node_t* node, 
	const char* prop_name);

// Device Tree Register Parsing
status_t dt_get_reg_address(device_tree_node_t* node, int index,
	phys_addr_t* address, size_t* size);

// Device Tree Interrupt Parsing
status_t dt_get_interrupt(device_tree_node_t* node, int index,
	uint32* interrupt_num);

// Device Tree Clock Frequency
uint32 dt_get_clock_frequency(device_tree_node_t* node);

#endif /* _KERNEL_ARCH_ARM64_DEVICE_TREE_H */
