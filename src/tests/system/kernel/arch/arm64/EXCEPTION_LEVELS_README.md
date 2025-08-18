# ARM64 Exception Level Management

## Overview

The ARM64 exception level management system provides comprehensive support for detecting, configuring, and managing ARM64 exception levels, with particular focus on proper EL1 (kernel level) system register configuration.

## ARM64 Exception Level Hierarchy

ARM64 defines four exception levels with different privilege levels:

| Exception Level | Privilege | Typical Usage |
|----------------|-----------|---------------|
| EL0 | Least privileged | User applications |
| EL1 | Kernel level | Operating system kernel |
| EL2 | Hypervisor level | Virtual machine monitor |
| EL3 | Secure monitor | Secure world/TrustZone |

## Key Features Implemented

### 1. **Exception Level Detection**
- **Current EL Detection**: Read and decode CurrentEL register
- **EL Availability Check**: Verify which exception levels are implemented
- **Feature Detection**: Identify available ARM64 features per exception level
- **System Analysis**: Comprehensive system configuration analysis

### 2. **EL1 System Register Configuration**
- **SCTLR_EL1**: System Control Register with proper RES1 bits and kernel settings
- **MAIR_EL1**: Memory Attribute Indirection Register for various memory types
- **CPACR_EL1**: Coprocessor Access Control for FP/SIMD enablement
- **Security Features**: Pointer authentication and other security feature setup

### 3. **Exception Level Transitions**
- **EL2→EL1 Transition**: Configure EL2 registers for proper EL1 operation
- **HCR_EL2 Setup**: Hypervisor Configuration for AArch64 EL1 execution
- **Timer Access**: Enable EL1 access to generic timers
- **Trap Configuration**: Disable unnecessary traps to higher exception levels

### 4. **Memory Management Integration**
- **MMU Configuration**: Prepare for MMU enablement at EL1
- **Cache Setup**: Configure instruction and data cache policies
- **Memory Attributes**: Define device, normal cacheable, and non-cacheable memory types

## Implementation Details

### Core Functions

#### Exception Level Detection
```c
// Get current exception level (0-3)
uint32_t arch_get_current_exception_level(void);

// Check if specific exception level is available
bool arch_exception_level_available(uint32_t exception_level);

// Detect and initialize exception level information
status_t arch_detect_exception_levels(void);
```

#### EL1 System Register Configuration
```c
// Configure all EL1 system registers for kernel operation
status_t arch_configure_el1_system_registers(void);

// Enable MMU and caches (called after memory management setup)
status_t arch_enable_el1_mmu_caches(void);
```

#### Exception Level Transitions
```c
// Configure EL2 for EL1 operation
status_t arch_transition_el2_to_el1(void);
```

### System Register Configurations

#### SCTLR_EL1 (System Control Register)

The SCTLR_EL1 register is configured with:

```c
// Required reserved bits (RES1)
sctlr_el1 |= (1UL << 11); // RES1
sctlr_el1 |= (1UL << 20); // RES1
sctlr_el1 |= (1UL << 22); // RES1
sctlr_el1 |= (1UL << 23); // RES1
sctlr_el1 |= (1UL << 28); // RES1
sctlr_el1 |= (1UL << 29); // RES1

// Essential control bits
sctlr_el1 |= SCTLR_EL1_SA;      // Stack alignment check enable
sctlr_el1 |= SCTLR_EL1_SA0;     // Stack alignment check for EL0
sctlr_el1 |= SCTLR_EL1_nTWI;    // Don't trap WFI instructions
sctlr_el1 |= SCTLR_EL1_nTWE;    // Don't trap WFE instructions
sctlr_el1 |= SCTLR_EL1_DZE;     // Enable DC ZVA instruction at EL0
sctlr_el1 |= SCTLR_EL1_UCT;     // EL0 access to CTR_EL0
sctlr_el1 |= SCTLR_EL1_UCI;     // EL0 access to cache instructions
```

**Key Features:**
- Stack alignment enforcement for both EL1 and EL0
- WFI/WFE instructions not trapped (power management)
- EL0 access to certain cache and system registers
- MMU/caches disabled initially (enabled later during memory management)

#### MAIR_EL1 (Memory Attribute Indirection Register)

Eight memory attribute configurations:

| Attribute | Value | Description |
|-----------|-------|-------------|
| Attr0 | 0x00 | Device-nGnRnE (strongly ordered) |
| Attr1 | 0x04 | Device-nGnRE (device memory) |
| Attr2 | 0x0C | Device-GRE (device with gather/reorder) |
| Attr3 | 0x44 | Normal memory, non-cacheable |
| Attr4 | 0xAA | Normal memory, write-through cacheable |
| Attr5 | 0xEE | Normal memory, write-back cacheable |
| Attr6 | 0x4E | Normal memory, inner WB, outer NC |
| Attr7 | 0xE4 | Normal memory, inner NC, outer WB |

#### CPACR_EL1 (Coprocessor Access Control Register)

```c
// Enable full FP/SIMD access at EL1 and EL0
cpacr_el1 &= ~CPACR_EL1_FPEN_MASK;
cpacr_el1 |= CPACR_EL1_FPEN_FULL;
```

### EL2→EL1 Transition Configuration

When transitioning from EL2 to EL1, several EL2 registers must be configured:

#### HCR_EL2 (Hypervisor Configuration Register)
```c
hcr_el2 |= HCR_EL2_RW;  // EL1 executes in AArch64 state
```

