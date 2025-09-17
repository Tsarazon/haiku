# X86_64 UEFI Entry Point Implementation Documentation

This document provides a comprehensive analysis of how the UEFI entry point is implemented for x86_64 architecture in Haiku OS, including function signatures, parameter handling, and initial setup procedures.

## Table of Contents

1. [Overview](#overview)
2. [File Structure and Entry Flow](#file-structure-and-entry-flow)
3. [Function Signatures and Parameter Handling](#function-signatures-and-parameter-handling)
4. [Initial Setup Procedures](#initial-setup-procedures)
5. [Key Architecture Features](#key-architecture-features)
6. [Source File Locations](#source-file-locations)

## Overview

The Haiku x86_64 UEFI boot loader implements a sophisticated multi-phase entry point system that transitions from UEFI firmware environment to the Haiku kernel. The implementation handles ELF relocation, ABI transitions, memory model changes, and multi-processor initialization.

## File Structure and Entry Flow

The UEFI entry point follows this execution chain:

```
UEFI Firmware
    ↓
1. crt0-efi-x86_64.S     - Assembly bootstrap and relocation
    ↓
2. relocation_func.cpp   - ELF relocation handling  
    ↓
3. start.cpp::efi_main() - Main EFI application entry
    ↓
4. arch_start.cpp::arch_start_kernel() - Architecture-specific kernel launch
    ↓
5. entry.S::arch_enter_kernel() - Final kernel transition
    ↓
Haiku Kernel
```

## Function Signatures and Parameter Handling

### 1. Initial Assembly Entry Point

**File:** `src/system/boot/platform/efi/arch/x86_64/crt0-efi-x86_64.S:41`

```assembly
_start:
    subq $8, %rsp           # Align stack to 16 bytes
    pushq %rcx              # Save EFI image handle 
    pushq %rdx              # Save EFI system table
```

**Parameters received from UEFI firmware:**
- **`%rcx`** - `efi_handle image` (firmware-allocated image handle)
- **`%rdx`** - `efi_system_table *systemTable` (EFI system table pointer)

**Purpose:** Initial bootstrap code that receives control from UEFI firmware using Microsoft ABI calling convention.

### 2. ELF Relocation Function

**File:** `src/system/boot/platform/efi/arch/x86_64/relocation_func.cpp:43-45`

```cpp
extern "C" efi_status _relocate(
    long ldbase,                    // Load base address
    Elf64_Dyn *dyn,                // Dynamic section
    efi_handle image,              // EFI image handle (unused)
    efi_system_table *systab       // EFI system table (unused)
)
```

**Parameter Details:**
- **`ldbase`** - Base address where image was loaded by UEFI
- **`dyn`** - Pointer to ELF dynamic section for relocation processing
- **`image`** - EFI image handle (parameter unused in current implementation)
- **`systab`** - EFI system table (parameter unused in current implementation)

**Return Value:** `efi_status` (EFI_SUCCESS or EFI_LOAD_ERROR)

**Purpose:** Processes ELF relocations to adjust addresses for the actual load address.

### 3. Main EFI Application Entry

**File:** `src/system/boot/platform/efi/start.cpp:232`

```cpp
extern "C" efi_status efi_main(
    efi_handle image,              // Firmware image handle
    efi_system_table *systemTable  // EFI system table
)
```

**Parameter Types:**
- **`efi_handle`** - `typedef void* efi_handle` (opaque firmware handle)
- **`efi_system_table`** - Complete EFI system interface structure containing:
  - `efi_boot_services *BootServices` - Boot-time services
  - `efi_runtime_services *RuntimeServices` - Runtime services
  - Console I/O interfaces
  - Configuration tables

**Return Value:** `efi_status` - `typedef size_t efi_status` (success/error code)

**Purpose:** Main EFI application entry point that initializes Haiku boot environment.

### 4. Architecture-Specific Kernel Launch

**File:** `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp:87`

```cpp
void arch_start_kernel(addr_t kernelEntry)
```

**Parameters:**
- **`kernelEntry`** - Virtual address of kernel entry point (`addr_t` = `uint64_t`)

**Purpose:** Prepares the system for kernel execution, including memory map processing and EFI exit.

### 5. Final Kernel Entry

**File:** `src/system/boot/platform/efi/arch/x86_64/entry.S:20`

```assembly
FUNCTION(arch_enter_kernel):
    # Parameters in System V ABI:
    # %rdi = pml4 (page table root)
    # %rsi = entry_point (kernel entry address)  
    # %rdx = stackTop (kernel stack pointer)
```

**Parameters:**
- **`pml4`** - Physical address of kernel page table root (PML4)
- **`entry_point`** - Virtual address of kernel entry function
- **`stackTop`** - Top of kernel stack for initial execution

**Purpose:** Final transition from boot loader to kernel with proper memory management setup.

## Initial Setup Procedures

### Phase 1: Assembly Bootstrap (crt0-efi-x86_64.S)

```assembly
1. Stack Alignment
   subq $8, %rsp              # Ensure 16-byte stack alignment

2. Parameter Preservation  
   pushq %rcx                 # Save EFI image handle
   pushq %rdx                 # Save EFI system table

3. Address Calculation
   lea ImageBase(%rip), %rdi  # Calculate load base address
   lea _DYNAMIC(%rip), %rsi   # Locate ELF dynamic section

4. Relocation Processing
   call _relocate             # Apply ELF relocations

5. Parameter Restoration and Main Call
   popq %rdi                  # Restore system table
   popq %rsi                  # Restore image handle  
   call efi_main              # Enter main application
```

**Key Features:**
- **Position-independent addressing** using RIP-relative instructions
- **Microsoft ABI compatibility** for UEFI interface
- **Stack alignment maintenance** for x86_64 ABI requirements

### Phase 2: ELF Relocation Processing (_relocate)

```cpp
1. Dynamic Section Parsing
   for (i = 0; dyn[i].d_tag != DT_NULL; ++i) {
       switch (dyn[i].d_tag) {
           case DT_RELA:    // Relocation table address
           case DT_RELASZ:  // Relocation table size  
           case DT_RELAENT: // Relocation entry size
       }
   }

2. Address Relocation Processing
   while (relsz > 0) {
       switch (ELF64_R_TYPE(rel->r_info)) {
           case R_X86_64_RELATIVE:
               addr = (unsigned long *)(ldbase + rel->r_offset);
               *addr += ldbase;  // Apply base address adjustment
               break;
       }
       rel = (Elf64_Rel*)((char*)rel + relent);
       relsz -= relent;
   }
```

**Purpose:** Adjusts all position-dependent addresses in the loaded image to account for the actual load address assigned by UEFI firmware.

### Phase 3: EFI Main Application Setup (efi_main)

```cpp
1. Global EFI Service Initialization
   kImage = image;                              // Store image handle
   kSystemTable = systemTable;                  // Store system table
   kBootServices = systemTable->BootServices;   // Cache boot services
   kRuntimeServices = systemTable->RuntimeServices; // Cache runtime services

2. C++ Runtime Initialization
   call_ctors();  // Execute global constructors for C++ objects

3. Platform Subsystem Initialization (in order)
   console_init();     // Initialize text output console
   serial_init();      // Set up debug serial port communication
   serial_enable();    // Enable serial debugging output
   sBootOptions = console_check_boot_keys(); // Check for boot options

4. Hardware and Firmware Interface Setup
   cpu_init();         // CPU identification and feature detection
   acpi_init();        // Parse ACPI tables from firmware
   timer_init();       // Initialize system timer services
   smp_init();         // Multi-processor initialization

5. Boot Process Continuation
   main(&args);        // Transfer control to stage2 boot loader
```

### Phase 4: Kernel Launch Preparation (arch_start_kernel)

```cpp
1. Memory Map Acquisition and Analysis
   // First call to determine buffer size
   kBootServices->GetMemoryMap(&memory_map_size, &dummy, &map_key,
                               &descriptor_size, &descriptor_version);
   
   // Allocate buffer (2x size for potential growth)
   actual_memory_map_size = memory_map_size * 2;
   memory_map = (efi_memory_descriptor*)kernel_args_malloc(actual_memory_map_size);
   
   // Get actual memory map
   kBootServices->GetMemoryMap(&memory_map_size, memory_map, &map_key,
                               &descriptor_size, &descriptor_version);

2. Memory Map Analysis and Logging
   for (size_t i = 0; i < memory_map_size / descriptor_size; i++) {
       efi_memory_descriptor *entry = (efi_memory_descriptor*)(addr + i * descriptor_size);
       // Log: physical range, virtual range, type, attributes
   }

3. Kernel Page Table Generation
   uint64_t final_pml4 = arch_mmu_generate_post_efi_page_tables(
       memory_map_size, memory_map, descriptor_size, descriptor_version);

4. EFI Boot Services Exit (Critical Section)
   while (true) {
       if (kBootServices->ExitBootServices(kImage, map_key) == EFI_SUCCESS) {
           serial_kernel_handoff();  // Disconnect from EFI serial services
           break;
       }
       // Memory map may have changed, retry with updated map
       kBootServices->GetMemoryMap(&memory_map_size, memory_map, &map_key,
                                   &descriptor_size, &descriptor_version);
   }

5. Post-EFI Environment Setup
   arch_mmu_post_efi_setup(memory_map_size, memory_map,
                           descriptor_size, descriptor_version);
   serial_init();          // Re-initialize serial in post-EFI environment
   serial_enable();        // Re-enable serial debugging
   
   smp_boot_other_cpus(final_pml4, kernelEntry, (addr_t)&gKernelArgs);

6. Final Kernel Entry
   arch_enter_kernel(final_pml4, kernelEntry,
                     gKernelArgs.cpu_kstack[0].start + gKernelArgs.cpu_kstack[0].size);
```

### Phase 5: Final Kernel Transition (arch_enter_kernel)

```assembly
1. Memory Management Activation
   movq %rdi, %cr3              # Load kernel page table (PML4)

2. Global Descriptor Table Setup  
   lgdtq gLongGDTR(%rip)        # Load 64-bit GDT

3. Code Segment Transition
   push $KERNEL_CODE_SELECTOR   # Push target code segment
   lea .Llmode(%rip), %rax      # Get target address
   push %rax                    # Push target address
   lretq                        # Far return to enter kernel code segment

4. Data Segment Configuration
   mov $KERNEL_DATA_SELECTOR, %ax  # Kernel data segment
   mov %ax, %ss                    # Stack segment
   xor %ax, %ax                    # Null selector
   mov %ax, %ds                    # Data segment (null)
   mov %ax, %es                    # Extra segment (null)  
   mov %ax, %fs                    # FS segment (null)
   mov %ax, %gs                    # GS segment (null)

5. Stack and Execution Environment Setup
   movq %rdx, %rsp              # Set kernel stack pointer
   xorq %rbp, %rbp              # Clear frame pointer
   push $2                      # Push RFLAGS value
   popf                         # Set RFLAGS to known state

6. Kernel Entry Call
   mov %rsi, %rax               # Load kernel entry point
   leaq gKernelArgs(%rip), %rdi # First argument: kernel args
   xorl %esi, %esi              # Second argument: current CPU (0)
   call *%rax                   # Call kernel entry point
```

## Key Architecture Features

### Microsoft ABI Compatibility

**EFIAPI Attribute Definition:**
```cpp
// From headers/private/kernel/platform/efi/types.h:11-15
#if defined(__x86_64__) || defined(__x86__)
#define EFIAPI __attribute__((ms_abi))
#else  
#define EFIAPI
#endif
```

**Calling Convention Differences:**
- **Microsoft ABI (UEFI):** Parameters in RCX, RDX, R8, R9
- **System V ABI (Kernel):** Parameters in RDI, RSI, RDX, RCX, R8, R9
- **Stack Alignment:** 16-byte alignment maintained in both conventions

### Position Independent Code Support

**RIP-Relative Addressing:**
```assembly
lea ImageBase(%rip), %rdi    # Load base address relative to instruction pointer
lea _DYNAMIC(%rip), %rsi     # Load dynamic section relative to instruction pointer
```

**Dynamic Relocation Processing:**
- **R_X86_64_RELATIVE relocations:** Adjust pointers by load base offset
- **Load-time address resolution:** All absolute addresses corrected at runtime
- **EFI firmware compatibility:** Works with arbitrary load addresses

### Memory Model Transition

**EFI Identity Mapping Phase:**
- Physical addresses == Virtual addresses
- Direct hardware access possible
- EFI services available

**Kernel Virtual Mapping Phase:**
- High canonical addresses (0xFFFF800000000000+)
- Separate kernel and user address spaces
- Memory protection enabled

**Transition Mechanism:**
```cpp
// Generate new page tables while in EFI mode
uint64_t final_pml4 = arch_mmu_generate_post_efi_page_tables(...);

// Exit EFI services
kBootServices->ExitBootServices(kImage, map_key);

// Activate kernel page tables
movq %rdi, %cr3  // CR3 = final_pml4
```

### Multi-Processor Initialization

**SMP Boot Sequence:**
```cpp
// Boot secondary CPUs before kernel entry
smp_boot_other_cpus(final_pml4, kernelEntry, (addr_t)&gKernelArgs);

// Each CPU gets:
// - Same page table root (final_pml4)  
// - Same kernel entry point
// - Shared kernel arguments structure
// - Individual CPU ID assignment
```

## Source File Locations

### Primary Implementation Files

| File | Purpose |
|------|---------|
| `src/system/boot/platform/efi/arch/x86_64/crt0-efi-x86_64.S` | Assembly entry point and bootstrap |
| `src/system/boot/platform/efi/arch/x86_64/relocation_func.cpp` | ELF relocation processing |
| `src/system/boot/platform/efi/start.cpp` | Main EFI application logic |
| `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp` | x86_64-specific kernel launch |
| `src/system/boot/platform/efi/arch/x86_64/entry.S` | Final kernel transition assembly |

### Supporting Files

| File | Purpose |
|------|---------|
| `src/system/boot/platform/efi/efi_platform.h` | EFI platform definitions |
| `headers/private/kernel/platform/efi/types.h` | EFI type definitions |
| `headers/private/kernel/platform/efi/system-table.h` | EFI system table structure |
| `src/system/boot/platform/efi/arch/x86_64/arch_mmu.cpp` | Memory management setup |
| `src/system/boot/platform/efi/console.cpp` | Console initialization |
| `src/system/boot/platform/efi/serial.cpp` | Serial debug support |

### Build Integration

| File | Purpose |
|------|---------|
| `src/system/boot/platform/efi/arch/x86_64/Jamfile` | Build configuration |
| `build/jam/ArchitectureRules` | Architecture-specific build flags |
| `build/scripts/build_cross_tools_gcc4` | Cross-compilation toolchain |

## Conclusion

The Haiku x86_64 UEFI entry point implementation demonstrates a sophisticated understanding of low-level boot requirements, handling complex transitions between firmware and kernel environments while maintaining compatibility with UEFI specifications and x86_64 architecture requirements.

The multi-phase approach ensures robust initialization while providing extensive debugging capabilities and maintaining clean separation between boot-time and runtime environments.

---

*This documentation was generated as part of the Haiku ARM64 architecture support analysis project.*