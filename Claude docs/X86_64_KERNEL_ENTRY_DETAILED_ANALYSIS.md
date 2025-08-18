# X86_64 Kernel Entry Point Analysis

## Overview

This document provides a detailed analysis of the x86_64 kernel entry point implementation in Haiku, including register state expectations, stack setup, and initial kernel setup procedures.

## Kernel Entry Point Architecture

### Key Files
- `src/system/boot/platform/efi/arch/x86_64/entry.S` - Assembly entry point
- `src/system/boot/platform/efi/arch/x86_64/arch_start.cpp` - Boot loader handoff
- `src/system/kernel/main.cpp` - Kernel main function (`_start`)

### Entry Point Flow
```
EFI Boot Loader (arch_start_kernel)
    ↓
arch_enter_kernel (entry.S) - Assembly handoff
    ↓
kernel _start (main.cpp) - Kernel initialization
```

## Register State Expectations at Kernel Entry

### Assembly Entry Function (`arch_enter_kernel`)
**Location:** `src/system/boot/platform/efi/arch/x86_64/entry.S:20`

**Input Parameters:**
- `%rdi` - PML4 page table address (uint64_t)
- `%rsi` - Kernel entry point address (uint64_t)  
- `%rdx` - Stack top address (uint64_t)

**Register Setup Sequence:**
```asm
FUNCTION(arch_enter_kernel):
    // 1. Set up page tables
    movq    %rdi, %cr3              // Load PML4 into CR3
    
    // 2. Load 64-bit GDT
    lgdtq   gLongGDTR(%rip)        
    
    // 3. Jump to 64-bit code segment
    push    $KERNEL_CODE_SELECTOR   
    lea     .Llmode(%rip), %rax     
    push    %rax
    lretq                           // Far return to change CS
    
    // 4. Set up data segments
    mov     $KERNEL_DATA_SELECTOR, %ax
    mov     %ax, %ss                // Stack segment
    xor     %ax, %ax                
    mov     %ax, %ds                // Data segment = 0
    mov     %ax, %es                // Extra segment = 0
    mov     %ax, %fs                // FS segment = 0
    mov     %ax, %gs                // GS segment = 0
    
    // 5. Set up stack
    movq    %rdx, %rsp              // Set stack pointer
    xorq    %rbp, %rbp              // Clear frame pointer
    push    $2                      // Set RFLAGS = 2
    popf
    
    // 6. Prepare kernel arguments and call
    mov     %rsi, %rax              // entry point -> RAX
    leaq    gKernelArgs(%rip), %rdi // kernel_args -> RDI
    xorl    %esi, %esi              // currentCPU = 0 -> RSI
    call    *%rax                   // Call kernel entry
```

### Kernel Main Function (`_start`) Register State
**Location:** `src/system/kernel/main.cpp:98`

**Input Register State:**
| Register | Value | Description |
|----------|-------|-------------|
| **RDI** | `kernel_args*` | Pointer to kernel arguments structure |
| **RSI** | `0` | Current CPU number (boot CPU) |
| **RAX** | *undefined* | Scratch register |
| **RBX-R15** | *undefined* | General purpose registers |
| **RSP** | Stack Top | Kernel stack pointer |
| **RBP** | `0` | Frame pointer (cleared) |

**Control Registers:**
| Register | Value | Description |
|----------|-------|-------------|
| **CR3** | PML4 Address | Page table base register |
| **CS** | `KERNEL_CODE_SELECTOR` | Code segment (64-bit) |
| **SS** | `KERNEL_DATA_SELECTOR` | Stack segment |
| **DS,ES,FS,GS** | `0` | Data segments (flat model) |
| **RFLAGS** | `2` | Processor flags (minimal set) |

## Stack Setup Analysis

### Stack Configuration
```
High Address
+------------------+ <- RSP (gKernelArgs.cpu_kstack[0].start + cpu_kstack[0].size)
|   (empty stack)  |
|                  | Stack grows down ↓
|                  |
|                  |
+------------------+ <- Stack Base (gKernelArgs.cpu_kstack[0].start)
Low Address
```

### Stack Properties
- **Size:** Determined by boot loader (typically 16KB-64KB)
- **Alignment:** 16-byte aligned for x86_64 ABI compliance
- **Growth Direction:** Downward (toward lower addresses)
- **Initial State:** Empty, RSP points to top
- **Frame Pointer:** RBP cleared to 0 (no initial frame)

### Stack Setup Code Analysis
```asm
movq    %rdx, %rsp              // Set stack pointer to top
xorq    %rbp, %rbp              // Clear frame pointer
push    $2                      // Push RFLAGS value
popf                            // Set RFLAGS register
```

## Initial Kernel Setup Procedures

### Phase 1: Pre-Kernel Boot Loader Setup
**Function:** `arch_start_kernel()` in `arch_start.cpp`

1. **Memory Map Acquisition**
   ```cpp
   kBootServices->GetMemoryMap(&memory_map_size, &dummy, &map_key, 
                               &descriptor_size, &descriptor_version)
   ```

