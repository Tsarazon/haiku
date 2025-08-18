# ARM64 Exception Levels Analysis and Implementation Requirements

## Overview

This document provides a comprehensive analysis of ARM64 exception levels (EL0-EL3), their usage in kernel implementations, EL1 kernel operation requirements, and transition procedures from higher exception levels, based on research and Haiku's ARM64 implementation.

## ARM64 Exception Level Hierarchy

### Exception Level Definitions

ARM64 architecture defines four exception levels with increasing privilege:

```
EL3 (Secure Monitor)     ← Highest privilege, secure mode only
 ↑
EL2 (Hypervisor)         ← Virtualization level, non-secure only  
 ↑
EL1 (Kernel/OS)          ← Operating system kernel level
 ↑
EL0 (User Applications)  ← Unprivileged user space
```

### Privilege and Access Rights

| Exception Level | Privilege | Purpose | Register Access |
|----------------|-----------|---------|----------------|
| **EL0** | Unprivileged | User applications | Limited system registers |
| **EL1** | Privileged | OS kernel | Most system registers |
| **EL2** | Hypervisor | Virtualization | EL1 + hypervisor registers |
| **EL3** | Secure Monitor | Security/Firmware | All registers |

**Key Principle:** Higher exception levels have access to all registers of lower levels, but not vice versa.

## Exception Level Implementation Requirements

### Mandatory vs Optional Support

**Mandatory:**
- EL0 and EL1 must be implemented in all ARM64 processors
- Basic kernel operation requires only EL0 and EL1

**Optional:**
- EL2 (Hypervisor) is optional but recommended for virtualization
- EL3 (Secure Monitor) is optional but common in production systems

**Implementation Variants:**
- Minimal: EL0, EL1 only
- Virtualization: EL0, EL1, EL2
- Full security: EL0, EL1, EL2, EL3

### AArch64 Execution State Requirements

**Critical Constraint:** For an exception level to use AArch64, the immediate higher exception level must also use AArch64.

```
EL3 → AArch64 only (if implemented)
EL2 → AArch64 required if EL3 uses AArch64
EL1 → AArch64 required if EL2 uses AArch64
EL0 → AArch64 possible if EL1 uses AArch64
```

## EL1 Kernel Operation Requirements

### Boot Entry Requirements

**From Linux Kernel Documentation:**
- CPUs must enter kernel at **EL2 (recommended)** or **non-secure EL1**
- All CPUs must enter at the same exception level
- **MMU must be disabled** at entry
- **Interrupts must be masked** (PSTATE.DAIF set)
- **Instruction cache** may be on or off

### Register Initialization Requirements

**All writable architected system registers** at the entry exception level must be initialized by software at a higher exception level to prevent UNKNOWN state execution.

### Key System Registers for EL1

#### Memory Management (MMU)
```cpp
// Translation Table Base Registers
TTBR0_EL1    // User space page tables (0x0000_0000_0000_0000 - 0x0000_FFFF_FFFF_FFFF)
TTBR1_EL1    // Kernel space page tables (0xFFFF_0000_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF)

// Translation Control Register
TCR_EL1      // Controls translation table configuration

// Memory Attribute Indirection Register
MAIR_EL1     // Defines memory attributes for page table entries

// System Control Register
SCTLR_EL1    // Controls MMU enable, cache enable, alignment checking, etc.
```

#### Exception Handling
```cpp
VBAR_EL1     // Vector Base Address Register - exception vector table
ESR_EL1      // Exception Syndrome Register - exception cause information  
FAR_EL1      // Fault Address Register - faulting virtual address
ELR_EL1      // Exception Link Register - return address
SPSR_EL1     // Saved Program Status Register - saved processor state
```

#### Performance and Debug
```cpp
PMCR_EL0     // Performance Monitors Control Register
MDSCR_EL1    // Monitor Debug System Control Register
```

## Exception Level Transitions

### Transition Mechanisms

**Upward Transitions (Lower → Higher EL):**
- Only possible through **exceptions** (interrupts, system calls, faults)
- Hardware automatically switches to higher EL when exception occurs

**Downward Transitions (Higher → Lower EL):**
- Only possible through **exception return** instruction (`ERET`)
- Software explicitly returns to lower EL

### Transition Rules

```
Exception occurs:    EL0 → EL1 (system call, fault)
                    EL1 → EL2 (hypervisor call)
                    ELx → EL3 (secure monitor call)

Exception return:   ELx → ELx (same level)
                   ELx → EL(x-1) (lower level)
                   ELx → EL(x-2) (skip level)
```

**Never possible:** Direct transition to higher EL without exception

## EL2 to EL1 Transition Implementation

### Haiku's Transition Procedure

Based on `/home/ruslan/haiku/src/system/boot/platform/efi/arch/aarch64/transition.S`:

