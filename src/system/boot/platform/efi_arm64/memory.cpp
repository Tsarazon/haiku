/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 EFI Memory Map Parsing and Management
 * 
 * This module implements UEFI memory map parsing specific to ARM64
 * memory layout requirements, including memory type detection and
 * basic memory region categorization.
 */


#include <string.h>
#include <algorithm>

#include <KernelExport.h>
#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/kernel_args.h>
#include <boot/addr_range.h>

#include <efi/types.h>
#include <efi/boot-services.h>

#include "efi_platform.h"


//#define TRACE_MEMORY
#ifdef TRACE_MEMORY
#	define TRACE(x...) dprintf("arm64/memory: " x)
#else
#	define TRACE(x...) ;
#endif


// ARM64-specific memory layout constants
#define ARM64_MEMORY_BELOW_4GB_LIMIT	0x100000000ULL	// 4GB
#define ARM64_USABLE_MEMORY_BASE	0x100000		// 1MB minimum
#define ARM64_MAX_MEMORY_HOLE		0x10000000		// 256MB max hole
#define ARM64_MEMORY_ALIGNMENT		0x200000		// 2MB alignment for large pages

// ARM64 memory attribute definitions for MAIR_EL1
#define ARM64_MEMORY_ATTR_DEVICE_nGnRnE		0x00	// Device non-Gathering, non-Reordering, non-Early Write Acknowledgement
#define ARM64_MEMORY_ATTR_DEVICE_nGnRE		0x04	// Device non-Gathering, non-Reordering, Early Write Acknowledgement  
#define ARM64_MEMORY_ATTR_NORMAL_NC		0x44	// Normal memory, Non-cacheable
#define ARM64_MEMORY_ATTR_NORMAL_WT		0xBB	// Normal memory, Write-through
#define ARM64_MEMORY_ATTR_NORMAL_WB		0xFF	// Normal memory, Write-back


/**
 * Convert EFI memory type to descriptive string
 */
static const char*
memory_region_type_str(efi_memory_type type)
{
	switch (type) {
		case EfiReservedMemoryType:
			return "EfiReservedMemoryType";
		case EfiLoaderCode:
			return "EfiLoaderCode";
		case EfiLoaderData:
			return "EfiLoaderData";
		case EfiBootServicesCode:
			return "EfiBootServicesCode";
		case EfiBootServicesData:
			return "EfiBootServicesData";
		case EfiRuntimeServicesCode:
			return "EfiRuntimeServicesCode";
		case EfiRuntimeServicesData:
			return "EfiRuntimeServicesData";
		case EfiConventionalMemory:
			return "EfiConventionalMemory";
		case EfiUnusableMemory:
			return "EfiUnusableMemory";
		case EfiACPIReclaimMemory:
			return "EfiACPIReclaimMemory";
		case EfiACPIMemoryNVS:
			return "EfiACPIMemoryNVS";
		case EfiMemoryMappedIO:
			return "EfiMemoryMappedIO";
		case EfiMemoryMappedIOPortSpace:
			return "EfiMemoryMappedIOPortSpace";
		case EfiPalCode:
			return "EfiPalCode";
		case EfiPersistentMemory:
			return "EfiPersistentMemory";
		default:
			return "unknown";
	}
}


/**
 * Determine if EFI memory type is usable for ARM64 kernel
 */
static bool
is_usable_memory_type(efi_memory_type type)
{
	switch (type) {
		case EfiLoaderCode:
		case EfiLoaderData:
		case EfiBootServicesCode:
		case EfiBootServicesData:
		case EfiConventionalMemory:
			return true;
		default:
			return false;
	}
}


/**
 * Determine if EFI memory type requires runtime mapping
 */
static bool
requires_runtime_mapping(efi_memory_type type)
{
	switch (type) {
		case EfiRuntimeServicesCode:
		case EfiRuntimeServicesData:
		case EfiMemoryMappedIO:
		case EfiMemoryMappedIOPortSpace:
			return true;
		default:
			return false;
	}
}


/**
 * Get ARM64 memory attributes for EFI memory type
 */
static uint8
get_arm64_memory_attributes(efi_memory_type type, uint64 efi_attributes)
{
	// Check EFI memory attributes
	if (efi_attributes & EFI_MEMORY_UC) {
		// Uncacheable memory - use device attributes
		return ARM64_MEMORY_ATTR_DEVICE_nGnRnE;
	}
	
	if (efi_attributes & EFI_MEMORY_WC) {
		// Write-combining memory
		return ARM64_MEMORY_ATTR_NORMAL_NC;
	}
	
	if (efi_attributes & EFI_MEMORY_WT) {
		// Write-through memory
		return ARM64_MEMORY_ATTR_NORMAL_WT;
	}

	// Default to write-back for normal memory
	switch (type) {
		case EfiLoaderCode:
		case EfiLoaderData:
		case EfiBootServicesCode:
		case EfiBootServicesData:
		case EfiConventionalMemory:
		case EfiRuntimeServicesCode:
		case EfiRuntimeServicesData:
		case EfiACPIReclaimMemory:
			return ARM64_MEMORY_ATTR_NORMAL_WB;
			
		case EfiMemoryMappedIO:
		case EfiMemoryMappedIOPortSpace:
			return ARM64_MEMORY_ATTR_DEVICE_nGnRnE;
			
		default:
			return ARM64_MEMORY_ATTR_DEVICE_nGnRnE;
	}
}


