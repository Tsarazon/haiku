/*
 * Copyright 2025, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _SYSTEM_KERNEL_ARCH_ARM64_ARCH_VM_TYPES_H
#define _SYSTEM_KERNEL_ARCH_ARM64_ARCH_VM_TYPES_H

#include <SupportDefs.h>

// ARM64 VMSAv8-64 Architecture Virtual Memory Types

// Page sizes and granules
#define ARM64_PAGE_SIZE_4K		4096
#define ARM64_PAGE_SIZE_16K		16384
#define ARM64_PAGE_SIZE_64K		65536

// Default page configuration
#define ARM64_PAGE_SIZE			ARM64_PAGE_SIZE_4K
#define ARM64_PAGE_SHIFT		12
#define ARM64_PAGE_MASK			(ARM64_PAGE_SIZE - 1)

// Translation granule bits
#define ARM64_GRANULE_4K_BITS	9		// 512 entries per table
#define ARM64_GRANULE_16K_BITS	11		// 2048 entries per table  
#define ARM64_GRANULE_64K_BITS	13		// 8192 entries per table

// Default granule configuration (4KB pages)
#define ARM64_GRANULE_BITS		ARM64_GRANULE_4K_BITS
#define ARM64_ENTRIES_PER_TABLE	(1 << ARM64_GRANULE_BITS)

// Virtual address space configuration
#define ARM64_VA_BITS_39		39
#define ARM64_VA_BITS_48		48
#define ARM64_VA_BITS_DEFAULT	ARM64_VA_BITS_48

// ARM64 48-bit Virtual Address Space Layout
// The ARMv8 architecture splits the 64-bit virtual address space into two regions:
// - Lower half (TTBR0_EL1): User space - 0x0000000000000000 to 0x0000FFFFFFFFFFFF
// - Upper half (TTBR1_EL1): Kernel space - 0xFFFF000000000000 to 0xFFFFFFFFFFFFFFFF
// The gap in the middle (0x0001000000000000 to 0xFFFEFFFFFFFFFFFF) causes translation faults

// User space (TTBR0_EL1) - Lower 48 bits of virtual address space
#define ARM64_USER_BASE			0x0000000000000000UL
#define ARM64_USER_TOP			0x0000FFFFFFFFFFFFUL
#define ARM64_USER_SIZE			0x0001000000000000UL	// 256TB

// Kernel space (TTBR1_EL1) - Upper 48 bits of virtual address space  
#define ARM64_KERNEL_BASE		0xFFFF000000000000UL
#define ARM64_KERNEL_TOP		0xFFFFFFFFFFFFFFFFUL
#define ARM64_KERNEL_SIZE		0x0001000000000000UL	// 256TB

// Virtual address space regions within kernel space
#define ARM64_KERNEL_SPACE_BASE		ARM64_KERNEL_BASE
#define ARM64_KERNEL_SPACE_SIZE		ARM64_KERNEL_SIZE

// Direct physical memory mapping region (first part of kernel space)
// Maps all physical RAM with a fixed offset for fast kernel access
#define ARM64_PHYSMAP_BASE		0xFFFF000000000000UL	// Start of kernel space
#define ARM64_PHYSMAP_SIZE		0x0000800000000000UL	// 128TB for physical mapping
#define ARM64_PHYSMAP_TOP		(ARM64_PHYSMAP_BASE + ARM64_PHYSMAP_SIZE - 1)

// Kernel heap region
#define ARM64_KERNEL_HEAP_BASE	0xFFFF800000000000UL	// After physmap
#define ARM64_KERNEL_HEAP_SIZE	0x0000400000000000UL	// 64TB for kernel heap
#define ARM64_KERNEL_HEAP_TOP	(ARM64_KERNEL_HEAP_BASE + ARM64_KERNEL_HEAP_SIZE - 1)

// Kernel modules region
#define ARM64_KERNEL_MODULES_BASE	0xFFFFC00000000000UL	// After heap
#define ARM64_KERNEL_MODULES_SIZE	0x0000200000000000UL	// 32TB for modules
#define ARM64_KERNEL_MODULES_TOP	(ARM64_KERNEL_MODULES_BASE + ARM64_KERNEL_MODULES_SIZE - 1)

// Kernel text/data region (loaded kernel image)
#define ARM64_KERNEL_TEXT_BASE	0xFFFFE00000000000UL	// After modules
#define ARM64_KERNEL_TEXT_SIZE	0x0000100000000000UL	// 16TB for kernel image
#define ARM64_KERNEL_TEXT_TOP	(ARM64_KERNEL_TEXT_BASE + ARM64_KERNEL_TEXT_SIZE - 1)

// Device/MMIO mapping region
#define ARM64_DEVICE_BASE		0xFFFFF00000000000UL	// After kernel text
#define ARM64_DEVICE_SIZE		0x00000F0000000000UL	// 15TB for device mappings
#define ARM64_DEVICE_TOP		(ARM64_DEVICE_BASE + ARM64_DEVICE_SIZE - 1)

// Reserved region at top of kernel space
#define ARM64_KERNEL_RESERVED_BASE	0xFFFFFF0000000000UL
#define ARM64_KERNEL_RESERVED_SIZE	0x0000010000000000UL	// 1TB reserved
#define ARM64_KERNEL_RESERVED_TOP	ARM64_KERNEL_TOP

// Translation Control Register (TCR_EL1) configuration for 48-bit VA
#define ARM64_TCR_T0SZ_48BIT		16		// 64 - 48 = 16 (for TTBR0)
#define ARM64_TCR_T1SZ_48BIT		16		// 64 - 48 = 16 (for TTBR1)

// Standard TCR_EL1 configuration for 48-bit addressing with 4KB pages
#define ARM64_TCR_EL1_48BIT_4K_VALUE \
	((ARM64_TCR_T0SZ_48BIT << ARM64_TCR_T0SZ_SHIFT) | \
	 (ARM64_TCR_T1SZ_48BIT << ARM64_TCR_T1SZ_SHIFT) | \
	 ARM64_TCR_TG0_4K | ARM64_TCR_TG1_4K | \
	 ARM64_TCR_SH0_INNER | ARM64_TCR_SH1_INNER | \
	 ARM64_TCR_ORGN0_WB_WA | ARM64_TCR_ORGN1_WB_WA | \
	 ARM64_TCR_IRGN0_WB_WA | ARM64_TCR_IRGN1_WB_WA)

// Page table levels for 48-bit VA with 4KB pages
#define ARM64_48BIT_4K_START_LEVEL	0		// Start at level 0 (4 levels: 0-3)
#define ARM64_48BIT_4K_LEVELS		4		// 4 translation levels

// Virtual address bit allocation for 48-bit VA with 4KB pages
// VA[47:39] - Level 0 index (9 bits)
// VA[38:30] - Level 1 index (9 bits)  
// VA[29:21] - Level 2 index (9 bits)
// VA[20:12] - Level 3 index (9 bits)
// VA[11:0]  - Page offset (12 bits)

#define ARM64_VA_LEVEL0_SHIFT		39
#define ARM64_VA_LEVEL1_SHIFT		30
#define ARM64_VA_LEVEL2_SHIFT		21
#define ARM64_VA_LEVEL3_SHIFT		12
#define ARM64_VA_PAGE_SHIFT			12

#define ARM64_VA_LEVEL0_MASK		0x1FFUL		// 9 bits
#define ARM64_VA_LEVEL1_MASK		0x1FFUL		// 9 bits
#define ARM64_VA_LEVEL2_MASK		0x1FFUL		// 9 bits
#define ARM64_VA_LEVEL3_MASK		0x1FFUL		// 9 bits
#define ARM64_VA_PAGE_MASK			0xFFFUL		// 12 bits

// Address validation macros
#define ARM64_IS_USER_ADDRESS(va) \
	(((va) >= ARM64_USER_BASE) && ((va) <= ARM64_USER_TOP))

#define ARM64_IS_KERNEL_ADDRESS(va) \
	(((va) >= ARM64_KERNEL_BASE) && ((va) <= ARM64_KERNEL_TOP))

#define ARM64_IS_PHYSMAP_ADDRESS(va) \
	(((va) >= ARM64_PHYSMAP_BASE) && ((va) <= ARM64_PHYSMAP_TOP))

#define ARM64_IS_KERNEL_HEAP_ADDRESS(va) \
	(((va) >= ARM64_KERNEL_HEAP_BASE) && ((va) <= ARM64_KERNEL_HEAP_TOP))

#define ARM64_IS_DEVICE_ADDRESS(va) \
	(((va) >= ARM64_DEVICE_BASE) && ((va) <= ARM64_DEVICE_TOP))

// Address space conversion macros
#define ARM64_PHYS_TO_PHYSMAP(pa) \
	((addr_t)(pa) + ARM64_PHYSMAP_BASE)

#define ARM64_PHYSMAP_TO_PHYS(va) \
	((phys_addr_t)((va) - ARM64_PHYSMAP_BASE))

// TTBR register manipulation macros for 48-bit addressing
#define ARM64_TTBR_BADDR_48BIT_MASK	0x0000FFFFFFFFF000UL	// Bits [47:12] for base address
#define ARM64_TTBR_ASID_48BIT_MASK	0xFFFF000000000000UL	// Bits [63:48] for ASID

#define ARM64_TTBR_SET_BADDR(ttbr, addr) \
	(((ttbr) & ~ARM64_TTBR_BADDR_48BIT_MASK) | ((addr) & ARM64_TTBR_BADDR_48BIT_MASK))

#define ARM64_TTBR_SET_ASID(ttbr, asid) \
	(((ttbr) & ~ARM64_TTBR_ASID_48BIT_MASK) | (((uint64)(asid) << 48) & ARM64_TTBR_ASID_48BIT_MASK))

#define ARM64_TTBR_GET_BADDR(ttbr) \
	((ttbr) & ARM64_TTBR_BADDR_48BIT_MASK)

#define ARM64_TTBR_GET_ASID(ttbr) \
	(((ttbr) & ARM64_TTBR_ASID_48BIT_MASK) >> 48)

// Memory region descriptors for Haiku VM integration
typedef struct {
	addr_t		base;		// Base virtual address
	size_t		size;		// Size of region
	const char*	name;		// Region name for debugging
	uint32		flags;		// Region-specific flags
} arm64_vm_region_t;

// Predefined kernel memory regions
#define ARM64_VM_REGIONS \
	{ ARM64_PHYSMAP_BASE, ARM64_PHYSMAP_SIZE, "Physical Memory Map", 0 }, \
	{ ARM64_KERNEL_HEAP_BASE, ARM64_KERNEL_HEAP_SIZE, "Kernel Heap", 0 }, \
	{ ARM64_KERNEL_MODULES_BASE, ARM64_KERNEL_MODULES_SIZE, "Kernel Modules", 0 }, \
	{ ARM64_KERNEL_TEXT_BASE, ARM64_KERNEL_TEXT_SIZE, "Kernel Text", 0 }, \
	{ ARM64_DEVICE_BASE, ARM64_DEVICE_SIZE, "Device/MMIO", 0 }

// Page table allocation helpers for 48-bit addressing
#define ARM64_PAGES_PER_L0_TABLE	512		// 2^9 entries
#define ARM64_PAGES_PER_L1_TABLE	512		// 2^9 entries  
#define ARM64_PAGES_PER_L2_TABLE	512		// 2^9 entries
#define ARM64_PAGES_PER_L3_TABLE	512		// 2^9 entries

// Coverage calculations for each level (4KB pages, 48-bit VA)
#define ARM64_L0_COVERAGE		(1UL << 39)	// 512GB per L0 entry
#define ARM64_L1_COVERAGE		(1UL << 30)	// 1GB per L1 entry
#define ARM64_L2_COVERAGE		(1UL << 21)	// 2MB per L2 entry
#define ARM64_L3_COVERAGE		(1UL << 12)	// 4KB per L3 entry

// Block size definitions for different levels
#define ARM64_L1_BLOCK_SIZE		ARM64_L1_COVERAGE		// 1GB blocks
#define ARM64_L2_BLOCK_SIZE		ARM64_L2_COVERAGE		// 2MB blocks
#define ARM64_L3_PAGE_SIZE		ARM64_L3_COVERAGE		// 4KB pages

// Maximum number of page table entries needed for full address space
#define ARM64_MAX_L0_ENTRIES	((ARM64_KERNEL_SIZE + ARM64_L0_COVERAGE - 1) / ARM64_L0_COVERAGE)
#define ARM64_MAX_L1_ENTRIES	((ARM64_KERNEL_SIZE + ARM64_L1_COVERAGE - 1) / ARM64_L1_COVERAGE)
#define ARM64_MAX_L2_ENTRIES	((ARM64_KERNEL_SIZE + ARM64_L2_COVERAGE - 1) / ARM64_L2_COVERAGE)
#define ARM64_MAX_L3_ENTRIES	((ARM64_KERNEL_SIZE + ARM64_L3_COVERAGE - 1) / ARM64_L3_COVERAGE)

// Translation table descriptor types (bits [1:0])
#define ARM64_DESC_TYPE_MASK	0x3UL
#define ARM64_DESC_TYPE_FAULT	0x0UL	// Invalid/fault
#define ARM64_DESC_TYPE_BLOCK	0x1UL	// Block entry (levels 1-2)
#define ARM64_DESC_TYPE_TABLE	0x3UL	// Table entry (levels 0-2)
#define ARM64_DESC_TYPE_PAGE	0x3UL	// Page entry (level 3)

// Translation table entry structure (64-bit)
typedef uint64 arm64_pte_t;

// Page table entry bit definitions
#define ARM64_PTE_VALID			(1UL << 0)		// Valid entry
#define ARM64_PTE_TYPE_MASK		0x3UL			// Entry type mask

// Block/Page attributes (bits 11:2)
#define ARM64_PTE_ATTRINDX_MASK	(0x7UL << 2)	// Memory attribute index
#define ARM64_PTE_ATTRINDX_SHIFT 2
#define ARM64_PTE_NS			(1UL << 5)		// Non-secure
#define ARM64_PTE_AP_MASK		(0x3UL << 6)	// Access permissions
#define ARM64_PTE_AP_SHIFT		6
#define ARM64_PTE_SH_MASK		(0x3UL << 8)	// Shareability
#define ARM64_PTE_SH_SHIFT		8
#define ARM64_PTE_AF			(1UL << 10)	// Access flag
#define ARM64_PTE_NG			(1UL << 11)	// Not global

// Access permissions (AP bits 7:6)
#define ARM64_PTE_AP_RW_EL1		(0x0UL << 6)	// R/W at EL1, no access at EL0
#define ARM64_PTE_AP_RW_ALL		(0x1UL << 6)	// R/W at EL1/EL0
#define ARM64_PTE_AP_RO_EL1		(0x2UL << 6)	// RO at EL1, no access at EL0
#define ARM64_PTE_AP_RO_ALL		(0x3UL << 6)	// RO at EL1/EL0

// Shareability field (SH bits 9:8)
#define ARM64_PTE_SH_NON		(0x0UL << 8)	// Non-shareable
#define ARM64_PTE_SH_OUTER		(0x2UL << 8)	// Outer shareable
#define ARM64_PTE_SH_INNER		(0x3UL << 8)	// Inner shareable

// Upper attributes (bits 63:52)
#define ARM64_PTE_SOFTWARE_MASK	(0xFUL << 55)	// Software use bits
#define ARM64_PTE_UXN			(1UL << 54)	// User execute never
#define ARM64_PTE_PXN			(1UL << 53)	// Privileged execute never
#define ARM64_PTE_RESERVED_52	(1UL << 52)	// Reserved/implementation defined
#define ARM64_PTE_DBM			(1UL << 51)	// Dirty bit modifier (ARMv8.1+)

// Address masks
#define ARM64_PTE_ADDR_4K_MASK	(0x0000FFFFFFFFF000UL)	// 4KB page address
#define ARM64_PTE_ADDR_16K_MASK	(0x0000FFFFFFFFC000UL)	// 16KB page address  
#define ARM64_PTE_ADDR_64K_MASK	(0x0000FFFFFFFF0000UL)	// 64KB page address

// Default address mask (4KB pages)
#define ARM64_PTE_ADDR_MASK		ARM64_PTE_ADDR_4K_MASK

// Block size calculations for different levels and granules
#define ARM64_BLOCK_SIZE_1G		(1UL << 30)	// Level 1 block (1GB)
#define ARM64_BLOCK_SIZE_2M		(1UL << 21)	// Level 2 block (2MB)
#define ARM64_BLOCK_SIZE_4K		(1UL << 12)	// Level 3 page (4KB)

// Memory Attribute Indirection Register (MAIR_EL1) values
#define ARM64_MAIR_DEVICE_nGnRnE	0x00UL	// Device non-gathering, non-reordering, no early write ack
#define ARM64_MAIR_DEVICE_nGnRE		0x04UL	// Device non-gathering, non-reordering, early write ack
#define ARM64_MAIR_DEVICE_GRE		0x0CUL	// Device gathering, reordering, early write ack
#define ARM64_MAIR_NORMAL_NC		0x44UL	// Normal memory non-cacheable
#define ARM64_MAIR_NORMAL_WT		0xBBUL	// Normal memory write-through
#define ARM64_MAIR_NORMAL_WB		0xFFUL	// Normal memory write-back

// Standard MAIR_EL1 configuration
#define ARM64_MAIR_EL1_VALUE \
	((ARM64_MAIR_DEVICE_nGnRnE << (0 * 8)) | \
	 (ARM64_MAIR_DEVICE_nGnRE  << (1 * 8)) | \
	 (ARM64_MAIR_DEVICE_GRE    << (2 * 8)) | \
	 (ARM64_MAIR_NORMAL_NC     << (3 * 8)) | \
	 (ARM64_MAIR_NORMAL_WT     << (4 * 8)) | \
	 (ARM64_MAIR_NORMAL_WB     << (5 * 8)) | \
	 (ARM64_MAIR_NORMAL_WB     << (6 * 8)) | \
	 (ARM64_MAIR_NORMAL_WB     << (7 * 8)))

// Memory attribute indices for MAIR_EL1
#define ARM64_MAIR_IDX_DEVICE_nGnRnE	0
#define ARM64_MAIR_IDX_DEVICE_nGnRE		1
#define ARM64_MAIR_IDX_DEVICE_GRE		2
#define ARM64_MAIR_IDX_NORMAL_NC		3
#define ARM64_MAIR_IDX_NORMAL_WT		4
#define ARM64_MAIR_IDX_NORMAL_WB		5

// Translation Control Register (TCR_EL1) fields
#define ARM64_TCR_T0SZ_SHIFT		0		// Size offset for TTBR0_EL1
#define ARM64_TCR_T1SZ_SHIFT		16		// Size offset for TTBR1_EL1
#define ARM64_TCR_TG0_SHIFT			14		// TTBR0 granule size
#define ARM64_TCR_TG1_SHIFT			30		// TTBR1 granule size

#define ARM64_TCR_TG0_4K			(0x0UL << ARM64_TCR_TG0_SHIFT)
#define ARM64_TCR_TG0_16K			(0x2UL << ARM64_TCR_TG0_SHIFT)
#define ARM64_TCR_TG0_64K			(0x1UL << ARM64_TCR_TG0_SHIFT)

#define ARM64_TCR_TG1_4K			(0x2UL << ARM64_TCR_TG1_SHIFT)
#define ARM64_TCR_TG1_16K			(0x1UL << ARM64_TCR_TG1_SHIFT)
#define ARM64_TCR_TG1_64K			(0x3UL << ARM64_TCR_TG1_SHIFT)

// Shareability and cacheability for TCR_EL1
#define ARM64_TCR_SH0_NON			(0x0UL << 12)
#define ARM64_TCR_SH0_OUTER			(0x2UL << 12)
#define ARM64_TCR_SH0_INNER			(0x3UL << 12)

#define ARM64_TCR_SH1_NON			(0x0UL << 28)
#define ARM64_TCR_SH1_OUTER			(0x2UL << 28)
#define ARM64_TCR_SH1_INNER			(0x3UL << 28)

#define ARM64_TCR_ORGN0_NC			(0x0UL << 10)	// Outer non-cacheable
#define ARM64_TCR_ORGN0_WB_WA		(0x1UL << 10)	// Outer write-back write-allocate
#define ARM64_TCR_ORGN0_WT_NO_WA	(0x2UL << 10)	// Outer write-through no write-allocate
#define ARM64_TCR_ORGN0_WB_NO_WA	(0x3UL << 10)	// Outer write-back no write-allocate

#define ARM64_TCR_IRGN0_NC			(0x0UL << 8)	// Inner non-cacheable
#define ARM64_TCR_IRGN0_WB_WA		(0x1UL << 8)	// Inner write-back write-allocate
#define ARM64_TCR_IRGN0_WT_NO_WA	(0x2UL << 8)	// Inner write-through no write-allocate
#define ARM64_TCR_IRGN0_WB_NO_WA	(0x3UL << 8)	// Inner write-back no write-allocate

#define ARM64_TCR_ORGN1_NC			(0x0UL << 26)
#define ARM64_TCR_ORGN1_WB_WA		(0x1UL << 26)
#define ARM64_TCR_ORGN1_WT_NO_WA	(0x2UL << 26)
#define ARM64_TCR_ORGN1_WB_NO_WA	(0x3UL << 26)

#define ARM64_TCR_IRGN1_NC			(0x0UL << 24)
#define ARM64_TCR_IRGN1_WB_WA		(0x1UL << 24)
#define ARM64_TCR_IRGN1_WT_NO_WA	(0x2UL << 24)
#define ARM64_TCR_IRGN1_WB_NO_WA	(0x3UL << 24)

// Translation table level definitions
#define ARM64_TABLE_LEVEL_0		0
#define ARM64_TABLE_LEVEL_1		1
#define ARM64_TABLE_LEVEL_2		2
#define ARM64_TABLE_LEVEL_3		3
#define ARM64_MAX_TABLE_LEVELS	4

// ASID (Address Space Identifier) configuration
#define ARM64_ASID_BITS			8		// Typically 8 or 16 bits
#define ARM64_MAX_ASID			((1UL << ARM64_ASID_BITS) - 1)
#define ARM64_TTBR_ASID_SHIFT	48
#define ARM64_TTBR_ASID_MASK	(ARM64_MAX_ASID << ARM64_TTBR_ASID_SHIFT)
#define ARM64_TTBR_BADDR_MASK	(~ARM64_TTBR_ASID_MASK)

// Page table entry manipulation macros
#define ARM64_PTE_SET_ADDR(pte, addr) \
	((pte) = ((pte) & ~ARM64_PTE_ADDR_MASK) | ((addr) & ARM64_PTE_ADDR_MASK))

#define ARM64_PTE_GET_ADDR(pte) \
	((pte) & ARM64_PTE_ADDR_MASK)

#define ARM64_PTE_SET_MAIR_IDX(pte, idx) \
	((pte) = ((pte) & ~ARM64_PTE_ATTRINDX_MASK) | (((idx) << ARM64_PTE_ATTRINDX_SHIFT) & ARM64_PTE_ATTRINDX_MASK))

#define ARM64_PTE_GET_MAIR_IDX(pte) \
	(((pte) & ARM64_PTE_ATTRINDX_MASK) >> ARM64_PTE_ATTRINDX_SHIFT)

// Page table walking structure
typedef struct {
	int level;					// Current table level (0-3)
	int va_bits;				// Virtual address width
	int page_bits;				// Page size bits
	int granule_bits;			// Granule size bits
	phys_addr_t table_pa;		// Physical address of current table
	arm64_pte_t* table_va;		// Virtual address of current table
	uint64 index;				// Current entry index
	uint64 va_mask;				// VA mask for current level
	uint64 va_shift;			// VA shift for current level
} arm64_page_walk_t;

// Architecture-specific VM area flags
#define ARM64_AREA_DEVICE		(1 << 24)	// Device memory area
#define ARM64_AREA_STRONGLY_ORDERED (1 << 25)	// Strongly ordered memory
#define ARM64_AREA_NON_CACHEABLE (1 << 26)	// Non-cacheable memory

// TLB invalidation types
typedef enum {
	ARM64_TLB_INVALIDATE_ALL,		// Invalidate all TLB entries
	ARM64_TLB_INVALIDATE_ASID,		// Invalidate by ASID
	ARM64_TLB_INVALIDATE_VA,		// Invalidate by VA
	ARM64_TLB_INVALIDATE_VA_ASID	// Invalidate by VA and ASID
} arm64_tlb_invalidate_type_t;

// Cache maintenance types
typedef enum {
	ARM64_CACHE_CLEAN,				// Clean cache to PoC
	ARM64_CACHE_INVALIDATE,			// Invalidate cache
	ARM64_CACHE_CLEAN_INVALIDATE	// Clean and invalidate cache
} arm64_cache_op_t;

// Forward declarations
#ifdef __cplusplus
extern "C" {
#endif

// Helper functions for ARM64 page table entry creation and manipulation

// Page table entry creation functions
static inline arm64_pte_t
arm64_make_table_descriptor(phys_addr_t table_addr)
{
	return (table_addr & ARM64_PTE_ADDR_MASK) | ARM64_DESC_TYPE_TABLE | ARM64_PTE_VALID;
}

static inline arm64_pte_t
arm64_make_block_descriptor(phys_addr_t block_addr, uint32 mair_idx, uint32 permissions, uint32 shareability)
{
	arm64_pte_t pte = (block_addr & ARM64_PTE_ADDR_MASK) | ARM64_DESC_TYPE_BLOCK | ARM64_PTE_VALID;
	pte |= ((uint64)mair_idx << ARM64_PTE_ATTRINDX_SHIFT) & ARM64_PTE_ATTRINDX_MASK;
	pte |= ((uint64)permissions << ARM64_PTE_AP_SHIFT) & ARM64_PTE_AP_MASK;
	pte |= ((uint64)shareability << ARM64_PTE_SH_SHIFT) & ARM64_PTE_SH_MASK;
	pte |= ARM64_PTE_AF;  // Always set access flag
	return pte;
}

static inline arm64_pte_t
arm64_make_page_descriptor(phys_addr_t page_addr, uint32 mair_idx, uint32 permissions, uint32 shareability)
{
	arm64_pte_t pte = (page_addr & ARM64_PTE_ADDR_MASK) | ARM64_DESC_TYPE_PAGE | ARM64_PTE_VALID;
	pte |= ((uint64)mair_idx << ARM64_PTE_ATTRINDX_SHIFT) & ARM64_PTE_ATTRINDX_MASK;
	pte |= ((uint64)permissions << ARM64_PTE_AP_SHIFT) & ARM64_PTE_AP_MASK;
	pte |= ((uint64)shareability << ARM64_PTE_SH_SHIFT) & ARM64_PTE_SH_MASK;
	pte |= ARM64_PTE_AF;  // Always set access flag
	return pte;
}

// Page table entry validation functions
static inline bool
arm64_pte_is_valid(arm64_pte_t pte)
{
	return (pte & ARM64_PTE_VALID) != 0;
}

static inline bool
arm64_pte_is_table(arm64_pte_t pte)
{
	return arm64_pte_is_valid(pte) && ((pte & ARM64_DESC_TYPE_MASK) == ARM64_DESC_TYPE_TABLE);
}

static inline bool
arm64_pte_is_block(arm64_pte_t pte)
{
	return arm64_pte_is_valid(pte) && ((pte & ARM64_DESC_TYPE_MASK) == ARM64_DESC_TYPE_BLOCK);
}

static inline bool
arm64_pte_is_page(arm64_pte_t pte)
{
	return arm64_pte_is_valid(pte) && ((pte & ARM64_DESC_TYPE_MASK) == ARM64_DESC_TYPE_PAGE);
}

static inline bool
arm64_pte_is_fault(arm64_pte_t pte)
{
	return !arm64_pte_is_valid(pte);
}

// Address extraction functions
static inline phys_addr_t
arm64_pte_get_address(arm64_pte_t pte)
{
	return pte & ARM64_PTE_ADDR_MASK;
}

static inline phys_addr_t
arm64_pte_get_table_address(arm64_pte_t pte)
{
	return arm64_pte_is_table(pte) ? (pte & ARM64_PTE_ADDR_MASK) : 0;
}

static inline phys_addr_t
arm64_pte_get_block_address(arm64_pte_t pte)
{
	return arm64_pte_is_block(pte) ? (pte & ARM64_PTE_ADDR_MASK) : 0;
}

static inline phys_addr_t
arm64_pte_get_page_address(arm64_pte_t pte)
{
	return arm64_pte_is_page(pte) ? (pte & ARM64_PTE_ADDR_MASK) : 0;
}

// Memory attribute functions
static inline uint32
arm64_pte_get_mair_index(arm64_pte_t pte)
{
	return (pte & ARM64_PTE_ATTRINDX_MASK) >> ARM64_PTE_ATTRINDX_SHIFT;
}

static inline arm64_pte_t
arm64_pte_set_mair_index(arm64_pte_t pte, uint32 mair_idx)
{
	return (pte & ~ARM64_PTE_ATTRINDX_MASK) | 
	       (((uint64)mair_idx << ARM64_PTE_ATTRINDX_SHIFT) & ARM64_PTE_ATTRINDX_MASK);
}

static inline uint32
arm64_pte_get_shareability(arm64_pte_t pte)
{
	return (pte & ARM64_PTE_SH_MASK) >> ARM64_PTE_SH_SHIFT;
}

static inline arm64_pte_t
arm64_pte_set_shareability(arm64_pte_t pte, uint32 shareability)
{
	return (pte & ~ARM64_PTE_SH_MASK) | 
	       (((uint64)shareability << ARM64_PTE_SH_SHIFT) & ARM64_PTE_SH_MASK);
}

// Access permission functions
static inline uint32
arm64_pte_get_access_permissions(arm64_pte_t pte)
{
	return (pte & ARM64_PTE_AP_MASK) >> ARM64_PTE_AP_SHIFT;
}

static inline arm64_pte_t
arm64_pte_set_access_permissions(arm64_pte_t pte, uint32 permissions)
{
	return (pte & ~ARM64_PTE_AP_MASK) | 
	       (((uint64)permissions << ARM64_PTE_AP_SHIFT) & ARM64_PTE_AP_MASK);
}

static inline bool
arm64_pte_is_writable(arm64_pte_t pte)
{
	uint32 ap = arm64_pte_get_access_permissions(pte);
	return (ap == 0) || (ap == 1);  // RW at EL1 or RW at EL1/EL0
}

static inline bool
arm64_pte_is_user_accessible(arm64_pte_t pte)
{
	uint32 ap = arm64_pte_get_access_permissions(pte);
	return (ap == 1) || (ap == 3);  // RW at EL1/EL0 or RO at EL1/EL0
}

static inline bool
arm64_pte_is_executable_user(arm64_pte_t pte)
{
	return !(pte & ARM64_PTE_UXN);
}

static inline bool
arm64_pte_is_executable_kernel(arm64_pte_t pte)
{
	return !(pte & ARM64_PTE_PXN);
}

// Execute permission functions
static inline arm64_pte_t
arm64_pte_set_user_execute_never(arm64_pte_t pte, bool never)
{
	return never ? (pte | ARM64_PTE_UXN) : (pte & ~ARM64_PTE_UXN);
}

static inline arm64_pte_t
arm64_pte_set_privileged_execute_never(arm64_pte_t pte, bool never)
{
	return never ? (pte | ARM64_PTE_PXN) : (pte & ~ARM64_PTE_PXN);
}

// Access and dirty bit functions
static inline bool
arm64_pte_is_accessed(arm64_pte_t pte)
{
	return (pte & ARM64_PTE_AF) != 0;
}

static inline arm64_pte_t
arm64_pte_set_accessed(arm64_pte_t pte, bool accessed)
{
	return accessed ? (pte | ARM64_PTE_AF) : (pte & ~ARM64_PTE_AF);
}

static inline bool
arm64_pte_is_dirty(arm64_pte_t pte)
{
	return (pte & ARM64_PTE_DBM) != 0;
}

static inline arm64_pte_t
arm64_pte_set_dirty(arm64_pte_t pte, bool dirty)
{
	return dirty ? (pte | ARM64_PTE_DBM) : (pte & ~ARM64_PTE_DBM);
}

// Global/non-global functions
static inline bool
arm64_pte_is_global(arm64_pte_t pte)
{
	return !(pte & ARM64_PTE_NG);
}

static inline arm64_pte_t
arm64_pte_set_global(arm64_pte_t pte, bool global)
{
	return global ? (pte & ~ARM64_PTE_NG) : (pte | ARM64_PTE_NG);
}

// Software bits functions
static inline uint32
arm64_pte_get_software_bits(arm64_pte_t pte)
{
	return (pte & ARM64_PTE_SOFTWARE_MASK) >> 55;
}

static inline arm64_pte_t
arm64_pte_set_software_bits(arm64_pte_t pte, uint32 software_bits)
{
	return (pte & ~ARM64_PTE_SOFTWARE_MASK) | 
	       (((uint64)(software_bits & 0xF) << 55) & ARM64_PTE_SOFTWARE_MASK);
}

// High-level helper functions for Haiku VM integration

// Convert Haiku protection flags to ARM64 PTE attributes
static inline arm64_pte_t
arm64_make_pte_from_haiku_flags(phys_addr_t addr, uint32 haiku_protection, uint32 haiku_memory_type, bool is_kernel)
{
	arm64_pte_t pte = (addr & ARM64_PTE_ADDR_MASK) | ARM64_DESC_TYPE_PAGE | ARM64_PTE_VALID;
	
	// Set memory attribute index based on Haiku memory type
	uint32 mair_idx;
	switch (haiku_memory_type) {
		case 0x01:  // B_UNCACHED_MEMORY
			mair_idx = ARM64_MAIR_IDX_DEVICE_nGnRnE;
			break;
		case 0x02:  // B_WRITE_COMBINING_MEMORY  
			mair_idx = ARM64_MAIR_IDX_NORMAL_NC;
			break;
		case 0x03:  // B_WRITE_THROUGH_MEMORY
			mair_idx = ARM64_MAIR_IDX_NORMAL_WT;
			break;
		default:    // B_WRITE_BACK_MEMORY and others
			mair_idx = ARM64_MAIR_IDX_NORMAL_WB;
			break;
	}
	pte |= ((uint64)mair_idx << ARM64_PTE_ATTRINDX_SHIFT) & ARM64_PTE_ATTRINDX_MASK;
	
	// Set access permissions
	uint32 ap;
	if (is_kernel) {
		if (haiku_protection & 0x02) {  // B_WRITE_AREA
			ap = (haiku_protection & 0x01) ? 1 : 0;  // RW_ALL if user read, RW_EL1 otherwise
		} else {
			ap = (haiku_protection & 0x01) ? 3 : 2;  // RO_ALL if user read, RO_EL1 otherwise
		}
	} else {
		ap = (haiku_protection & 0x02) ? 1 : 3;  // User area: RW_ALL or RO_ALL
	}
	pte |= ((uint64)ap << ARM64_PTE_AP_SHIFT) & ARM64_PTE_AP_MASK;
	
	// Set execute permissions
	if (!(haiku_protection & 0x04)) {  // !B_EXECUTE_AREA
		pte |= ARM64_PTE_UXN;
	}
	if (!(haiku_protection & 0x20)) {  // !B_KERNEL_EXECUTE_AREA
		pte |= ARM64_PTE_PXN;
	}
	
	// Set shareability (inner shareable for normal memory, non-shareable for device)
	if (mair_idx <= ARM64_MAIR_IDX_DEVICE_GRE) {
		pte |= ARM64_PTE_SH_NON;
	} else {
		pte |= ARM64_PTE_SH_INNER;
	}
	
	// Set access flag and global bit for kernel pages
	pte |= ARM64_PTE_AF;
	if (is_kernel) {
		pte &= ~ARM64_PTE_NG;  // Global for kernel
	} else {
		pte |= ARM64_PTE_NG;   // Non-global for user
	}
	
	return pte;
}

// Extract Haiku flags from ARM64 PTE
static inline void
arm64_extract_haiku_flags_from_pte(arm64_pte_t pte, uint32* protection, uint32* memory_type)
{
	if (protection) {
		*protection = 0;
		
		// Extract access permissions
		uint32 ap = arm64_pte_get_access_permissions(pte);
		switch (ap) {
			case 0:  // RW at EL1, no access at EL0
				*protection |= 0x18;  // B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA
				break;
			case 1:  // RW at EL1/EL0
				*protection |= 0x1B;  // B_READ_AREA | B_WRITE_AREA | B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA
				break;
			case 2:  // RO at EL1, no access at EL0
				*protection |= 0x08;  // B_KERNEL_READ_AREA
				break;
			case 3:  // RO at EL1/EL0
				*protection |= 0x09;  // B_READ_AREA | B_KERNEL_READ_AREA
				break;
		}
		
		// Extract execute permissions
		if (!arm64_pte_is_executable_user(pte)) {
			// UXN is set, user cannot execute
		} else {
			*protection |= 0x04;  // B_EXECUTE_AREA
		}
		
		if (!arm64_pte_is_executable_kernel(pte)) {
			// PXN is set, kernel cannot execute
		} else {
			*protection |= 0x20;  // B_KERNEL_EXECUTE_AREA
		}
	}
	
	if (memory_type) {
		uint32 mair_idx = arm64_pte_get_mair_index(pte);
		switch (mair_idx) {
			case ARM64_MAIR_IDX_DEVICE_nGnRnE:
			case ARM64_MAIR_IDX_DEVICE_nGnRE:
			case ARM64_MAIR_IDX_DEVICE_GRE:
				*memory_type = 0x01;  // B_UNCACHED_MEMORY
				break;
			case ARM64_MAIR_IDX_NORMAL_NC:
				*memory_type = 0x02;  // B_WRITE_COMBINING_MEMORY
				break;
			case ARM64_MAIR_IDX_NORMAL_WT:
				*memory_type = 0x03;  // B_WRITE_THROUGH_MEMORY
				break;
			case ARM64_MAIR_IDX_NORMAL_WB:
			default:
				*memory_type = 0x04;  // B_WRITE_BACK_MEMORY
				break;
		}
	}
}

// Page table level calculation helpers
static inline int
arm64_calculate_start_level(int va_bits, int page_bits)
{
	int level = 4;
	int table_bits = page_bits - 3;  // 3 bits for 8-byte entries
	int bits_left = va_bits - page_bits;
	
	while (bits_left > 0) {
		bits_left -= table_bits;
		level--;
	}
	
	return level;
}

static inline uint64
arm64_va_to_table_index(uint64 va, int level, int page_bits)
{
	int table_bits = page_bits - 3;
	int shift = page_bits + (3 - level) * table_bits;
	return (va >> shift) & ((1UL << table_bits) - 1);
}

static inline bool
arm64_is_valid_block_size(int level, int page_bits)
{
	// Block entries are only valid at levels 1 and 2
	// Level 0 is always table entries, Level 3 is always page entries
	return (level == 1) || (level == 2);
}

// 48-bit virtual address space helper functions

// Extract level indices from 48-bit virtual address
static inline uint64
arm64_va_to_level0_index(uint64 va)
{
	return (va >> ARM64_VA_LEVEL0_SHIFT) & ARM64_VA_LEVEL0_MASK;
}

static inline uint64
arm64_va_to_level1_index(uint64 va)
{
	return (va >> ARM64_VA_LEVEL1_SHIFT) & ARM64_VA_LEVEL1_MASK;
}

static inline uint64
arm64_va_to_level2_index(uint64 va)
{
	return (va >> ARM64_VA_LEVEL2_SHIFT) & ARM64_VA_LEVEL2_MASK;
}

static inline uint64
arm64_va_to_level3_index(uint64 va)
{
	return (va >> ARM64_VA_LEVEL3_SHIFT) & ARM64_VA_LEVEL3_MASK;
}

static inline uint64
arm64_va_to_page_offset(uint64 va)
{
	return va & ARM64_VA_PAGE_MASK;
}

// Build virtual address from level indices
static inline uint64
arm64_build_va_from_indices(uint64 l0_idx, uint64 l1_idx, uint64 l2_idx, uint64 l3_idx, uint64 offset)
{
	return ((l0_idx & ARM64_VA_LEVEL0_MASK) << ARM64_VA_LEVEL0_SHIFT) |
	       ((l1_idx & ARM64_VA_LEVEL1_MASK) << ARM64_VA_LEVEL1_SHIFT) |
	       ((l2_idx & ARM64_VA_LEVEL2_MASK) << ARM64_VA_LEVEL2_SHIFT) |
	       ((l3_idx & ARM64_VA_LEVEL3_MASK) << ARM64_VA_LEVEL3_SHIFT) |
	       (offset & ARM64_VA_PAGE_MASK);
}

// Address space validation functions
static inline bool
arm64_is_canonical_address(uint64 va)
{
	// 48-bit canonical addresses: either all upper 16 bits are 0 or all are 1
	return ARM64_IS_USER_ADDRESS(va) || ARM64_IS_KERNEL_ADDRESS(va);
}

static inline bool
arm64_va_uses_ttbr0(uint64 va)
{
	return ARM64_IS_USER_ADDRESS(va);
}

static inline bool
arm64_va_uses_ttbr1(uint64 va)
{
	return ARM64_IS_KERNEL_ADDRESS(va);
}

// Region identification functions
static inline const char*
arm64_get_va_region_name(uint64 va)
{
	if (ARM64_IS_PHYSMAP_ADDRESS(va))
		return "Physical Memory Map";
	else if (ARM64_IS_KERNEL_HEAP_ADDRESS(va))
		return "Kernel Heap";
	else if ((va >= ARM64_KERNEL_MODULES_BASE) && (va <= ARM64_KERNEL_MODULES_TOP))
		return "Kernel Modules";
	else if ((va >= ARM64_KERNEL_TEXT_BASE) && (va <= ARM64_KERNEL_TEXT_TOP))
		return "Kernel Text";
	else if (ARM64_IS_DEVICE_ADDRESS(va))
		return "Device/MMIO";
	else if ((va >= ARM64_KERNEL_RESERVED_BASE) && (va <= ARM64_KERNEL_RESERVED_TOP))
		return "Reserved";
	else if (ARM64_IS_USER_ADDRESS(va))
		return "User Space";
	else if (ARM64_IS_KERNEL_ADDRESS(va))
		return "Kernel Space";
	else
		return "Invalid";
}

// Page table allocation size calculations for 48-bit addressing
static inline size_t
arm64_calculate_page_table_size(int level, size_t va_range)
{
	size_t coverage;
	switch (level) {
		case 0: coverage = ARM64_L0_COVERAGE; break;
		case 1: coverage = ARM64_L1_COVERAGE; break;
		case 2: coverage = ARM64_L2_COVERAGE; break;
		case 3: coverage = ARM64_L3_COVERAGE; break;
		default: return 0;
	}
	
	size_t entries = (va_range + coverage - 1) / coverage;
	return entries * sizeof(arm64_pte_t);
}

// TTBR register configuration for 48-bit addressing
static inline uint64
arm64_make_ttbr0_48bit(phys_addr_t pt_phys, uint16 asid)
{
	return ARM64_TTBR_SET_ASID(ARM64_TTBR_SET_BADDR(0, pt_phys), asid);
}

static inline uint64
arm64_make_ttbr1_48bit(phys_addr_t pt_phys)
{
	// TTBR1 doesn't use ASID - only TTBR0 does
	return ARM64_TTBR_SET_BADDR(0, pt_phys);
}

// Address alignment functions for different block sizes
static inline uint64
arm64_align_to_l1_block(uint64 addr)
{
	return addr & ~(ARM64_L1_BLOCK_SIZE - 1);
}

static inline uint64
arm64_align_to_l2_block(uint64 addr)
{
	return addr & ~(ARM64_L2_BLOCK_SIZE - 1);
}

static inline uint64
arm64_align_to_page(uint64 addr)
{
	return addr & ~(ARM64_L3_PAGE_SIZE - 1);
}

// Check if address is aligned to block/page boundaries
static inline bool
arm64_is_l1_block_aligned(uint64 addr)
{
	return (addr & (ARM64_L1_BLOCK_SIZE - 1)) == 0;
}

static inline bool
arm64_is_l2_block_aligned(uint64 addr)
{
	return (addr & (ARM64_L2_BLOCK_SIZE - 1)) == 0;
}

static inline bool
arm64_is_page_aligned(uint64 addr)
{
	return (addr & (ARM64_L3_PAGE_SIZE - 1)) == 0;
}

// Calculate how many entries are needed at each level for a VA range
static inline size_t
arm64_calculate_l0_entries_needed(uint64 va_start, size_t size)
{
	uint64 va_end = va_start + size - 1;
	uint64 start_l0_idx = arm64_va_to_level0_index(va_start);
	uint64 end_l0_idx = arm64_va_to_level0_index(va_end);
	return (end_l0_idx - start_l0_idx + 1);
}

static inline size_t
arm64_calculate_l1_entries_needed(uint64 va_start, size_t size)
{
	uint64 va_end = va_start + size - 1;
	uint64 start_l1_idx = arm64_va_to_level1_index(va_start);
	uint64 end_l1_idx = arm64_va_to_level1_index(va_end);
	return (end_l1_idx - start_l1_idx + 1);
}

static inline size_t
arm64_calculate_l2_entries_needed(uint64 va_start, size_t size)
{
	uint64 va_end = va_start + size - 1;
	uint64 start_l2_idx = arm64_va_to_level2_index(va_start);
	uint64 end_l2_idx = arm64_va_to_level2_index(va_end);
	return (end_l2_idx - start_l2_idx + 1);
}

static inline size_t
arm64_calculate_l3_entries_needed(uint64 va_start, size_t size)
{
	uint64 va_end = va_start + size - 1;
	uint64 start_l3_idx = arm64_va_to_level3_index(va_start);
	uint64 end_l3_idx = arm64_va_to_level3_index(va_end);
	return (end_l3_idx - start_l3_idx + 1);
}

// Physical to virtual address conversion helpers
static inline bool
arm64_phys_addr_in_physmap_range(phys_addr_t pa)
{
	// Check if physical address can be mapped in physmap region
	return (pa < ARM64_PHYSMAP_SIZE);
}

static inline addr_t
arm64_phys_to_kernel_va(phys_addr_t pa)
{
	// Convert physical address to kernel virtual address via physmap
	if (arm64_phys_addr_in_physmap_range(pa))
		return ARM64_PHYS_TO_PHYSMAP(pa);
	return 0; // Invalid conversion
}

static inline phys_addr_t
arm64_kernel_va_to_phys(addr_t va)
{
	// Convert kernel virtual address back to physical address
	if (ARM64_IS_PHYSMAP_ADDRESS(va))
		return ARM64_PHYSMAP_TO_PHYS(va);
	return 0; // Invalid conversion - not a physmap address
}

// TCR_EL1 helper functions for different configurations
static inline uint64
arm64_make_tcr_el1_48bit_4k(void)
{
	return ARM64_TCR_EL1_48BIT_4K_VALUE;
}

// Check if a virtual address range crosses page table boundaries
static inline bool
arm64_va_range_crosses_l0_boundary(uint64 va_start, size_t size)
{
	uint64 va_end = va_start + size - 1;
	return arm64_va_to_level0_index(va_start) != arm64_va_to_level0_index(va_end);
}

static inline bool
arm64_va_range_crosses_l1_boundary(uint64 va_start, size_t size)
{
	uint64 va_end = va_start + size - 1;
	return arm64_va_to_level1_index(va_start) != arm64_va_to_level1_index(va_end);
}

static inline bool
arm64_va_range_crosses_l2_boundary(uint64 va_start, size_t size)
{
	uint64 va_end = va_start + size - 1;
	return arm64_va_to_level2_index(va_start) != arm64_va_to_level2_index(va_end);
}

#ifdef __cplusplus
}
#endif

#endif /* _SYSTEM_KERNEL_ARCH_ARM64_ARCH_VM_TYPES_H */