/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 EFI Device Tree Blob Support
 * 
 * This module implements device tree blob parsing and validation for ARM64
 * UEFI systems, including basic node enumeration and hardware discovery.
 */


#include <string.h>
#include <algorithm>

#include <KernelExport.h>
#include <ByteOrder.h>

#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/kernel_args.h>
#include <boot/addr_range.h>

extern "C" {
#include <libfdt.h>
}

#include <efi/types.h>
#include <efi/system-table.h>

#include "efi_platform.h"


//#define TRACE_DTB
#ifdef TRACE_DTB
#	define TRACE(x...) dprintf("arm64/dtb: " x)
#else
#	define TRACE(x...) ;
#endif

#define INFO(x...) dprintf("arm64/dtb: " x)
#define ERROR(x...) dprintf("arm64/dtb: ERROR: " x)


// DTB location and size
static void* sDtbTable = NULL;
static uint32 sDtbSize = 0;

// DTB GUID for EFI configuration table lookup
static const efi_guid kDtbTableGuid = DEVICE_TREE_GUID;


/**
 * Locate device tree blob in EFI configuration tables
 */
static status_t
dtb_locate_from_efi_config_table(void)
{
	TRACE("Searching for DTB in EFI configuration tables\n");

	if (kSystemTable == NULL || kSystemTable->ConfigurationTable == NULL) {
		ERROR("EFI system table or configuration table not available\n");
		return B_ERROR;
	}

	// Search through EFI configuration tables
	for (size_t i = 0; i < kSystemTable->NumberOfTableEntries; i++) {
		efi_configuration_table* table = &kSystemTable->ConfigurationTable[i];
		
		if (table->VendorGuid.equals(kDtbTableGuid)) {
			sDtbTable = table->VendorTable;
			TRACE("Found DTB in EFI config table at %p\n", sDtbTable);
			return B_OK;
		}
	}

	ERROR("DTB not found in EFI configuration tables\n");
	return B_ERROR;
}


/**
 * Validate device tree blob structure and magic
 */
static status_t
dtb_validate_blob(void* fdt)
{
	if (fdt == NULL) {
		ERROR("DTB pointer is NULL\n");
		return B_BAD_VALUE;
	}

	// Check FDT magic number
	int result = fdt_check_header(fdt);
	if (result != 0) {
		ERROR("Invalid DTB header: %s\n", fdt_strerror(result));
		return B_BAD_DATA;
	}

	// Get and validate DTB size
	sDtbSize = fdt_totalsize(fdt);
	if (sDtbSize < sizeof(fdt_header) || sDtbSize > 16 * 1024 * 1024) {
		ERROR("Invalid DTB size: %u bytes\n", sDtbSize);
		return B_BAD_DATA;
	}

	INFO("Valid DTB found: %u bytes, version %d\n", 
		sDtbSize, fdt_version(fdt));

	return B_OK;
}


/**
 * Parse memory information from device tree
 */
static status_t
dtb_parse_memory_nodes(void* fdt)
{
	TRACE("Parsing memory nodes from DTB\n");

	int memory_node = fdt_path_offset(fdt, "/memory");
	if (memory_node < 0) {
		// Try alternative path
		memory_node = fdt_path_offset(fdt, "/memory@0");
		if (memory_node < 0) {
			TRACE("No memory node found in DTB\n");
			return B_OK;  // Not an error, EFI memory map is primary
		}
	}

	// Get memory reg property
	int length;
	const void* reg_property = fdt_getprop(fdt, memory_node, "reg", &length);
	if (reg_property == NULL) {
		TRACE("No reg property in memory node\n");
		return B_OK;
	}

	// Parse memory ranges (assuming 64-bit addresses)
	const uint64* reg_data = (const uint64*)reg_property;
	int num_ranges = length / (2 * sizeof(uint64));

	INFO("DTB Memory ranges (%d entries):\n", num_ranges);
	for (int i = 0; i < num_ranges; i++) {
		uint64 base = B_BENDIAN_TO_HOST_INT64(reg_data[i * 2]);
		uint64 size = B_BENDIAN_TO_HOST_INT64(reg_data[i * 2 + 1]);
		
		INFO("  [%d] 0x%016llx - 0x%016llx (%lld MB)\n", 
			i, base, base + size, size / (1024 * 1024));
		
		// TODO: Cross-reference with EFI memory map for validation
	}

	return B_OK;
}


/**
 * Parse CPU information from device tree
 */