/**
 * Validate ARM64 memory region for kernel use
 */
static bool
validate_arm64_memory_region(uint64 base, uint64 size)
{
	// Check minimum alignment requirements
	if (base & (B_PAGE_SIZE - 1)) {
		TRACE("Memory region not page-aligned: 0x%llx\n", base);
		return false;
	}
	
	// Check for reasonable size
	if (size < B_PAGE_SIZE) {
		TRACE("Memory region too small: 0x%llx bytes\n", size);
		return false;
	}
	
	// Check for overflow
	if (base + size < base) {
		TRACE("Memory region overflow: base=0x%llx size=0x%llx\n", base, size);
		return false;
	}
	
	return true;
}


/**
 * Get UEFI memory map from firmware
 */
extern "C" status_t
memory_get_efi_memory_map(efi_memory_descriptor** _memory_map, 
	size_t* _memory_map_size, size_t* _map_key, size_t* _descriptor_size, 
	uint32_t* _descriptor_version)
{
	TRACE("Getting EFI memory map\n");

	if (kBootServices == NULL) {
		dprintf("EFI boot services not available\n");
		return B_ERROR;
	}

	// First call to determine buffer size
	size_t memory_map_size = 0;
	efi_memory_descriptor dummy;
	size_t map_key;
	size_t descriptor_size;
	uint32_t descriptor_version;

	efi_status status = kBootServices->GetMemoryMap(&memory_map_size, &dummy, 
		&map_key, &descriptor_size, &descriptor_version);
		
	if (status != EFI_BUFFER_TOO_SMALL) {
		dprintf("Unable to determine EFI memory map size: 0x%lx\n", status);
		return B_ERROR;
	}

	// Allocate buffer with extra space for potential changes
	size_t actual_memory_map_size = memory_map_size * 2;
	efi_memory_descriptor* memory_map = (efi_memory_descriptor*)
		kernel_args_malloc(actual_memory_map_size);
		
	if (memory_map == NULL) {
		dprintf("Unable to allocate memory map buffer\n");
		return B_NO_MEMORY;
	}

	// Get actual memory map
	memory_map_size = actual_memory_map_size;
	status = kBootServices->GetMemoryMap(&memory_map_size, memory_map, 
		&map_key, &descriptor_size, &descriptor_version);
		
	if (status != EFI_SUCCESS) {
		dprintf("Unable to get EFI memory map: 0x%lx\n", status);
		return B_ERROR;
	}

	TRACE("EFI memory map: %zu bytes, %zu entries, descriptor size %zu\n",
		memory_map_size, memory_map_size / descriptor_size, descriptor_size);

	*_memory_map = memory_map;
	*_memory_map_size = memory_map_size;
	*_map_key = map_key;
	*_descriptor_size = descriptor_size;
	*_descriptor_version = descriptor_version;

	return B_OK;
}


/**
 * Parse and categorize EFI memory map for ARM64
 */