```asm
FUNCTION(_arch_transition_EL2_EL1):
    // 1. Migrate critical registers from EL2 to EL1
    mreg TTBR0_EL2, TTBR0_EL1, x10     // Translation table base
    mreg MAIR_EL2, MAIR_EL1, x10       // Memory attributes  
    mreg vbar_el2, vbar_el1, x10       // Vector base address
    
    // 2. Migrate stack pointer
    mov x10, sp
    msr sp_el1, x10
    
    // 3. Enable floating-point/SIMD at EL1
    mov x10, #3 << 20                  // FPEN bits
    msr cpacr_el1, x10
    
    b drop_to_el1
FUNCTION_END(_arch_transition_EL2_EL1)

FUNCTION(drop_to_el1):
    // 4. Verify we're at EL2
    mrs x1, CurrentEL
    lsr x1, x1, #2
    cmp x1, #0x2
    b.eq 1f
    ret                                 // Already at lower EL
    
1:  // 5. Configure hypervisor for EL1 execution
    mov x2, #(HCR_RW)                  // EL1 uses AArch64
    msr hcr_el2, x2
    
    // 6. Set up virtualization ID registers
    mrs x2, midr_el1
    msr vpidr_el2, x2                  // Virtual processor ID
    mrs x2, mpidr_el1  
    msr vmpidr_el2, x2                 // Virtual multiprocessor ID
    
    // 7. Initialize EL1 system control
    ldr x2, .Lsctlr_res1              // Required bits for SCTLR_EL1
    msr sctlr_el1, x2
    
    // 8. Configure trapping behavior
    mov x2, #CPTR_RES1                // Don't trap to EL2 for exceptions
    msr cptr_el2, x2
    msr hstr_el2, xzr                 // Don't trap CP15
    
    // 9. Enable timer access at EL1
    mrs x2, cnthctl_el2
    orr x2, x2, #(CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN)
    msr cnthctl_el2, x2
    msr cntvoff_el2, xzr              // Clear virtual timer offset
    
    // 10. Configure GICv3 if available
    mrs x2, id_aa64pfr0_el1
    ubfx x2, x2, #ID_AA64PFR0_GIC_SHIFT, #ID_AA64PFR0_GIC_BITS
    cmp x2, #(ID_AA64PFR0_GIC_CPUIF_EN >> ID_AA64PFR0_GIC_SHIFT)
    b.ne 2f
    
    mrs x2, S3_4_C12_C9_5             // ICC_SRE_EL2
    orr x2, x2, #ICC_SRE_EL2_EN       // Enable EL1 access
    orr x2, x2, #ICC_SRE_EL2_SRE      // Enable system registers
    msr S3_4_C12_C9_5, x2
    
2:  // 11. Set up EL1 processor state for return
    mov x2, #(PSR_F | PSR_I | PSR_A | PSR_D | PSR_M_EL1h)
    msr spsr_el2, x2                  // EL1h mode, exceptions masked
    
    // 12. Set return address and execute transition
    msr elr_el2, x30                  // Return to caller
    isb                               // Ensure all changes visible
    eret                              // Return to EL1
FUNCTION_END(drop_to_el1)
```

### Critical Transition Steps

1. **Register Migration:** Copy essential registers from EL2 to EL1
2. **Hypervisor Configuration:** Set up HCR_EL2 to allow EL1 execution
3. **Trap Configuration:** Disable unnecessary traps to EL2
4. **Timer Access:** Enable physical timer access at EL1
5. **Interrupt Controller:** Configure GIC for EL1 operation
6. **State Setup:** Configure target processor state (SPSR_EL2)
7. **Return:** Use ERET to transition to EL1

## Memory Management Between Exception Levels

### Virtual Address Space Layout

**EL1 (Kernel) - Dual Address Space:**
```
0x0000_0000_0000_0000 - 0x0000_FFFF_FFFF_FFFF  : User space (TTBR0_EL1)
0xFFFF_0000_0000_0000 - 0xFFFF_FFFF_FFFF_FFFF  : Kernel space (TTBR1_EL1)
```

**EL2/EL3 - Single Address Space:**
```
0x0000_0000_0000_0000 - 0x0000_FFFF_FFFF_FFFF  : Single space (TTBR0_ELx)
```

**Note:** EL2 and EL3 only have TTBR0, no TTBR1, limiting virtual address range.

### Haiku's Exception Level Detection

```cpp
static inline uint64 arch_exception_level()
{
    return (READ_SPECIALREG(CurrentEL) >> 2);
}

static inline uint64 arch_mmu_base_register(bool kernel = false)
{
    switch (arch_exception_level()) {
        case 1:
            return kernel ? READ_SPECIALREG(TTBR1_EL1) : READ_SPECIALREG(TTBR0_EL1);
        case 2:
            return READ_SPECIALREG(TTBR0_EL2);  // No TTBR1_EL2
        case 3:
            return READ_SPECIALREG(TTBR0_EL3);  // No TTBR1_EL3
        default:
            return 0;
    }
}
```

## Security and Virtualization Implications

### Secure vs Non-Secure States