static status_t
dtb_parse_cpu_nodes(void* fdt)
{
	TRACE("Parsing CPU nodes from DTB\n");

	int cpus_node = fdt_path_offset(fdt, "/cpus");
	if (cpus_node < 0) {
		TRACE("No /cpus node found in DTB\n");
		return B_OK;
	}

	uint32 cpu_count = 0;
	int cpu_node = fdt_first_subnode(fdt, cpus_node);
	
	while (cpu_node >= 0) {
		const char* node_name = fdt_get_name(fdt, cpu_node, NULL);
		if (node_name && strncmp(node_name, "cpu", 3) == 0) {
			cpu_count++;
			
			// Get CPU reg property (CPU ID)
			int length;
			const void* reg_prop = fdt_getprop(fdt, cpu_node, "reg", &length);
			if (reg_prop && length >= 4) {
				uint32 cpu_id = B_BENDIAN_TO_HOST_INT32(*(const uint32*)reg_prop);
				TRACE("  CPU %u: reg=0x%x\n", cpu_count - 1, cpu_id);
			}

			// Get compatible property
			const char* compatible = (const char*)fdt_getprop(fdt, cpu_node, "compatible", &length);
			if (compatible) {
				TRACE("  CPU %u: compatible=%s\n", cpu_count - 1, compatible);
			}
		}
		
		cpu_node = fdt_next_subnode(fdt, cpu_node);
	}

	INFO("Found %u CPU(s) in DTB\n", cpu_count);
	
	// Update kernel args if we found CPUs
	if (cpu_count > 0 && cpu_count <= SMP_MAX_CPUS) {
		gKernelArgs.num_cpus = cpu_count;
	}

	return B_OK;
}


/**
 * Parse interrupt controller information from device tree
 */
static status_t
dtb_parse_interrupt_controller(void* fdt)
{
	TRACE("Parsing interrupt controller from DTB\n");

	// Look for GIC (Generic Interrupt Controller)
	int gic_node = -1;
	int node = fdt_node_offset_by_compatible(fdt, -1, "arm,gic-400");
	if (node < 0) {
		node = fdt_node_offset_by_compatible(fdt, -1, "arm,cortex-a15-gic");
	}
	if (node < 0) {
		node = fdt_node_offset_by_compatible(fdt, -1, "arm,cortex-a9-gic");
	}
	if (node < 0) {
		node = fdt_node_offset_by_compatible(fdt, -1, "arm,gic-v3");
	}
	
	if (node >= 0) {
		gic_node = node;
		
		// Get GIC register properties
		int length;
		const void* reg_prop = fdt_getprop(fdt, gic_node, "reg", &length);
		if (reg_prop && length >= 16) {
			const uint64* reg_data = (const uint64*)reg_prop;
			uint64 dist_base = B_BENDIAN_TO_HOST_INT64(reg_data[0]);
			uint64 dist_size = B_BENDIAN_TO_HOST_INT64(reg_data[1]);
			
			INFO("GIC Distributor: 0x%016llx (size: 0x%llx)\n", dist_base, dist_size);
			
			if (length >= 32) {
				uint64 cpu_base = B_BENDIAN_TO_HOST_INT64(reg_data[2]);
				uint64 cpu_size = B_BENDIAN_TO_HOST_INT64(reg_data[3]);
				INFO("GIC CPU Interface: 0x%016llx (size: 0x%llx)\n", cpu_base, cpu_size);
			}
		}
		
		// TODO: Store GIC information in kernel args for later use
	} else {
		TRACE("No supported GIC found in DTB\n");
	}

	return B_OK;
}


/**
 * Parse timer information from device tree
 */
static status_t
dtb_parse_timer_nodes(void* fdt)
{
	TRACE("Parsing timer nodes from DTB\n");

	// Look for ARM generic timer
	int timer_node = fdt_node_offset_by_compatible(fdt, -1, "arm,armv8-timer");
	if (timer_node < 0) {
		timer_node = fdt_node_offset_by_compatible(fdt, -1, "arm,armv7-timer");
	}
	
	if (timer_node >= 0) {
		INFO("Found ARM generic timer\n");
		
		// Get timer interrupt information
		int length;
		const void* interrupts_prop = fdt_getprop(fdt, timer_node, "interrupts", &length);
		if (interrupts_prop && length >= 12) {
			const uint32* interrupts = (const uint32*)interrupts_prop;
			
			// ARM generic timer typically has 4 interrupts:
			// 0: secure physical timer
			// 1: non-secure physical timer  
			// 2: virtual timer
			// 3: hypervisor timer
			int num_interrupts = length / (3 * sizeof(uint32));
			for (int i = 0; i < num_interrupts && i < 4; i++) {
				uint32 type = B_BENDIAN_TO_HOST_INT32(interrupts[i * 3 + 0]);
				uint32 irq = B_BENDIAN_TO_HOST_INT32(interrupts[i * 3 + 1]);
				uint32 flags = B_BENDIAN_TO_HOST_INT32(interrupts[i * 3 + 2]);
				
				TRACE("  Timer interrupt %d: type=%u irq=%u flags=0x%x\n", 
					i, type, irq, flags);
			}
		}
		
		// TODO: Store timer information in kernel args
	} else {
		TRACE("No ARM generic timer found in DTB\n");
	}

	return B_OK;
}