#### Other EL2 Registers
- **CPTR_EL2**: Set to 0x33FF (RES1 bits, clear TFP bit)
- **HSTR_EL2**: Set to 0 (don't trap CP15 accesses)
- **CNTHCTL_EL2**: Set to 0x3 (EL1PCTEN | EL1PCEN for timer access)
- **CNTVOFF_EL2**: Set to 0 (clear virtual timer offset)

### Security Features

#### Pointer Authentication Support

The implementation detects and configures pointer authentication:

```c
// Check ID_AA64ISAR1_EL1 for pointer auth features
uint64_t apa_field = (id_aa64isar1_el1 >> 4) & 0xF;   // Address auth, PAuth instruction
uint64_t api_field = (id_aa64isar1_el1 >> 8) & 0xF;   // Address auth, QARMA algorithm  
uint64_t gpa_field = (id_aa64isar1_el1 >> 24) & 0xF;  // Generic auth, PAuth instruction
uint64_t gpi_field = (id_aa64isar1_el1 >> 28) & 0xF;  // Generic auth, QARMA algorithm

// Enable pointer authentication in SCTLR_EL1 if available
if (pointer_auth_available) {
    sctlr_el1 |= SCTLR_EL1_EnIA;  // Enable instruction pointer auth
    sctlr_el1 |= SCTLR_EL1_EnDA;  // Enable data pointer auth (A key)
    sctlr_el1 |= SCTLR_EL1_EnDB;  // Enable data pointer auth (B key)
}
```

### Exception Level Information Structure

```c
struct arm64_exception_level_info {
    uint32_t current_el;      // Current exception level
    uint32_t target_el;       // Target exception level (usually EL1)
    bool el2_present;         // Whether EL2 is implemented
    bool el3_present;         // Whether EL3 is implemented
    uint64_t sctlr_el1;      // System Control Register EL1
    uint64_t hcr_el2;        // Hypervisor Configuration Register
    uint64_t scr_el3;        // Secure Configuration Register
};
```

## Integration with Kernel Boot Process

### Boot Sequence Integration

1. **Early Detection**: Exception level detection during kernel initialization
2. **Register Configuration**: EL1 system register setup before MMU enablement
3. **Transition Support**: EL2→EL1 transition if starting from EL2
4. **MMU Enablement**: MMU and cache activation after memory management setup

### Debug and Diagnostic Support

The implementation provides comprehensive debugging capabilities:

```c
// Dump all EL1 system registers
void arch_dump_el1_registers(void);

// Get current exception level information
status_t arch_get_exception_level_info(struct arm64_exception_level_info* info);
```

Example debug output:
```
ARM64 EL1 System Registers (from EL1):
================================
SCTLR_EL1 = 0x0000000034d5c818  (System Control)
MAIR_EL1  = 0xe44eeeaa440c0400  (Memory Attributes)
CPACR_EL1 = 0x0000000000300000  (Coprocessor Access)
TTBR0_EL1 = 0x0000000000000000  (Translation Table Base 0)
TTBR1_EL1 = 0x0000000000000000  (Translation Table Base 1)

SCTLR_EL1 decoded:
  MMU:     disabled
  D-Cache: disabled
  I-Cache: disabled
  SA:      enabled
  SA0:     enabled
```

## Testing

### Comprehensive Test Suite

The implementation includes extensive testing via `exceptions_test.cpp`:

1. **Exception Level Validation**: Test valid/invalid exception levels
2. **SCTLR_EL1 Configuration**: Verify system control register setup
3. **MAIR_EL1 Configuration**: Test memory attribute configurations
4. **Floating Point Setup**: Verify FP/SIMD access configuration
5. **HCR_EL2 Configuration**: Test EL2→EL1 transition setup
6. **Info Structure**: Validate exception level information structure
7. **MMU Enable Sequence**: Test MMU and cache enablement
8. **Pointer Authentication**: Test security feature detection

### Test Results

```
ARM64 Exception Level Management Test Suite
===========================================
✓ Exception level validation tests passed
✓ SCTLR_EL1 configuration tests passed
✓ MAIR_EL1 configuration tests passed
✓ Floating point configuration tests passed
✓ HCR_EL2 configuration tests passed
✓ Exception level info structure tests passed
✓ MMU and cache enable tests passed
✓ Pointer authentication tests passed

All exception level management tests PASSED! ✓
```

## Benefits

1. **Standards Compliance**: Full ARM Architecture Reference Manual compliance
2. **Security**: Proper isolation and security feature configuration
3. **Performance**: Optimized cache and memory management setup
4. **Flexibility**: Support for various boot scenarios (EL1/EL2 entry)
5. **Debugging**: Comprehensive diagnostic and introspection capabilities
6. **Future-Proof**: Support for emerging ARM64 features like pointer authentication

## Usage Examples

### Basic Exception Level Detection
```c
// Detect current system configuration
status_t status = arch_detect_exception_levels();
if (status != B_OK) {
    panic("Failed to detect exception levels");
}

uint32_t current_el = arch_get_current_exception_level();
dprintf("Kernel running at EL%u\n", current_el);
```

### EL1 System Register Configuration
```c
// Configure EL1 for kernel operation
status_t status = arch_configure_el1_system_registers();
if (status != B_OK) {
    panic("Failed to configure EL1 registers");
}

// Later, after memory management setup:
status = arch_enable_el1_mmu_caches();
if (status != B_OK) {
    panic("Failed to enable MMU/caches");
}
```

### EL2→EL1 Transition
```c
if (arch_get_current_exception_level() == ARM64_EL2) {
    // Configure EL2 for EL1 operation
    status_t status = arch_transition_el2_to_el1();
    if (status != B_OK) {
        panic("Failed to configure EL2→EL1 transition");
    }
}
```

---

This implementation provides a robust foundation for ARM64 exception level management, ensuring proper kernel operation across various boot scenarios while maintaining security and performance requirements.