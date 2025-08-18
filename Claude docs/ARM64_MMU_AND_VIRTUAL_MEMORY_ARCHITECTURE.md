# ARM64 Memory Management Unit (MMU) Architecture and Haiku Virtual Memory Implementation

## Table of Contents

1. [ARM64 MMU Architecture Overview](#arm64-mmu-architecture-overview)
2. [VMSAv8-64 Translation System](#vmsav8-64-translation-system)
3. [Translation Table Formats](#translation-table-formats)
4. [Page Sizes and Granules](#page-sizes-and-granules)
5. [Memory Attributes](#memory-attributes)
6. [Haiku Virtual Memory Architecture](#haiku-virtual-memory-architecture)
7. [Haiku VM Implementation Analysis](#haiku-vm-implementation-analysis)
8. [ARM64 Integration Requirements](#arm64-integration-requirements)
9. [Implementation Recommendations](#implementation-recommendations)

---

## ARM64 MMU Architecture Overview

### Virtual Memory System Architecture v8 (VMSAv8-64)

ARM64 processors implement the VMSAv8-64 virtual memory architecture, which provides:

- **Two-stage translation capability** (Stage 1 VA→IPA, Stage 2 IPA→PA)
- **48-bit virtual address space** (up to 256TB per translation regime)
- **52-bit physical address space** support (up to 4PB)
- **Flexible page sizes** (4KB, 16KB, 64KB granules)
- **Multi-level page tables** (up to 4 levels)
- **Hardware-assisted features** (Access Flag, Dirty Bit Management)

### Key System Registers

#### Translation Table Base Registers
```
TTBR0_EL1  - Translation Table Base Register 0 (User space)
TTBR1_EL1  - Translation Table Base Register 1 (Kernel space)
```

#### Control Registers
```
TCR_EL1    - Translation Control Register
SCTLR_EL1  - System Control Register
MAIR_EL1   - Memory Attribute Indirection Register
```

#### Address Space Layout
```
0x0000000000000000 - 0x0000FFFFFFFFFFFF: User space (TTBR0_EL1)
0xFFFF000000000000 - 0xFFFFFFFFFFFFFFFF: Kernel space (TTBR1_EL1)
```

---

## VMSAv8-64 Translation System

### Address Translation Process

1. **Virtual Address Input**: 48-bit maximum VA
2. **Translation Table Walk**: Hardware performs multi-level lookup
3. **Physical Address Output**: Final PA with attributes
4. **TLB Caching**: Hardware caches translations

### Translation Stages

#### Stage 1 Translation (VA → IPA/PA)
- Controlled by operating system
- Uses TTBR0_EL1 (user) and TTBR1_EL1 (kernel)
- Implements memory protection and attributes
- Handles page fault exceptions

#### Stage 2 Translation (IPA → PA)
- Used for virtualization
- Controlled by hypervisor
- Optional for non-virtualized systems

### Address Space Management

#### Dual Translation Base Architecture
```
TTBR0_EL1: User space translation tables
- VA range: 0x0000000000000000 to (2^(64-T0SZ))-1
- Used for application virtual memory

TTBR1_EL1: Kernel space translation tables  
- VA range: 2^(64-T1SZ) to 0xFFFFFFFFFFFFFFFF
- Used for kernel virtual memory
```

#### Translation Control (TCR_EL1)
- **T0SZ/T1SZ**: Controls VA space size (16-39 bits unused)
- **TG0/TG1**: Translation granule size selection
- **SH0/SH1**: Shareability configuration
- **ORGN0/ORGN1**: Outer cacheability
- **IRGN0/IRGN1**: Inner cacheability

---

## Translation Table Formats

### Page Table Entry Structure (64-bit)

#### Level 0, 1, 2 Descriptors
```
Bits [63:52] - Upper attributes
Bits [51:48] - Reserved/Software use
Bits [47:12] - Output address
Bits [11:2]  - Lower attributes
Bits [1:0]   - Descriptor type
```

#### Level 3 (Page) Descriptors
```
Bits [63:52] - Upper attributes
Bits [51:48] - Reserved/Software use
Bits [47:12] - Page address (4KB aligned)
Bits [11:2]  - Lower attributes  
Bits [1:0]   - Must be 11b (valid page)
```

### Descriptor Types

#### Table Descriptor (Levels 0-2)
```
Bits [1:0] = 11b (valid table)
Bits [47:12] - Next level table address
```

#### Block Descriptor (Levels 1-2)
```
Bits [1:0] = 01b (valid block)
Bits [47:N] - Block address (N depends on level and granule)
```

#### Page Descriptor (Level 3)
```
Bits [1:0] = 11b (valid page)
Bits [47:12] - Page address
```

### Attribute Fields

#### Upper Attributes (Bits 63:52)
- **Bit 54**: UXN (User Execute Never)
- **Bit 53**: PXN (Privileged Execute Never) 
- **Bit 51**: DBM (Dirty Bit Modifier - ARMv8.1+)
- **Bit 55**: Software use

#### Lower Attributes (Bits 11:2)
- **Bit 10**: AF (Access Flag)
- **Bit 11**: nG (not Global)
- **Bits 9:8**: SH (Shareability)
- **Bits 7:6**: AP (Access Permissions)
- **Bit 5**: NS (Non-secure - EL3 only)
- **Bits 4:2**: AttrIndx (Memory Attribute Index)

---

## Page Sizes and Granules

### Supported Granule Sizes

#### 4KB Granule
- **Page size**: 4KB (4,096 bytes)
- **Translation levels**: 0-3 (4 levels)
- **VA bits supported**: 39-48 bits
- **Entries per table**: 512 (9 bits per level)

#### 16KB Granule  
- **Page size**: 16KB (16,384 bytes)
- **Translation levels**: 0-3 (4 levels)
- **VA bits supported**: 36-47 bits
- **Entries per table**: 2048 (11 bits per level)

#### 64KB Granule
- **Page size**: 64KB (65,536 bytes) 
- **Translation levels**: 1-3 (3 levels)
- **VA bits supported**: 42-48 bits
- **Entries per table**: 8192 (13 bits per level)

### Level Calculation

For a given VA width and granule size:
```c
int CalculateStartLevel(int vaBits, int pageBits) {
    int level = 4;
    int tableBits = pageBits - 3;  // 3 bits for 8-byte entries
    int bitsLeft = vaBits - pageBits;
    
    while (bitsLeft > 0) {
        bitsLeft -= tableBits;
        level--;
    }
    
    return level;
}
```

---

## Memory Attributes

### Memory Attribute Indirection Register (MAIR_EL1)

MAIR_EL1 contains 8 memory attribute encodings (Attr0-Attr7):

#### Device Memory Types
```
0x00: Device-nGnRnE (non-Gathering, non-Reordering, no Early write acknowledgment)
0x04: Device-nGnRE  (non-Gathering, non-Reordering, Early write acknowledgment)
0x08: Device-GRE    (Gathering, Reordering, Early write acknowledgment)
0x0C: Device-GRE    (Alternative encoding)
```

#### Normal Memory Types
```
0x44: Normal memory, Outer/Inner Non-cacheable
0xBB: Normal memory, Outer/Inner Write-through, Read/Write-allocate
0xFF: Normal memory, Outer/Inner Write-back, Read/Write-allocate
```

### Memory Attribute Fields

#### AttrIndx (Bits 4:2)
Points to one of 8 MAIR_EL1 entries

#### Shareability (SH - Bits 9:8)
- **00**: Non-shareable
- **01**: Reserved
- **10**: Outer Shareable  
- **11**: Inner Shareable

#### Access Permissions (AP - Bits 7:6)
- **00**: Read/Write at EL1, No access at EL0
- **01**: Read/Write at EL1/EL0
- **10**: Read-only at EL1, No access at EL0
- **11**: Read-only at EL1/EL0

---

## Haiku Virtual Memory Architecture

### Core VM Components

Based on analysis of `/src/system/kernel/vm/` and related headers:

#### VMTranslationMap (Virtual Base Class)
```cpp
struct VMTranslationMap {
    // Core translation operations
    virtual status_t Map(addr_t va, phys_addr_t pa, uint32 attrs, uint32 memType,
                        vm_page_reservation* reservation) = 0;
    virtual status_t Unmap(addr_t start, addr_t end) = 0;
    virtual status_t Query(addr_t va, phys_addr_t* pa, uint32* flags) = 0;
    virtual status_t Protect(addr_t base, addr_t top, uint32 attrs, uint32 memType) = 0;
    
    // Page-level operations
    virtual status_t UnmapPage(VMArea* area, addr_t address, bool updatePageQueue,
                              bool deletingAddressSpace = false, uint32* flags = NULL) = 0;
    virtual void UnmapPages(VMArea* area, addr_t base, size_t size, 
                           bool updatePageQueue, bool deletingAddressSpace = false);
    
    // Access tracking
    virtual status_t ClearFlags(addr_t va, uint32 flags) = 0;
    virtual bool ClearAccessedAndModified(VMArea* area, addr_t address,
                                         bool unmapIfUnaccessed, bool& modified) = 0;
    
    // Cache management
    virtual void Flush() = 0;
    
protected:
    recursive_lock fLock;
    int32 fMapCount;
};
```

#### VMPhysicalPageMapper
```cpp
struct VMPhysicalPageMapper {
    // Physical page access
    virtual status_t GetPage(phys_addr_t pa, addr_t* va, void** handle) = 0;
    virtual status_t PutPage(addr_t va, void* handle) = 0;
    
    // CPU-specific access
    virtual status_t GetPageCurrentCPU(phys_addr_t pa, addr_t* va, void** handle) = 0;
    virtual status_t PutPageCurrentCPU(addr_t va, void** handle) = 0;
    
    // Memory operations
    virtual status_t MemsetPhysical(phys_addr_t addr, int value, phys_size_t length) = 0;
    virtual status_t MemcpyFromPhysical(void* to, phys_addr_t from, size_t length, bool user) = 0;
    virtual status_t MemcpyToPhysical(phys_addr_t to, const void* from, size_t length, bool user) = 0;
    virtual void MemcpyPhysicalPage(phys_addr_t to, phys_addr_t from) = 0;
};
```

#### VMAddressSpace
```cpp
class VMAddressSpace {
private:
    addr_t fBase;                    // Address space base
    addr_t fEndAddress;              // Address space end  
    size_t fFreeSpace;               // Available space
    team_id fID;                     // Address space ID
    int32 fRefCount;                 // Reference count
    int32 fFaultCount;               // Page fault count
    int32 fChangeCount;              // Change counter
    VMTranslationMap* fTranslationMap; // Architecture translation map
    bool fRandomizingEnabled;        // Address randomization
    bool fDeleting;                  // Deletion in progress
    
public:
    VMTranslationMap* TranslationMap() { return fTranslationMap; }
    static VMAddressSpace* Kernel();
    team_id ID() const { return fID; }
};
```

#### VMArea (Virtual Memory Area)
```cpp
class VMArea {
public:
    char name[B_OS_NAME_LENGTH];     // Area name
    area_id id;                      // Unique area ID
    addr_t base;                     // Base virtual address
    size_t size;                     // Area size
    uint32 protection;               // Memory protection flags
    uint32 protection_max;           // Maximum allowed protection
    uint32 wiring;                   // Wiring type (B_NO_LOCK, etc.)
    uint32 memory_type;              // Memory type (caching attributes)
    
    VMCache* cache;                  // Backing cache
    vint32 cache_offset;             // Offset in cache
    uint32 cache_type;               // Cache type
    
    uint32* page_protections;        // Per-page protection bits
    VMAddressSpace* address_space;   // Containing address space
    VMAreaMappings mappings;         // Page mappings
    
    // Memory type constants
    static const uint32 B_UNCACHED_MEMORY = 0x01;
    static const uint32 B_WRITE_COMBINING_MEMORY = 0x02;
    static const uint32 B_WRITE_THROUGH_MEMORY = 0x03;
    static const uint32 B_WRITE_BACK_MEMORY = 0x04;
    static const uint32 B_WRITE_PROTECTED_MEMORY = 0x05;
};
```

### Architecture Interface

From `/headers/private/kernel/arch/vm_translation_map.h`:

```cpp
// Architecture-specific functions
status_t arch_vm_translation_map_create_map(bool kernel, VMTranslationMap** map);
status_t arch_vm_translation_map_init(kernel_args* args, VMPhysicalPageMapper** mapper);
status_t arch_vm_translation_map_init_post_area(kernel_args* args);
status_t arch_vm_translation_map_init_post_sem(kernel_args* args);

// Early mapping for boot
status_t arch_vm_translation_map_early_map(kernel_args* args, addr_t va, 
                                          phys_addr_t pa, uint8 attributes);

// Access validation
bool arch_vm_translation_map_is_kernel_page_accessible(addr_t va, uint32 protection);
```

---

## Haiku VM Implementation Analysis

### Key Design Patterns

#### 1. Virtual Interface Pattern
Haiku uses abstract base classes (`VMTranslationMap`, `VMPhysicalPageMapper`) with architecture-specific implementations.

#### 2. Reference Counting
Address spaces and areas use reference counting for lifecycle management.

#### 3. Hierarchical Locking
- Address space locks protect area modifications
- Translation map locks protect page table operations
- Recursive locks allow reentrant access

#### 4. Cache Integration
Areas are backed by `VMCache` objects that handle:
- Anonymous memory (swap-backed)
- File-backed memory (memory-mapped files)
- Device memory (frame buffers, etc.)

### Memory Protection Flags

```cpp
// Protection flags (from vm/vm_types.h)
#define B_READ_AREA           0x01
#define B_WRITE_AREA          0x02  
#define B_EXECUTE_AREA        0x04
#define B_KERNEL_READ_AREA    0x08
#define B_KERNEL_WRITE_AREA   0x10
#define B_KERNEL_EXECUTE_AREA 0x20

// Page flags
#define PAGE_PRESENT          0x01
#define PAGE_ACCESSED         0x02
#define PAGE_MODIFIED         0x04
```

### x86 Implementation Reference

From `/src/system/kernel/arch/x86/arch_vm.cpp`:

#### Memory Type Range Management
The x86 implementation shows how Haiku manages memory types using:
- MTRR (Memory Type Range Registers) for x86
- Memory type ranges with area integration
- Dynamic memory type updates

#### Key Functions
```cpp
status_t arch_vm_init(kernel_args* args);
status_t arch_vm_init2(kernel_args* args); 
status_t arch_vm_init_post_area(kernel_args* args);
status_t arch_vm_init_end(kernel_args* args);
status_t arch_vm_init_post_modules(kernel_args* args);

void arch_vm_aspace_swap(VMAddressSpace* from, VMAddressSpace* to);
bool arch_vm_supports_protection(uint32 protection);
status_t arch_vm_set_memory_type(VMArea* area, phys_addr_t physicalBase, 
                                uint32 type, uint32* effectiveType);
```

---

## ARM64 Integration Requirements

### 1. VMSAv8TranslationMap Implementation

Must inherit from `VMTranslationMap` and implement:

```cpp
class VMSAv8TranslationMap : public VMTranslationMap {
private:
    bool fIsKernel;                    // Kernel vs user map
    phys_addr_t fPageTable;           // Root page table PA
    int fPageBits;                    // Page size (12/14/16)
    int fVaBits;                      // VA width (39-48)
    int fInitialLevel;                // Starting table level
    int fASID;                        // Address Space ID
    
    // Hardware features
    static uint32_t fHwFeature;       // HW capabilities
    static uint64_t fMair;            // MAIR_EL1 value
    
public:
    // Required VMTranslationMap interface
    virtual bool Lock();
    virtual void Unlock();
    virtual addr_t MappedSize() const;
    virtual size_t MaxPagesNeededToMap(addr_t start, addr_t end) const;
    
    virtual status_t Map(addr_t va, phys_addr_t pa, uint32 attributes, 
                        uint32 memoryType, vm_page_reservation* reservation);
    virtual status_t Unmap(addr_t start, addr_t end);
    virtual status_t UnmapPage(VMArea* area, addr_t address, bool updatePageQueue,
                              bool deletingAddressSpace, uint32* flags);
    
    virtual status_t Query(addr_t va, phys_addr_t* pa, uint32* flags);
    virtual status_t QueryInterrupt(addr_t va, phys_addr_t* pa, uint32* flags);
    virtual status_t Protect(addr_t base, addr_t top, uint32 attributes, uint32 memoryType);
    virtual status_t ClearFlags(addr_t va, uint32 flags);
    virtual bool ClearAccessedAndModified(VMArea* area, addr_t address,
                                         bool unmapIfUnaccessed, bool& modified);
    virtual void Flush();
    
    // ARM64-specific functions
    static void SwitchUserMap(VMSAv8TranslationMap* from, VMSAv8TranslationMap* to);
    static int CalcStartLevel(int vaBits, int pageBits);
    static uint64_t GetMemoryAttr(uint32 attributes, uint32 memoryType, bool isKernel);
};
```

### 2. Physical Page Mapper

ARM64 typically uses direct mapping for kernel physical memory access:

```cpp
class PMAPPhysicalPageMapper : public VMPhysicalPageMapper {
    // Direct mapping: VA = PA + KERNEL_PMAP_BASE
    virtual status_t GetPage(phys_addr_t pa, addr_t* va, void** handle) {
        *va = KERNEL_PMAP_BASE + pa;
        *handle = NULL;
        return B_OK;
    }
};
```

### 3. Architecture Functions

```cpp
// In arch_vm_translation_map.cpp
status_t arch_vm_translation_map_create_map(bool kernel, VMTranslationMap** map) {
    phys_addr_t pt = kernel ? (READ_SPECIALREG(TTBR1_EL1) & kTtbrBasePhysAddrMask) : 0;
    *map = new VMSAv8TranslationMap(kernel, pt, 12, 48, 1);
    return B_OK;
}

status_t arch_vm_translation_map_init(kernel_args* args, VMPhysicalPageMapper** mapper) {
    // Initialize MAIR, detect hardware features, set up translation
    *mapper = new PMAPPhysicalPageMapper();
    return B_OK;
}
```

### 4. Memory Attribute Mapping

Map Haiku memory types to ARM64 MAIR attributes:

```cpp
uint64_t GetMemoryAttr(uint32 attributes, uint32 memoryType, bool isKernel) {
    uint64_t attr = 0;
    
    // Set access permissions
    if (attributes & B_KERNEL_WRITE_AREA) attr |= kAttrSWDBM;
    if (attributes & B_READ_AREA) attr |= kAttrAPUserAccess;
    if (!(attributes & B_EXECUTE_AREA)) attr |= kAttrUXN;
    if (!(attributes & B_KERNEL_EXECUTE_AREA)) attr |= kAttrPXN;
    
    // Set memory type
    uint8_t mair_idx;
    switch (memoryType & B_MEMORY_TYPE_MASK) {
        case B_UNCACHED_MEMORY: mair_idx = MairIndex(MAIR_DEVICE_nGnRnE); break;
        case B_WRITE_COMBINING_MEMORY: mair_idx = MairIndex(MAIR_NORMAL_NC); break;
        case B_WRITE_THROUGH_MEMORY: mair_idx = MairIndex(MAIR_NORMAL_WT); break;
        case B_WRITE_BACK_MEMORY: 
        default: mair_idx = MairIndex(MAIR_NORMAL_WB); break;
    }
    
    attr |= mair_idx << 2;
    attr |= kAttrSHInnerShareable;  // Inner shareable
    attr |= kAttrAF;                // Access flag
    
    return attr;
}
```

### 5. ASID Management

ARM64 uses 8-16 bit ASIDs for TLB efficiency:

```cpp
// ASID allocation for user address spaces
static constexpr size_t kAsidBits = 8;
static constexpr size_t kNumAsids = (1 << kAsidBits);
static uint64 sAsidBitMap[kNumAsids / 64];
static VMSAv8TranslationMap* sAsidMapping[kNumAsids];

void SwitchUserMap(VMSAv8TranslationMap* from, VMSAv8TranslationMap* to) {
    if (!to->fIsKernel && to->fASID != -1) {
        uint64_t ttbr = to->fPageTable | ((uint64_t)to->fASID << 48);
        WRITE_SPECIALREG(TTBR0_EL1, ttbr);
        asm("isb");
    }
}
```

---

## Implementation Recommendations

### 1. Phased Implementation

#### Phase 1: Basic Translation Map
- Implement core `VMSAv8TranslationMap` class
- Support 4KB pages with 39-bit VA
- Basic map/unmap/query operations

#### Phase 2: Hardware Features
- Access Flag and Dirty Bit Management
- Break-before-make compliance
- TLB and cache maintenance

#### Phase 3: Advanced Features  
- Multiple page sizes (16KB, 64KB)
- 48-bit VA support
- ASID management
- Performance optimizations

### 2. Testing Strategy

#### Unit Tests
- Page table level calculations
- Address validation
- Memory attribute mapping
- PTE manipulation

#### Integration Tests
- Map/unmap operations
- Protection changes
- Access/dirty bit tracking
- Multi-process scenarios

#### Stress Tests
- Large mappings
- Fragment/coalesce patterns
- Concurrent access
- Memory pressure scenarios

### 3. Performance Considerations

#### Hardware Optimization
- Use hardware AF/DBM when available
- Implement break-before-make efficiently
- Optimize TLB invalidation patterns

#### Software Optimization
- Batch page table updates
- Minimize TLB flushes
- Use direct physical mapping

#### Memory Efficiency
- Share page tables when possible
- Implement lazy allocation
- Optimize for common access patterns

### 4. Compatibility

#### Cross-Architecture
- Maintain compatibility with existing VM interfaces
- Support architecture-specific features gracefully
- Provide fallback implementations

#### Forward Compatibility
- Design for future ARM64 extensions
- Support emerging security features
- Plan for large page sizes

---

This document provides a comprehensive foundation for implementing ARM64 MMU support in Haiku's virtual memory system. The architecture is well-designed to accommodate ARM64's VMSAv8-64 translation system while maintaining compatibility with Haiku's existing VM abstractions.