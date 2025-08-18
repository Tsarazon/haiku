# ARM64 Early Stack Setup Implementation

## Overview

The ARM64 kernel now includes comprehensive early stack setup functionality that ensures proper stack configuration according to ARM64 AAPCS (Procedure Call Standard) requirements. This implementation provides reliable stack management from the very beginning of kernel execution.

## Key Features Implemented

### 1. **ARM64 AAPCS64 Compliance**
- **16-byte stack alignment**: All stack pointers are aligned to 16-byte boundaries as required by ARM64 AAPCS
- **Automatic alignment correction**: Misaligned bootloader stacks are automatically corrected
- **Runtime alignment verification**: Multiple validation points ensure alignment is maintained

### 2. **Bootloader Stack Validation**
- **Range validation**: Stack pointers must be within reasonable memory ranges (64KB - 4GB)
- **Space validation**: Ensures sufficient stack space (minimum 8KB for early kernel operations)
- **Null pointer protection**: Handles cases where bootloader provides invalid stack

### 3. **Emergency Stack Allocation**
- **64KB emergency stack**: Statically allocated emergency stack in BSS section
- **Automatic fallback**: Used when bootloader stack is unusable or insufficient
- **Proper alignment**: Emergency stack maintains ARM64 AAPCS alignment requirements

### 4. **Stack Overflow Protection**
- **Stack canary system**: Implements canary values to detect stack corruption
- **Boundary checking**: Validates stack operations stay within allocated bounds
- **Early detection**: Identifies stack issues before they cause system corruption

### 5. **Comprehensive Error Handling**
- **Distinctive panic codes**: Each error condition has unique panic code for debugging
- **Debug information storage**: Stack configuration stored in boot_info structure
- **Non-fatal warnings**: Handles recoverable issues with warnings rather than panics

## Implementation Details

### Stack Setup Function (`setup_early_stack`)

The core stack setup function implements a multi-phase approach:

#### Phase 1: Bootloader Stack Validation
```assembly
// Check if bootloader stack pointer is reasonable
cbz     x24, use_emergency_stack    // If SP is NULL, use emergency stack

// Check stack alignment (ARM64 AAPCS requires 16-byte alignment)
tst     x24, #0xf                   // Test 16-byte alignment
b.ne    realign_bootloader_stack    // Realign if not properly aligned
```

#### Phase 2: Final Configuration and Validation
```assembly
// Final validation that SP is 16-byte aligned
mov     x0, sp
tst     x0, #0xf
b.ne    stack_setup_panic           // Should never happen
```

### Emergency Stack Structure

```assembly
.bss
.align 4
.globl early_emergency_stack_bottom
early_emergency_stack_bottom:
    .skip   0x10000                     // 64KB emergency stack space

.globl early_emergency_stack_top
early_emergency_stack_top:
```

### Stack Configuration Constants

```assembly
.equ EARLY_STACK_SIZE,          0x10000     // 64KB emergency stack
.equ STACK_ALIGNMENT_MASK,      0xf         // 16-byte alignment for ARM64 AAPCS
.equ STACK_SAFETY_MARGIN,       0x100       // 256 bytes safety margin
.equ STACK_CANARY_OFFSET,       0x1000      // 4KB below SP for canary
.equ STACK_MINIMUM_SPACE,       0x2000      // 8KB minimum for early kernel
```

## Error Codes and Debugging

### Stack-Related Panic Codes

| Code | Mnemonic | Description |
|------|----------|-------------|
| 0xDEAD57CF | STKF | Stack setup fatal error |
| 0xDEAD57C0 | STC0 | Stack corrupted |
| 0xDEAD570F | STOF | Stack overflow |
| 0xDEAD5CA7 | SCAT | Stack canary violated |

### Boot Information Structure

The stack configuration is recorded in `arm64_boot_info`:

```c
struct arm64_boot_info {
    uint64_t dtb_address;               // Offset 0
    uint64_t original_currentel;        // Offset 8
    uint64_t validation_flags;          // Offset 16
    // ...
    uint64_t original_bootloader_sp;    // Offset 88
    uint64_t cpu_features;              // Offset 96
    uint64_t stack_setup_flags;         // Offset 104
    uint64_t final_kernel_sp;           // Offset 112
    uint64_t allocated_stack_size;      // Offset 120
};
```

## Testing

### Comprehensive Test Suite

The implementation includes extensive testing via `stack_setup_test.cpp`:

1. **AAPCS64 Alignment Tests**: Verify 16-byte alignment requirements
2. **Range Validation Tests**: Test stack pointer range checking
3. **Space Calculation Tests**: Validate stack size calculations
4. **Canary Tests**: Verify stack overflow protection
5. **Emergency Stack Tests**: Test fallback stack allocation
6. **Panic Code Tests**: Verify error reporting mechanisms
7. **Realignment Tests**: Test automatic stack alignment correction

### Test Results

```
ARM64 Early Stack Setup Test Suite
===================================
✓ ARM64 AAPCS64 alignment tests passed
✓ Stack range validation tests passed
✓ Stack space calculation tests passed
✓ Stack canary tests passed
✓ Emergency stack tests passed
✓ Panic code tests passed
✓ Stack realignment tests passed

All early stack setup tests PASSED! ✓
```

## Integration with Kernel Boot Process

### Boot Sequence Integration

1. **Entry Point**: `_start` in `arch_start.S`
2. **Early Setup**: `setup_early_stack` called immediately after register preservation
3. **Validation**: Register state validation proceeds with reliable stack
4. **Kernel Args**: Stack used for kernel_args structure allocation
5. **Kernel Main**: Clean stack provided to C kernel code

### ARM64 AAPCS Compliance Points

- **Function calls**: All function calls maintain 16-byte stack alignment
- **Parameter passing**: Stack parameters properly aligned
- **Return addresses**: Stack-based return address storage properly aligned
- **Local variables**: Stack-allocated variables maintain alignment requirements

## Benefits

1. **Reliability**: Robust stack management prevents early boot failures
2. **Standards Compliance**: Full ARM64 AAPCS64 compatibility
3. **Debugging**: Comprehensive error reporting and state tracking
4. **Safety**: Multiple protection mechanisms prevent stack corruption
5. **Flexibility**: Handles various bootloader stack configurations

## Future Enhancements

1. **Multi-CPU Support**: Extend for secondary CPU stack setup
2. **Dynamic Sizing**: Runtime stack size adjustment based on requirements
3. **Performance Monitoring**: Stack usage tracking and optimization
4. **Security Features**: Additional stack protection mechanisms

---

This implementation provides a solid foundation for ARM64 kernel stack management, ensuring reliable operation from the earliest stages of boot through kernel initialization.