2. **Page Table Generation**
   ```cpp
   uint64_t final_pml4 = arch_mmu_generate_post_efi_page_tables(
       memory_map_size, memory_map, descriptor_size, descriptor_version);
   ```

3. **EFI Exit**
   ```cpp
   kBootServices->ExitBootServices(kImage, map_key)
   ```

4. **SMP Bootstrap**
   ```cpp
   smp_boot_other_cpus(final_pml4, kernelEntry, (addr_t)&gKernelArgs);
   ```

### Phase 2: Assembly Transition
**Function:** `arch_enter_kernel()` in `entry.S`

1. **MMU Setup:** Load page tables into CR3
2. **GDT Setup:** Load 64-bit Global Descriptor Table
3. **Segment Setup:** Configure code/data segments for flat model
4. **Stack Setup:** Initialize kernel stack
5. **Parameter Setup:** Prepare arguments for kernel main

### Phase 3: Kernel Initialization
**Function:** `_start()` in `main.cpp`

#### Initialization Sequence:
```cpp
// 1. Version validation
if (bootKernelArgs->version != CURRENT_KERNEL_ARGS_VERSION)
    return -1;

// 2. SMP synchronization
smp_cpu_rendezvous(&sCpuRendezvous);

// 3. Kernel args copying (boot CPU only)
if (currentCPU == 0)
    memcpy(&sKernelArgs, bootKernelArgs, bootKernelArgs->kernel_args_size);

// 4. Platform initialization
if (currentCPU == 0) {
    arch_platform_init(&sKernelArgs);
    debug_init(&sKernelArgs);
    // ... other subsystems
}
```

#### Subsystem Initialization Order:
```
arch_platform_init() → debug_init() → cpu_init() → interrupts_init() → 
vm_init() → module_init() → smp_init() → timer_init() → scheduler_init() → 
thread_init() → ...
```

## Memory Management at Entry

### Page Table State
- **CR3:** Points to PML4 (Page Map Level 4) structure
- **Mapping:** Identity mapping for low memory + kernel high mapping
- **MMU:** Enabled with full 64-bit virtual addressing
- **Caching:** Write-back caching enabled for normal memory

### Memory Layout
```
Virtual Address Space:
0x0000000000000000 - 0x00007FFFFFFFFFFF: User space (128TB)
0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF: Kernel space (128TB)
```

## Exception Level and Privilege

### x86_64 Protection Rings
- **Current Ring:** Ring 0 (kernel mode)
- **Segments:** Flat memory model with minimal segmentation
- **Privileges:** Full supervisor privileges
- **SMEP/SMAP:** May be enabled depending on CPU features

## Key Differences from Other Architectures

### Unique x86_64 Features
1. **Segmentation Legacy:** Still uses GDT but in flat mode
2. **Page Table Format:** 4-level page tables (PML4 → PDPT → PD → PT)
3. **System Calls:** Uses SYSCALL/SYSRET instructions
4. **Context Switching:** FXSAVE/FXRSTOR for FPU state

### Architecture-Specific Registers
- **CR0-CR4:** Control registers for CPU features
- **MSRs:** Model-Specific Registers for advanced features
- **Segment Registers:** CS, DS, ES, FS, GS, SS
- **Debug Registers:** DR0-DR7 for hardware debugging

## Critical Considerations for ARM64 Adaptation

### Register Mapping
| x86_64 | ARM64 | Purpose |
|--------|-------|---------|
| RDI | X0 | First argument (kernel_args*) |
| RSI | X1 | Second argument (currentCPU) |
| RSP | SP | Stack pointer |
| RBP | X29 | Frame pointer |
| RIP | PC | Program counter |

### Architecture Differences
1. **No Segmentation:** ARM64 uses pure virtual memory
2. **Exception Levels:** EL0-EL3 instead of protection rings
3. **MMU Registers:** TTBR0/TTBR1_EL1 instead of CR3
4. **Stack Alignment:** 16-byte alignment (same as x86_64)

### Recommended ARM64 Implementation
```asm
// ARM64 equivalent of arch_enter_kernel
FUNCTION(arch_enter_kernel):
    // X0 = kernel_args*, X1 = kernelEntry, X2 = stackTop
    mov     sp, x2                  // Set stack pointer
    mov     x4, x1                  // Save entry point
    // X0 already contains kernel_args
    mov     x1, #0                  // currentCPU = 0
    br      x4                      // Branch to kernel
FUNCTION_END(arch_enter_kernel)
```

## Conclusion

The x86_64 kernel entry point follows a well-defined sequence:

1. **Boot loader** prepares memory, page tables, and kernel arguments
2. **Assembly transition** sets up CPU state and registers
3. **Kernel main** initializes all subsystems

This architecture provides a clean separation between boot-time setup and kernel initialization, making it adaptable to other architectures like ARM64 with appropriate modifications for architecture-specific features.