/**
 * Enumerate basic device tree nodes
 */
static status_t
dtb_enumerate_nodes(void* fdt)
{
	TRACE("Enumerating device tree nodes\n");

	int node = fdt_next_node(fdt, -1, NULL);
	int depth = 0;
	uint32 node_count = 0;

	while (node >= 0) {
		const char* name = fdt_get_name(fdt, node, NULL);
		if (name == NULL) {
			name = "<unnamed>";
		}

		// Get compatible property if available
		int length;
		const char* compatible = (const char*)fdt_getprop(fdt, node, "compatible", &length);
		
		TRACE("%*s%s", depth * 2, "", name);
		if (compatible) {
			TRACE(" (compatible: %s)", compatible);
		}
		TRACE("\n");

		node_count++;
		node = fdt_next_node(fdt, node, &depth);
	}

	INFO("Enumerated %u device tree nodes\n", node_count);
	return B_OK;
}


/**
 * Initialize device tree blob support
 */
extern "C" void
dtb_init(void)
{
	INFO("Initializing ARM64 device tree support\n");

	// Locate DTB from EFI configuration table
	if (dtb_locate_from_efi_config_table() != B_OK) {
		ERROR("Failed to locate device tree blob\n");
		return;
	}

	// Validate DTB structure
	if (dtb_validate_blob(sDtbTable) != B_OK) {
		ERROR("Device tree blob validation failed\n");
		sDtbTable = NULL;
		return;
	}

	INFO("DTB initialization successful\n");

	// Parse hardware information from DTB
	dtb_parse_memory_nodes(sDtbTable);
	dtb_parse_cpu_nodes(sDtbTable);
	dtb_parse_interrupt_controller(sDtbTable);
	dtb_parse_timer_nodes(sDtbTable);

#ifdef TRACE_DTB
	// Enumerate all nodes for debugging
	dtb_enumerate_nodes(sDtbTable);
#endif

	// Store DTB pointer in kernel arguments for kernel use
	gKernelArgs.arch_args.fdt = sDtbTable;
}


/**
 * Get pointer to flattened device tree
 */
extern "C" void*
dtb_get_fdt(void)
{
	return sDtbTable;
}


/**
 * Get size of device tree blob
 */
extern "C" uint32
dtb_get_size(void)
{
	return sDtbSize;
}


/**
 * Find device tree node by path
 */
extern "C" int
dtb_get_node_by_path(const char* path)
{
	if (sDtbTable == NULL || path == NULL) {
		return -1;
	}

	return fdt_path_offset(sDtbTable, path);
}


/**
 * Find device tree node by compatible string
 */
extern "C" int
dtb_get_node_by_compatible(const char* compatible)
{
	if (sDtbTable == NULL || compatible == NULL) {
		return -1;
	}

	return fdt_node_offset_by_compatible(sDtbTable, -1, compatible);
}


/**
 * Get property from device tree node
 */
extern "C" const void*
dtb_get_property(int node, const char* name, int* length)
{
	if (sDtbTable == NULL || node < 0 || name == NULL) {
		if (length) *length = 0;
		return NULL;
	}

	return fdt_getprop(sDtbTable, node, name, length);
}


/**
 * Get register address from device tree node
 */
extern "C" status_t
dtb_get_reg_address(int node, uint32 index, uint64* _address, uint64* _size)
{
	if (sDtbTable == NULL || node < 0 || _address == NULL) {
		return B_BAD_VALUE;
	}

	int length;
	const void* reg_prop = fdt_getprop(sDtbTable, node, "reg", &length);
	if (reg_prop == NULL) {
		return B_NAME_NOT_FOUND;
	}

	// Assume 64-bit addresses (address-cells=2, size-cells=2)
	const uint64* reg_data = (const uint64*)reg_prop;
	int num_entries = length / (2 * sizeof(uint64));

	if (index >= (uint32)num_entries) {
		return B_BAD_INDEX;
	}

	*_address = B_BENDIAN_TO_HOST_INT64(reg_data[index * 2]);
	if (_size) {
		*_size = B_BENDIAN_TO_HOST_INT64(reg_data[index * 2 + 1]);
	}

	return B_OK;
}


/**
 * Parse memory layout from device tree
 */
extern "C" status_t
dtb_parse_memory_layout(void)
{
	if (sDtbTable == NULL) {
		return B_ERROR;
	}

	return dtb_parse_memory_nodes(sDtbTable);
}


/**
 * Get CPU topology from device tree
 */
extern "C" status_t
dtb_get_cpu_topology(void)
{
	if (sDtbTable == NULL) {
		return B_ERROR;
	}

	return dtb_parse_cpu_nodes(sDtbTable);
}