**EL3 (Secure Monitor):**
- Only exception level that can switch security states
- Manages transition between secure and non-secure worlds
- Typically runs ARM Trusted Firmware

**EL2 (Hypervisor):**
- Exists only in non-secure state
- Provides virtualization services to multiple OS instances
- Can trap and emulate privileged operations from EL1

### Kernel Security Considerations

**EL1 Kernel Protections:**
- Cannot access EL2/EL3 registers directly
- Memory access controlled by MMU configuration from higher ELs
- Exception handling can be intercepted by EL2/EL3

**Privilege Escalation Prevention:**
- Higher ELs control what EL1 can access
- Hypervisor can provide security isolation
- Secure monitor provides hardware security boundary

## Exception Level Detection and Adaptation

### Runtime Detection

```cpp
void arch_start_kernel(addr_t kernelEntry)
{
    uint64 el = arch_exception_level();
    dprintf("Current Exception Level EL%1lx\n", el);
    
    switch (el) {
        case 1:
            // Already at EL1, proceed normally
            arch_mmu_setup_EL1(READ_SPECIALREG(TCR_EL1));
            break;
        case 2:
            // Drop from EL2 to EL1
            arch_mmu_setup_EL1(READ_SPECIALREG(TCR_EL2));
            arch_cache_disable();
            _arch_transition_EL2_EL1();
            break;
        default:
            panic("Unexpected Exception Level\n");
            break;
    }
    
    arch_cache_enable();
    arch_enter_kernel(&gKernelArgs, kernelEntry, stackTop);
}
```

### Conditional Register Access

```cpp
static inline uint64 _arch_mmu_get_tcr(int el = kInvalidExceptionLevel) {
    if (el == kInvalidExceptionLevel)
        el = arch_exception_level();
        
    switch (el) {
        case 1: return READ_SPECIALREG(TCR_EL1);
        case 2: return READ_SPECIALREG(TCR_EL2);
        case 3: return READ_SPECIALREG(TCR_EL3);
        default: return 0;
    }
}
```

## Boot Sequence Recommendations

### Ideal Boot Sequence

1. **Firmware/EL3:** Initialize secure services, set up EL2
2. **EL2 Entry:** Boot loader enters at EL2 with virtualization extensions
3. **EL2 Setup:** Configure hypervisor registers, prepare for EL1 transition
4. **EL1 Transition:** Drop to EL1 using controlled ERET
5. **EL1 Kernel:** Initialize OS kernel services
6. **EL0 User:** Launch user applications

### Fallback Support

```cpp
// Support both EL2 and EL1 entry
switch (arch_exception_level()) {
    case 2:
        // Preferred: Configure and drop to EL1
        setup_hypervisor_environment();
        transition_to_el1();
        break;
    case 1:
        // Acceptable: Direct EL1 entry
        setup_kernel_environment();
        break;
    default:
        panic("Unsupported exception level");
}
```

## Debugging and Diagnostics

### Exception Level Information

```cpp
void print_exception_level_info()
{
    uint64 el = arch_exception_level();
    dprintf("Current Exception Level: EL%lu\n", el);
    
    switch (el) {
        case 0:
            dprintf("  Running in user space\n");
            break;
        case 1:
            dprintf("  Running in kernel space\n");
            dprintf("  TTBR0_EL1: 0x%016lx (user)\n", READ_SPECIALREG(TTBR0_EL1));
            dprintf("  TTBR1_EL1: 0x%016lx (kernel)\n", READ_SPECIALREG(TTBR1_EL1));
            dprintf("  SCTLR_EL1: 0x%016lx\n", READ_SPECIALREG(SCTLR_EL1));
            dprintf("  TCR_EL1: 0x%016lx\n", READ_SPECIALREG(TCR_EL1));
            break;
        case 2:
            dprintf("  Running in hypervisor\n");
            dprintf("  TTBR0_EL2: 0x%016lx\n", READ_SPECIALREG(TTBR0_EL2));
            dprintf("  HCR_EL2: 0x%016lx\n", READ_SPECIALREG(HCR_EL2));
            break;
        case 3:
            dprintf("  Running in secure monitor\n");
            break;
    }
}
```

## Conclusion

ARM64 exception levels provide a hierarchical privilege model essential for modern kernel implementations:

**Key Requirements for EL1 Kernel:**
- Proper initialization of all system registers
- Support for both EL2 and EL1 entry scenarios  
- Correct MMU configuration with dual address spaces
- Exception handling setup for system calls and interrupts

**Critical Implementation Points:**
- EL2→EL1 transition requires careful register migration
- Hypervisor configuration must enable EL1 execution
- Security boundaries must be respected
- Runtime detection enables adaptive behavior

**Haiku's Approach:**
- Runtime exception level detection
- Graceful handling of EL2→EL1 transitions
- Comprehensive system register management
- Robust fallback for different entry scenarios

This analysis provides the foundation for implementing ARM64 kernel support with proper exception level handling and security boundaries.