extern "C" status_t
memory_parse_efi_entries(efi_memory_descriptor* memory_map, size_t memory_map_size,
	size_t descriptor_size, uint32_t descriptor_version)
{
	TRACE("Parsing EFI memory entries for ARM64\n");

	size_t entry_count = memory_map_size / descriptor_size;
	addr_t addr = (addr_t)memory_map;

	// Clear existing memory ranges
	gKernelArgs.num_physical_memory_ranges = 0;
	gKernelArgs.num_physical_allocated_ranges = 0;
	gKernelArgs.ignored_physical_memory = 0;

	dprintf("ARM64 EFI Memory Map (%zu entries):\n", entry_count);

	// First pass: Add all usable memory ranges
	for (size_t i = 0; i < entry_count; i++) {
		efi_memory_descriptor* entry = (efi_memory_descriptor*)
			(addr + i * descriptor_size);

		uint64 base = entry->PhysicalStart;
		uint64 size = entry->NumberOfPages * B_PAGE_SIZE;
		uint64 end = base + size;

		dprintf("  [%zu] phys: 0x%016llx-0x%016llx, type: %s, attr: 0x%llx\n",
			i, base, end, memory_region_type_str(entry->Type), entry->Attribute);

		if (is_usable_memory_type(entry->Type)) {
			// Apply ARM64-specific memory constraints
			uint64 original_size = size;
			
			// Skip memory below 1MB (ARM64 typically reserves low memory)
			if (base < ARM64_USABLE_MEMORY_BASE) {
				uint64 adjust = ARM64_USABLE_MEMORY_BASE - base;
				if (adjust >= size) {
					gKernelArgs.ignored_physical_memory += original_size;
					continue;
				}
				base = ARM64_USABLE_MEMORY_BASE;
				size -= adjust;
			}

			// Validate the memory region
			if (!validate_arm64_memory_region(base, size)) {
				gKernelArgs.ignored_physical_memory += original_size;
				continue;
			}

			// Track ignored memory
			gKernelArgs.ignored_physical_memory += (original_size - size);

			// Add to usable memory ranges
			if (insert_physical_memory_range(base, size) != B_OK) {
				dprintf("Failed to insert physical memory range\n");
				continue;
			}

			// Mark loader data as pre-allocated
			if (entry->Type == EfiLoaderData) {
				insert_physical_allocated_range(base, size);
			}

			TRACE("  -> Added usable: 0x%llx-0x%llx (%lld MB)\n", 
				base, base + size, size / (1024 * 1024));
		}

		// Set up runtime virtual mappings for EFI runtime services
		if (requires_runtime_mapping(entry->Type)) {
			entry->VirtualStart = entry->PhysicalStart;
			TRACE("  -> Runtime mapping: 0x%llx\n", entry->PhysicalStart);
		}
	}

	uint64 initial_physical_memory = total_physical_memory();

	// Second pass: Remove reserved/unusable regions that might overlap
	for (size_t i = 0; i < entry_count; i++) {
		efi_memory_descriptor* entry = (efi_memory_descriptor*)
			(addr + i * descriptor_size);

		if (!is_usable_memory_type(entry->Type)) {
			uint64 base = entry->PhysicalStart;
			uint64 size = entry->NumberOfPages * B_PAGE_SIZE;

			// Remove overlapping reserved regions
			remove_physical_memory_range(base, size);
			TRACE("  -> Removed reserved: 0x%llx-0x%llx\n", base, base + size);
		}
	}

	// Update ignored memory count
	gKernelArgs.ignored_physical_memory += 
		(initial_physical_memory - total_physical_memory());

	// Sort memory ranges
	sort_address_ranges(gKernelArgs.physical_memory_range,
		gKernelArgs.num_physical_memory_ranges);
	sort_address_ranges(gKernelArgs.physical_allocated_range,
		gKernelArgs.num_physical_allocated_ranges);

	dprintf("ARM64 Memory Summary:\n");
	dprintf("  Total usable: %lld MB\n", total_physical_memory() / (1024 * 1024));
	dprintf("  Ignored: %lld MB\n", gKernelArgs.ignored_physical_memory / (1024 * 1024));
	dprintf("  Usable ranges: %u\n", gKernelArgs.num_physical_memory_ranges);

	return B_OK;
}


/**
 * Find largest contiguous memory region for ARM64 kernel placement
 */
extern "C" status_t
memory_find_kernel_region(addr_t* _base, size_t* _size, size_t required_size)
{
	TRACE("Finding kernel region (required: %zu MB)\n", required_size / (1024 * 1024));

	addr_t best_base = 0;
	size_t best_size = 0;

	// Look through physical memory ranges for suitable region
	for (uint32 i = 0; i < gKernelArgs.num_physical_memory_ranges; i++) {
		addr_t base = gKernelArgs.physical_memory_range[i].start;
		size_t size = gKernelArgs.physical_memory_range[i].size;

		// Align to 2MB boundary for ARM64 large page support
		addr_t aligned_base = ROUNDUP(base, ARM64_MEMORY_ALIGNMENT);
		if (aligned_base >= base + size)
			continue;

		size_t aligned_size = (base + size) - aligned_base;
		aligned_size = ROUNDDOWN(aligned_size, ARM64_MEMORY_ALIGNMENT);

		if (aligned_size >= required_size && aligned_size > best_size) {
			best_base = aligned_base;
			best_size = aligned_size;
		}
	}

	if (best_size == 0) {
		dprintf("No suitable memory region found for kernel\n");
		return B_ERROR;
	}

	*_base = best_base;
	*_size = best_size;

	TRACE("Selected kernel region: 0x%lx-0x%lx (%zu MB)\n", 
		best_base, best_base + best_size, best_size / (1024 * 1024));

	return B_OK;
}


/**
 * Check if address range conflicts with existing allocations
 */
extern "C" bool
memory_region_available(addr_t base, size_t size)
{
	// Check against allocated ranges
	for (uint32 i = 0; i < gKernelArgs.num_physical_allocated_ranges; i++) {
		addr_t alloc_start = gKernelArgs.physical_allocated_range[i].start;
		addr_t alloc_end = alloc_start + gKernelArgs.physical_allocated_range[i].size;
		addr_t end = base + size;

		if (base < alloc_end && end > alloc_start) {
			return false;  // Overlap detected
		}
	}

	return true;
}


/**
 * Initialize ARM64-specific memory management
 */
extern "C" status_t
memory_init_arm64(void)
{
	TRACE("Initializing ARM64 memory management\n");

	// Set up ARM64-specific memory attributes
	// This will be used later for page table setup

	// TODO: Configure ARM64 memory management unit
	// TODO: Set up initial page tables
	// TODO: Configure memory protection

	return B_OK;
}