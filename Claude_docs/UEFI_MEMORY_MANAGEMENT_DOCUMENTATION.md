# UEFI Memory Management Documentation

This document provides a comprehensive analysis of how UEFI memory maps are handled in Haiku's x86_64 implementation, covering memory allocation patterns, page table setup, and the transition to kernel memory management.

## Table of Contents

1. [Overview](#overview)
2. [EFI Memory Types and Classification](#efi-memory-types-and-classification)
3. [Memory Map Acquisition Process](#memory-map-acquisition-process)
4. [Memory Allocation Patterns](#memory-allocation-patterns)
5. [Page Table Setup and Architecture](#page-table-setup-and-architecture)
6. [Memory Model Transition](#memory-model-transition)
7. [Kernel Memory Management Handoff](#kernel-memory-management-handoff)
8. [Key Data Structures](#key-data-structures)
9. [Source File Analysis](#source-file-analysis)

## Overview

Haiku's UEFI boot loader implements a sophisticated memory management system that bridges UEFI firmware memory models with kernel virtual memory management. The implementation handles multiple phases: EFI memory map analysis, page table generation, EFI exit procedures, and kernel memory handoff.

The key challenge is transitioning from UEFI's identity-mapped physical memory model to the kernel's virtual memory model while maintaining system stability and ensuring all hardware resources are properly mapped.

## EFI Memory Types and Classification

### EFI Memory Type Definitions

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp:46-83`

```cpp
static const char* memory_region_type_str(int type)
{
    switch (type) {
        case EfiReservedMemoryType:      // System reserved, do not use
        case EfiLoaderCode:              // Boot loader executable code
        case EfiLoaderData:              // Boot loader data and allocated memory
        case EfiBootServicesCode:        // EFI boot services code
        case EfiBootServicesData:        // EFI boot services data
        case EfiRuntimeServicesCode:     // EFI runtime services code (persistent)
        case EfiRuntimeServicesData:     // EFI runtime services data (persistent)
        case EfiConventionalMemory:      // Available physical memory
        case EfiUnusableMemory:          // Defective memory, do not use
        case EfiACPIReclaimMemory:       // ACPI tables (can be reclaimed)
        case EfiACPIMemoryNVS:           // ACPI Non-Volatile Storage
        case EfiMemoryMappedIO:          // Memory-mapped I/O regions
        case EfiMemoryMappedIOPortSpace: // Memory-mapped I/O port space
        case EfiPalCode:                 // Platform Abstraction Layer code
        case EfiPersistentMemory:        // Non-volatile memory (NVDIMM)
    }
}
```

### Memory Classification Logic

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_mmu.cpp:84-122`

**Usable Memory Types (Available for kernel):**
```cpp
case EfiLoaderCode:           // Bootloader code - reclaimable after boot
case EfiLoaderData:           // Bootloader data - reclaimable after boot  
case EfiBootServicesCode:     // EFI boot services - reclaimable after ExitBootServices
case EfiBootServicesData:     // EFI boot services data - reclaimable after ExitBootServices
case EfiConventionalMemory:   // Standard available memory
```

**Persistent Memory Types (Must remain mapped):**
```cpp
case EfiRuntimeServicesCode:  // EFI runtime services - needed post-boot
case EfiRuntimeServicesData:  // EFI runtime data - needed post-boot
```

**Reserved/Special Memory Types:**
```cpp
case EfiACPIReclaimMemory:    // ACPI tables - potentially reclaimable
case EfiACPIMemoryNVS:        // ACPI NVS - must preserve
case EfiMemoryMappedIO:       // Hardware I/O regions
case EfiReservedMemoryType:   // System reserved regions
case EfiUnusableMemory:       // Defective memory
```

## Memory Map Acquisition Process

### Phase 1: Memory Map Size Determination

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp:91-100`

```cpp
size_t memory_map_size = 0;
efi_memory_descriptor dummy;
size_t map_key;
size_t descriptor_size;
uint32_t descriptor_version;

// First call determines required buffer size
if (kBootServices->GetMemoryMap(&memory_map_size, &dummy, &map_key,
    &descriptor_size, &descriptor_version) != EFI_BUFFER_TOO_SMALL) {
    panic("Unable to determine size of system memory map");
}
```

**Purpose:** EFI GetMemoryMap() requires knowing the buffer size beforehand. This call deliberately fails with EFI_BUFFER_TOO_SMALL to get the required size.

### Phase 2: Buffer Allocation and Map Retrieval

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp:102-116`

```cpp
// Allocate buffer twice as large as needed for potential growth
size_t actual_memory_map_size = memory_map_size * 2;
efi_memory_descriptor *memory_map = 
    (efi_memory_descriptor *)kernel_args_malloc(actual_memory_map_size);

// Get actual memory map
memory_map_size = actual_memory_map_size;
if (kBootServices->GetMemoryMap(&memory_map_size, memory_map, &map_key,
    &descriptor_size, &descriptor_version) != EFI_SUCCESS) {
    panic("Unable to fetch system memory map.");
}
```

**Key Design Decision:** 2x buffer size allocation accounts for memory map changes during ExitBootServices() when additional allocations cannot be made.

### Phase 3: Memory Map Analysis and Logging

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp:118-130`

```cpp
addr_t addr = (addr_t)memory_map;
dprintf("System provided memory map:\n");
for (size_t i = 0; i < memory_map_size / descriptor_size; i++) {
    efi_memory_descriptor *entry = (efi_memory_descriptor *)(addr + i * descriptor_size);
    dprintf("  phys: 0x%08lx-0x%08lx, virt: 0x%08lx-0x%08lx, type: %s (%#x), attr: %#lx\n",
        entry->PhysicalStart,
        entry->PhysicalStart + entry->NumberOfPages * B_PAGE_SIZE,
        entry->VirtualStart,
        entry->VirtualStart + entry->NumberOfPages * B_PAGE_SIZE,
        memory_region_type_str(entry->Type), entry->Type,
        entry->Attribute);
}
```

**EFI Memory Descriptor Structure:**
- **PhysicalStart:** Physical base address of memory region
- **VirtualStart:** Virtual address (initially equals PhysicalStart)
- **NumberOfPages:** Size in 4KB pages
- **Type:** Memory type enum (see classification above)
- **Attribute:** Memory attributes (cacheable, write-protect, etc.)

## Memory Allocation Patterns

### Bootloader Memory Allocation Strategy

**File:** `src/system/boot/platform/efi/mmu.cpp:96-146`

```cpp
extern "C" status_t platform_allocate_region(void **_address, size_t size, uint8 protection)
{
    size_t pages = ROUNDUP(size, B_PAGE_SIZE) / B_PAGE_SIZE;
    efi_physical_addr addr = 0;
    
    // Allocate through EFI boot services
    efi_status status = kBootServices->AllocatePages(AllocateAnyPages,
        EfiLoaderData, pages, &addr);
    
    // Addresses above 512GB not supported
    if (addr + size > (512ull * 1024 * 1024 * 1024))
        panic("Can't currently support more than 512GB of RAM!");
        
    // Track allocation in linked list
    memory_region *region = new(std::nothrow) memory_region {
        next: allocated_regions,
        vaddr: 0,
        paddr: (phys_addr_t)addr,
        size: size,
    };
}
```

### Allocation Types and Strategies

**1. General Purpose Allocation:**
```cpp
platform_allocate_region()  // Uses AllocateAnyPages
```

**2. Address-Constrained Allocation:**
```cpp
platform_allocate_region_below()  // Uses AllocateMaxAddress for specific requirements
```

**3. Page Table Allocation:**
```cpp
mmu_allocate_page()  // Single page allocation for page table structures
```

**Memory Region Tracking:**
```cpp
struct memory_region {
    memory_region *next;    // Linked list of allocations
    addr_t vaddr;          // Virtual address (bootloader space)
    phys_addr_t paddr;     // Physical address
    size_t size;           // Allocation size
};
```

### Virtual Address Space Layout

**File:** `src/system/boot/platform/efi/mmu.cpp:45-46`

```cpp
static const size_t kMaxKernelSize = 0x2000000;    // 32 MB kernel size limit
static addr_t sNextVirtualAddress = KERNEL_LOAD_BASE + kMaxKernelSize;
```

**Address Space Organization:**
- **KERNEL_LOAD_BASE:** Base virtual address for kernel (typically 0xFFFFFF0000000000)
- **KERNEL_LOAD_BASE + kMaxKernelSize:** Start of bootloader dynamic allocations
- **sNextVirtualAddress:** Current allocation pointer, grows upward

## Page Table Setup and Architecture

### x86_64 Page Table Hierarchy

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_mmu.cpp:191-308`

```
4-Level Page Table Structure:
PML4 (Page Map Level 4) - 512GB per entry, 512 entries total
 └── PDPT (Page Directory Pointer Table) - 1GB per entry, 512 entries per PML4
     └── PD (Page Directory) - 2MB per entry, 512 entries per PDPT  
         └── PT (Page Table) - 4KB per entry, 512 entries per PD
```

### Page Table Generation Process

**Phase 1: PML4 Allocation**
```cpp
uint64_t *pml4 = NULL;
if (platform_allocate_region_below((void**)&pml4, B_PAGE_SIZE, UINT32_MAX) != B_OK)
    panic("Failed to allocate PML4.");
    
gKernelArgs.arch_args.phys_pgdir = (uint32_t)(addr_t)pml4;
memset(pml4, 0, B_PAGE_SIZE);
```

**Critical Constraint:** PML4 must be below 4GB because SMP trampoline loads it in 32-bit mode.

**Phase 2: Physical Memory Mapping**
```cpp
// Find maximum physical address to map
uint64 maxAddress = 0;
for (size_t i = 0; i < memory_map_size / descriptor_size; ++i) {
    efi_memory_descriptor *entry = ...;
    switch (entry->Type) {
        case EfiLoaderCode:
        case EfiLoaderData:
        case EfiBootServicesCode:
        case EfiBootServicesData:
        case EfiConventionalMemory:
        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
        case EfiPersistentMemory:
        case EfiACPIReclaimMemory:
        case EfiACPIMemoryNVS:
            maxAddress = std::max(maxAddress,
                entry->PhysicalStart + entry->NumberOfPages * 4096);
            break;
    }
}

// Ensure at least 4GB is mapped
maxAddress = std::max(maxAddress, (uint64)0x100000000ll);
maxAddress = ROUNDUP(maxAddress, 0x40000000);  // Round to 1GB boundary
```

**Phase 3: Physical Map Page Table Creation**
```cpp
// Create PDPT for physical memory mapping
pdpt = (uint64*)mmu_allocate_page();
memset(pdpt, 0, B_PAGE_SIZE);
pml4[510] = (addr_t)pdpt | kTableMappingFlags;  // Physical map area
pml4[0] = (addr_t)pdpt | kTableMappingFlags;    // Identity mapping (temporary)

// Create page directories for each 1GB region
for (uint64 i = 0; i < maxAddress; i += 0x40000000) {
    pageDir = (uint64*)mmu_allocate_page();
    memset(pageDir, 0, B_PAGE_SIZE);
    pdpt[i / 0x40000000] = (addr_t)pageDir | kTableMappingFlags;
    
    // Use 2MB large pages for physical mapping
    for (uint64 j = 0; j < 0x40000000; j += 0x200000) {
        pageDir[j / 0x200000] = (i + j) | kLargePageMappingFlags;
    }
}
```

**Phase 4: Kernel Virtual Memory Mapping**
```cpp
// Allocate kernel space page tables
pdpt = (uint64*)mmu_allocate_page();
memset(pdpt, 0, B_PAGE_SIZE);
pml4[511] = (addr_t)pdpt | kTableMappingFlags;  // Kernel space

pageDir = (uint64*)mmu_allocate_page();  
memset(pageDir, 0, B_PAGE_SIZE);
pdpt[510] = (addr_t)pageDir | kTableMappingFlags;

// Map kernel pages with 4KB granularity
for (uint32 i = 0; i < gKernelArgs.virtual_allocated_range[0].size / B_PAGE_SIZE; i++) {
    if ((i % 512) == 0) {
        pageTable = (uint64*)mmu_allocate_page();
        memset(pageTable, 0, B_PAGE_SIZE);
        pageDir[i / 512] = (addr_t)pageTable | kTableMappingFlags;
    }
    
    void *phys;
    if (platform_kernel_address_to_bootloader_address(
        KERNEL_LOAD_BASE + (i * B_PAGE_SIZE), &phys) != B_OK) {
        continue;
    }
    
    pageTable[i % 512] = (addr_t)phys | kPageMappingFlags;
}
```

### Page Table Mapping Flags

**File:** `src/system/boot/platform/efi/mmu.h:22-29`

```cpp
static const uint64 kTableMappingFlags = 0x7;      // present, R/W, user
static const uint64 kLargePageMappingFlags = 0x183; // present, R/W, user, global, large
static const uint64 kPageMappingFlags = 0x103;     // present, R/W, user, global
```

**Flag Breakdown:**
- **Bit 0 (Present):** Page is present in memory
- **Bit 1 (R/W):** Page is writable
- **Bit 2 (User):** Page accessible from user mode
- **Bit 7 (Large):** 2MB page size (vs 4KB)
- **Bit 8 (Global):** Page not flushed on CR3 reload

## Memory Model Transition

### EFI Boot Services Exit Critical Section

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp:147-163`

```cpp
dprintf("Calling ExitBootServices. So long, EFI!\n");
serial_disable();

while (true) {
    if (kBootServices->ExitBootServices(kImage, map_key) == EFI_SUCCESS) {
        serial_kernel_handoff();
        dprintf("Unhooked from EFI serial services\n"); 
        break;
    }
    
    // Memory map may have changed, retry with updated map
    memory_map_size = actual_memory_map_size;
    if (kBootServices->GetMemoryMap(&memory_map_size, memory_map, &map_key,
            &descriptor_size, &descriptor_version) != EFI_SUCCESS) {
        panic("Unable to fetch system memory map.");
    }
}
```

**Critical Timing Issues:**
1. **map_key Validation:** EFI firmware invalidates map_key if memory layout changes
2. **No More Allocations:** After first ExitBootServices() call, no EFI memory services available
3. **Retry Logic:** Must retry with updated memory map if ExitBootServices() fails
4. **Service Disconnection:** Serial and console services become unavailable

### Post-EFI Memory Setup

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp:165-172`

```cpp
// Update EFI, generate final kernel physical memory map
arch_mmu_post_efi_setup(memory_map_size, memory_map, descriptor_size, descriptor_version);

// Re-initialize serial in post-EFI environment
serial_init();
serial_enable();
```

### EFI Runtime Services Virtual Mapping

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_mmu.cpp:155-157`

```cpp
// Switch EFI to virtual mode, using the kernel page tables
kRuntimeServices->SetVirtualAddressMap(memory_map_size, descriptor_size,
    descriptor_version, memory_map);
```

**EFI Runtime Services Transition:**
- **Before:** EFI runtime services use identity mapping (phys == virt)
- **After:** EFI runtime services use kernel virtual mapping
- **Requirement:** EFI runtime code/data regions must have VirtualStart set

## Kernel Memory Management Handoff

### Physical Memory Range Processing

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_mmu.cpp:78-122`

**Two-Pass Processing Algorithm:**

**Pass 1: Add Usable Memory Ranges**
```cpp
for (size_t i = 0; i < memory_map_size / descriptor_size; ++i) {
    efi_memory_descriptor *entry = (efi_memory_descriptor *)(addr + i * descriptor_size);
    switch (entry->Type) {
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiConventionalMemory: {
        uint64_t base = entry->PhysicalStart;
        uint64_t end = entry->PhysicalStart + entry->NumberOfPages * 4096;
        
        // Apply memory constraints
        if (base < 0x100000)              // Ignore memory below 1MB
            base = 0x100000;
        if (end > (512ull * 1024 * 1024 * 1024))  // Ignore memory above 512GB
            end = 512ull * 1024 * 1024 * 1024;
            
        if (base >= end) break;
        uint64_t size = end - base;
        
        insert_physical_memory_range(base, size);
        
        // Mark bootloader allocations as pre-allocated
        if (entry->Type == EfiLoaderData)
            insert_physical_allocated_range(base, size);
        break;
    }
}
```

**Pass 2: Remove Reserved Memory Ranges**
```cpp
for (size_t i = 0; i < memory_map_size / descriptor_size; ++i) {
    efi_memory_descriptor *entry = (efi_memory_descriptor *)(addr + i * descriptor_size);
    switch (entry->Type) {
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiConventionalMemory:
        break;  // Already processed in pass 1
    default:
        // Remove any overlap with reserved regions
        uint64_t base = entry->PhysicalStart;
        uint64_t end = entry->PhysicalStart + entry->NumberOfPages * 4096;
        remove_physical_memory_range(base, end - base);
    }
}
```

### Kernel Arguments Structure Population

**File:** `headers/private/kernel/boot/kernel_args.h:45-112`

**Physical Memory Information:**
```cpp
typedef struct kernel_args {
    uint32 num_physical_memory_ranges;
    addr_range physical_memory_range[MAX_PHYSICAL_MEMORY_RANGE];
    uint32 num_physical_allocated_ranges;
    addr_range physical_allocated_range[MAX_PHYSICAL_ALLOCATED_RANGE];
    uint32 num_virtual_allocated_ranges;
    addr_range virtual_allocated_range[MAX_VIRTUAL_ALLOCATED_RANGE];
    uint64 ignored_physical_memory;
    
    // Architecture-specific arguments
    arch_kernel_args arch_args;
} kernel_args;
```

**Architecture-Specific Arguments:**
```cpp
gKernelArgs.arch_args.phys_pgdir = (uint32_t)(addr_t)pml4;   // Physical PML4 address
gKernelArgs.arch_args.vir_pgdir = ...;                      // Virtual PML4 address  
gKernelArgs.arch_args.virtual_end = ROUNDUP(KERNEL_LOAD_BASE + 
    gKernelArgs.virtual_allocated_range[0].size, 0x200000);  // Virtual memory end
```

### Final Memory Layout Before Kernel Entry

**Address Space Layout:**
```
0x0000000000000000 - 0x0000007FFFFFFFFF : Identity mapping (temporary)
0xFFFF800000000000 - 0xFFFF9FFFFFFFFFFF : Physical memory map (510th PML4 entry)
0xFFFFFF0000000000 - 0xFFFFFFFFFFFFFFFF : Kernel virtual space (511th PML4 entry)
```

**Memory Range Types Passed to Kernel:**
1. **physical_memory_range[]:** Available physical memory for kernel allocation
2. **physical_allocated_range[]:** Pre-allocated physical memory (bootloader, kernel)
3. **virtual_allocated_range[]:** Used virtual memory in kernel space
4. **ignored_physical_memory:** Memory that was ignored due to constraints

### Address Translation Functions

**File:** `src/system/boot/platform/efi/mmu.cpp:149-200`

**Bootloader to Kernel Address Translation:**
```cpp
extern "C" status_t platform_bootloader_address_to_kernel_address(void *address, addr_t *_result)
{
    // Search allocated regions for physical address
    for (memory_region *region = allocated_regions; region != NULL; region = region->next) {
        if (region->matches((phys_addr_t)address, ...)) {
            if (region->vaddr != 0) {
                *_result = region->vaddr + offset;
                return B_OK;
            }
        }
    }
    return B_ERROR;
}
```

**Kernel to Bootloader Address Translation:**
```cpp
extern "C" status_t platform_kernel_address_to_bootloader_address(addr_t address, void **_result)
{
    // Reverse lookup: kernel virtual → bootloader physical
    for (memory_region *region = allocated_regions; region != NULL; region = region->next) {
        if (address >= region->vaddr && address < region->vaddr + region->size) {
            *_result = (void *)(region->paddr + (address - region->vaddr));
            return B_OK;
        }
    }
    return B_ERROR;
}
```

## Key Data Structures

### EFI Memory Descriptor

**File:** `headers/private/kernel/platform/efi/types.h:140-141`

```cpp
typedef struct {
    efi_memory_type Type;           // Memory type enum
    efi_physical_addr PhysicalStart; // Physical base address
    efi_virtual_addr VirtualStart;   // Virtual base address (initially == PhysicalStart)
    uint64_t NumberOfPages;         // Size in 4KB pages
    uint64_t Attribute;             // Memory attributes bitfield
} efi_memory_descriptor;
```

### Memory Region Tracking

**File:** `src/system/boot/platform/efi/mmu.cpp:28-42`

```cpp
struct memory_region {
    memory_region *next;    // Linked list pointer
    addr_t vaddr;          // Virtual address (bootloader space)
    phys_addr_t paddr;     // Physical address
    size_t size;           // Region size in bytes
    
    bool matches(phys_addr_t expected_paddr, size_t expected_size);
    void dprint(const char *msg);  // Debug output
};
```

### Kernel Arguments Address Ranges

**File:** `headers/private/kernel/boot/kernel_args.h:52-60`

```cpp
typedef struct {
    addr_t start;   // Base address
    addr_t size;    // Range size
} addr_range;

// Memory range arrays in kernel_args
addr_range physical_memory_range[MAX_PHYSICAL_MEMORY_RANGE];      // Available memory
addr_range physical_allocated_range[MAX_PHYSICAL_ALLOCATED_RANGE]; // Pre-allocated memory  
addr_range virtual_allocated_range[MAX_VIRTUAL_ALLOCATED_RANGE];   // Used virtual memory
```

## Source File Analysis

### Primary Memory Management Files

| File | Purpose | Key Functions |
|------|---------|---------------|
| `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp` | Memory map acquisition, EFI exit | `arch_start_kernel()`, `memory_region_type_str()` |
| `src/system/boot/platform/efi/arch/x86_64/arch_mmu.cpp` | Page table generation, post-EFI setup | `arch_mmu_generate_post_efi_page_tables()`, `arch_mmu_post_efi_setup()` |
| `src/system/boot/platform/efi/mmu.cpp` | Platform memory allocation | `platform_allocate_region()`, `mmu_allocate_page()` |
| `src/system/boot/platform/efi/mmu.h` | MMU interface definitions | Page table flags, address translation prototypes |

### Supporting Infrastructure

| File | Purpose |
|------|---------|
| `headers/private/kernel/platform/efi/types.h` | EFI type definitions and memory type enums |
| `headers/private/kernel/boot/kernel_args.h` | Kernel arguments structure definition |
| `src/system/boot/platform/efi/efi_platform.h` | EFI platform global variables |

### Memory Range Management

| File | Purpose |
|------|---------|
| `src/system/boot/loader/memory.cpp` | Generic memory range insertion/removal |
| `src/system/boot/platform/generic/memory.cpp` | Platform-independent memory utilities |

## Conclusion

Haiku's UEFI memory management implementation demonstrates sophisticated handling of the complex transition from firmware to kernel memory models. Key architectural decisions include:

1. **Two-Phase Memory Map Processing:** First gathering usable ranges, then removing reserved regions
2. **512GB Memory Limit:** Practical constraint for current page table implementation  
3. **2x Buffer Allocation:** Defensive programming for memory map changes during EFI exit
4. **Dual Address Space Management:** Bootloader and kernel address spaces with translation functions
5. **Large Page Optimization:** 2MB pages for physical memory mapping, 4KB pages for kernel space
6. **EFI Runtime Services Continuity:** Proper virtual address mapping for post-boot EFI services

The implementation provides a robust foundation for ARM64 adaptation, with clear separation between architecture-independent memory management logic and x86_64-specific page table structures.

---

*This documentation was generated as part of the Haiku ARM64 architecture support analysis project.*