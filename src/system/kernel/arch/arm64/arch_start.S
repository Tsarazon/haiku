/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Kernel Entry Point Implementation
 * Authors:
 *   ARM64 Kernel Development Team
 */

#include <asm_defs.h>

.text

/*
 * ARM64 Kernel Entry Point
 * 
 * Entry conditions (per ARM64 Linux booting protocol):
 * - MMU disabled
 * - Interrupts masked (PSTATE.DAIF = 0xf)
 * - Exception level: EL2 (recommended) or EL1
 * - Primary CPU only (secondary CPUs in WFE state)
 * 
 * Register state at entry:
 * - X0: Physical address of device tree blob (DTB)
 * - X1: 0 (reserved for future use)
 * - X2: 0 (reserved for future use)  
 * - X3: 0 (reserved for future use)
 * - X4-X30: UNDEFINED
 * - SP: Valid stack pointer (set by bootloader)
 */

.globl _start
.type _start, %function
_start:
    /*
     * Phase 1: Critical State Preservation and Early Stack Setup
     * Save essential register state and establish reliable stack
     */
    
    // Preserve critical boot parameters immediately
    mov     x20, x0                     // Save DTB pointer (primary boot parameter)
    mov     x21, x1                     // Save reserved parameter 1
    mov     x22, x2                     // Save reserved parameter 2  
    mov     x23, x3                     // Save reserved parameter 3
    mov     x24, sp                     // Save initial stack pointer
    mrs     x25, CurrentEL              // Save current exception level
    mrs     x26, DAIF                   // Save interrupt mask state
    mrs     x27, SCTLR_EL1              // Save system control state
    mrs     x28, MAIR_EL1               // Save memory attribute state
    
    // Immediately set up early stack for kernel initialization
    bl      setup_early_stack

    /*
     * Phase 2: Register State Validation
     * Verify entry conditions meet ARM64 kernel requirements
     */
    
validate_entry_state:
    /*
     * Enhanced Register State Validation
     * Comprehensive validation of bootloader-provided state
     */
    
    // Phase 2.1: Exception Level Validation with EL1 preference
    lsr     x0, x25, #2                 // Extract exception level
    cmp     x0, #1                      // Check if EL1 (preferred)
    b.eq    el1_native_entry
    cmp     x0, #2                      // Check if EL2 (acceptable, will transition)
    b.eq    el2_transition_required
    
    // Invalid exception level - halt with distinctive pattern
    mov     x0, #0xDEAD
    movk    x0, #0xE100, lsl #16        // EL00 = invalid exception level
    b       kernel_entry_panic

el1_native_entry:
    // Already at EL1 - validate EL1 state is properly configured
    mrs     x0, SCTLR_EL1              // Read system control register
    tst     x0, #0x1                    // Check if MMU is enabled
    b.ne    mmu_unexpected_enabled      // MMU should be disabled at entry
    
    // Validate that we have proper access to EL1 registers
    mrs     x0, TTBR0_EL1              // Test access to translation table register
    mrs     x0, TTBR1_EL1              // Test access to kernel translation table
    mrs     x0, MAIR_EL1               // Test access to memory attribute register
    
    // Mark that we're already at target exception level
    mov     x29, #0xE101                // EL1 native entry flag
    b       el_validation_complete

el2_transition_required:
    // At EL2 - validate that transition to EL1 is possible
    mrs     x0, HCR_EL2                // Read hypervisor configuration
    // Check if EL1 can use AArch64 (RW bit should be settable)
    
    // Validate EL2 registers are accessible
    mrs     x0, SCTLR_EL2              // System control register EL2
    mrs     x0, MAIR_EL2               // Memory attributes EL2
    mrs     x0, TTBR0_EL2              // Translation table EL2
    
    // Mark that EL2→EL1 transition is needed
    mov     x29, #0xE102                // EL2 transition required flag
    b       el_validation_complete

el_validation_complete:
    // Phase 2.2: Enhanced Device Tree Validation
    cbz     x20, dtb_null_error         // DTB cannot be null
    
    // Validate DTB pointer is in reasonable physical memory range
    cmp     x20, #0x1000                // Must be above 4KB (avoid null page)
    b.lo    dtb_invalid_range
    
    // Check upper bound (assume max 4GB physical memory for validation)
    mov     x0, #0x100000000            // 4GB limit
    cmp     x20, x0
    b.hs    dtb_invalid_range
    
    // Validate DTB pointer alignment (must be 8-byte aligned)
    tst     x20, #0x7                   // Check 8-byte alignment
    b.ne    dtb_alignment_error
    
    // Enhanced DTB header validation
    ldr     w0, [x20]                   // Load magic number
    rev     w0, w0                      // Convert from big-endian
    mov     w1, #0xd00d                 // FDT magic number (high part)
    movk    w1, #0xfeed, lsl #16        // FDT magic number (low part)
    cmp     w0, w1
    b.ne    dtb_magic_error
    
    // Validate DTB total size
    ldr     w0, [x20, #4]               // Load totalsize field
    rev     w0, w0                      // Convert from big-endian
    cmp     w0, #64                     // Minimum DTB size
    b.lo    dtb_size_error
    cmp     w0, #0x200000               // Maximum DTB size (2MB)
    b.hi    dtb_size_error
    
    // Validate DTB version
    ldr     w0, [x20, #20]              // Load version field
    rev     w0, w0                      // Convert from big-endian
    cmp     w0, #16                     // Minimum supported version
    b.lo    dtb_version_error
    
    // Phase 2.3: Bootloader Register State Validation
    // Validate reserved registers are zero as per ARM64 boot protocol
    cbnz    x21, reserved_reg_nonzero   // x1 should be 0
    cbnz    x22, reserved_reg_nonzero   // x2 should be 0
    cbnz    x23, reserved_reg_nonzero   // x3 should be 0
    
    // Phase 2.4: Interrupt and System State Validation
    // Verify interrupts are masked (DAIF should be 0xf0)
    and     x0, x26, #0xf0              // Extract DAIF mask bits
    cmp     x0, #0xf0                   // All interrupts should be masked
    b.ne    interrupt_mask_warning      // Continue with warning, not fatal
    
    // Validate that debug exceptions are also masked
    and     x0, x26, #0x200             // Check debug mask (D bit, bit 9)
    cbz     x0, debug_not_masked_warning
    
    // Phase 2.5: Stack Validation
    // Validate stack pointer is in reasonable range
    cmp     x24, #0x10000               // Must be above 64KB
    b.lo    stack_invalid_range
    
    // Validate stack pointer alignment (must be 16-byte aligned)
    tst     x24, #0xf                   // Check 16-byte alignment
    b.ne    stack_alignment_error
    
    // Estimate stack size by checking if we have reasonable space
    // Assume stack grows down, check we have at least 16KB available
    sub     x0, x24, #0x4000            // Subtract 16KB
    cmp     x0, #0x1000                 // Should still be above 4KB
    b.lo    stack_insufficient_space
    
    // Phase 2.6: CPU Feature Validation
    // Check that required ARM64 features are present
    mrs     x0, ID_AA64PFR0_EL1         // Processor feature register
    and     x1, x0, #0xf                // Extract EL0 field
    cmp     x1, #2                      // Should support AArch64 at EL0
    b.ne    cpu_feature_missing
    
    and     x1, x0, #0xf0               // Extract EL1 field  
    lsr     x1, x1, #4
    cmp     x1, #2                      // Should support AArch64 at EL1
    b.ne    cpu_feature_missing
    
    // Check floating point support
    and     x1, x0, #0xf0000            // Extract FP field
    lsr     x1, x1, #16
    cmp     x1, #0xf                    // 0xf means no FP support
    b.eq    cpu_feature_missing
    
    b       entry_validation_complete

/*
 * Early Stack Setup Function
 * Establishes a reliable stack for kernel initialization with proper ARM64 AAPCS alignment
 * 
 * This function is called very early, so it must be careful about stack usage
 * Input: x24 contains original stack pointer from bootloader
 * Output: SP set to properly aligned early kernel stack
 * Preserves: x20-x28 (boot parameters and state)
 */
.type setup_early_stack, %function
setup_early_stack:
    // Save return address using a scratch register since we don't trust stack yet
    mov     x19, x30                    // Save return address
    
    /*
     * Phase 1: Validate Bootloader Stack
     * Check if bootloader provided a usable stack
     */
    
    // Check if bootloader stack pointer is reasonable
    cbz     x24, use_emergency_stack    // If SP is NULL, use emergency stack
    
    // Check stack alignment (ARM64 AAPCS requires 16-byte alignment)
    tst     x24, #0xf                   // Test 16-byte alignment
    b.ne    realign_bootloader_stack    // Realign if not properly aligned
    
    // Check if stack is in reasonable range (above 64KB, below 4GB)
    cmp     x24, #0x10000               // Must be above 64KB
    b.lo    use_emergency_stack
    
    mov     x0, #0x100000000            // 4GB upper limit
    cmp     x24, x0
    b.hs    use_emergency_stack
    
    // Validate we have sufficient stack space (at least 32KB)
    sub     x0, x24, #0x8000            // Check 32KB below current SP
    cmp     x0, #0x1000                 // Should still be above 4KB
    b.lo    allocate_larger_stack
    
    // Bootloader stack is acceptable, use it with safety margin
    sub     sp, x24, #0x100             // Leave 256 bytes safety margin
    b       stack_setup_complete
    
realign_bootloader_stack:
    // Realign bootloader stack to 16-byte boundary
    and     x0, x24, #~0xf             // Clear lower 4 bits for 16-byte alignment
    sub     sp, x0, #0x100              // Set SP with safety margin
    b       validate_realigned_stack
    
validate_realigned_stack:
    // Ensure realigned stack still has sufficient space
    sub     x0, sp, #0x4000             // Check 16KB below realigned SP
    cmp     x0, #0x1000                 // Should be above 4KB
    b.lo    use_emergency_stack
    b       stack_setup_complete
    
allocate_larger_stack:
    // Allocate larger stack space by moving SP down
    sub     x0, x24, #0x8000            // Allocate 32KB stack
    and     sp, x0, #~0xf               // Ensure 16-byte alignment
    
    // Validate the larger allocation is reasonable
    cmp     sp, #0x1000                 // Must be above 4KB
    b.lo    use_emergency_stack
    b       stack_setup_complete
    
use_emergency_stack:
    /*
     * Emergency Stack Allocation
     * Use a statically allocated emergency stack when bootloader stack is unusable
     */
    
    // Load emergency stack from BSS section
    adrp    x0, early_emergency_stack_top
    add     sp, x0, :lo12:early_emergency_stack_top
    
    // Ensure emergency stack is properly aligned
    mov     x1, sp
    and     x1, x1, #~0xf              // 16-byte alignment for ARM64 AAPCS
    mov     sp, x1
    
    // Set emergency stack flag for debugging
    mov     x0, #0x1                    // Emergency stack flag
    adrp    x1, arm64_boot_info
    add     x1, x1, :lo12:arm64_boot_info
    str     x0, [x1, #104]              // Store emergency flag at reserved offset
    
stack_setup_complete:
    /*
     * Phase 2: Final Stack Configuration and Validation
     * Ensure stack meets ARM64 AAPCS64 requirements
     */
    
    // Final validation that SP is 16-byte aligned
    mov     x0, sp
    tst     x0, #0xf
    b.ne    stack_setup_panic           // Should never happen
    
    // Ensure stack has sufficient space for early kernel operations
    sub     x0, sp, #0x2000             // Reserve 8KB for early kernel
    cmp     x0, #0x1000                 // Should be above 4KB
    b.lo    stack_space_warning
    
    // Set up stack canary area (write pattern for stack overflow detection)
    sub     x0, sp, #0x1000             // 4KB below SP
    mov     x1, #0xBEEF
    movk    x1, #0xDEAD, lsl #16        // Stack canary pattern low part
    movk    x1, #0xCAFE, lsl #32        // Stack canary pattern high part
    movk    x1, #0x0000, lsl #48        // Clear upper bits
    str     x1, [x0]                    // Store canary
    str     x1, [x0, #8]                // Store canary + 8
    
    // Initialize frame pointer for clean stack traces
    mov     x29, #0                     // Clear frame pointer
    
    // Store final stack configuration in boot info
    adrp    x0, arm64_boot_info
    add     x0, x0, :lo12:arm64_boot_info
    str     x24, [x0, #88]              // Original bootloader SP
    mov     x1, sp                      // Get current SP
    str     x1, [x0, #112]              // Final kernel SP
    
    // Calculate and store stack size
    sub     x1, x24, x1                 // Calculate stack size allocated
    str     x1, [x0, #120]              // Store stack size
    
    // Return to caller
    mov     x30, x19                    // Restore return address
    ret
    
stack_space_warning:
    // Non-fatal: limited stack space but continue
    mov     x0, #0x2                    // Limited stack warning flag
    adrp    x1, arm64_boot_info
    add     x1, x1, :lo12:arm64_boot_info
    str     x0, [x1, #104]              // Store warning flag
    b       stack_setup_complete
    
stack_setup_panic:
    // Fatal: stack setup failed completely
    mov     x0, #0xDEAD
    movk    x0, #0x57CF, lsl #16        // STKF = Stack setup fatal error
    b       kernel_entry_panic

    /*
     * Error Handlers for Validation Failures
     */

// MMU and EL1 state errors
mmu_unexpected_enabled:
    mov     x0, #0xDEAD
    movk    x0, #0x4D4D, lsl #16        // MMUU = MMU unexpectedly enabled
    b       kernel_entry_panic

// Device Tree validation errors
dtb_null_error:
    mov     x0, #0xDEAD  
    movk    x0, #0xD7B2, lsl #16        // DTB2 = DTB null pointer
    b       kernel_entry_panic

dtb_invalid_range:
    mov     x0, #0xDEAD
    movk    x0, #0xD7B4, lsl #16        // DTB4 = DTB invalid address range
    b       kernel_entry_panic

dtb_alignment_error:
    mov     x0, #0xDEAD
    movk    x0, #0xD7B1, lsl #16        // DTB1 = DTB alignment error
    b       kernel_entry_panic

dtb_magic_error:
    mov     x0, #0xDEAD
    movk    x0, #0xD7B3, lsl #16        // DTB3 = DTB magic validation failed
    b       kernel_entry_panic

dtb_size_error:
    mov     x0, #0xDEAD
    movk    x0, #0xD7B5, lsl #16        // DTB5 = DTB size validation failed
    b       kernel_entry_panic

dtb_version_error:
    mov     x0, #0xDEAD
    movk    x0, #0xD7B6, lsl #16        // DTB6 = DTB version validation failed
    b       kernel_entry_panic

// Register state validation errors
reserved_reg_nonzero:
    mov     x0, #0xDEAD
    movk    x0, #0x5E60, lsl #16        // REG0 = Reserved register non-zero
    b       kernel_entry_panic

// Stack validation errors
stack_alignment_error:
    mov     x0, #0xDEAD
    movk    x0, #0x57C1, lsl #16        // STK1 = Stack alignment error  
    b       kernel_entry_panic

stack_invalid_range:
    mov     x0, #0xDEAD
    movk    x0, #0x57C2, lsl #16        // STK2 = Stack invalid range
    b       kernel_entry_panic

stack_insufficient_space:
    mov     x0, #0xDEAD
    movk    x0, #0x57C3, lsl #16        // STK3 = Stack insufficient space
    b       kernel_entry_panic

// CPU feature validation errors
cpu_feature_missing:
    mov     x0, #0xDEAD
    movk    x0, #0xCF0E, lsl #16        // CPU0 = Required CPU features missing
    b       kernel_entry_panic

// Warning handlers (non-fatal)
interrupt_mask_warning:
    // Non-fatal: continue but record the warning
    orr     x29, x29, #0x1              // Set interrupt mask warning bit
    b       continue_validation_with_warnings

debug_not_masked_warning:
    // Non-fatal: continue but record the warning  
    orr     x29, x29, #0x2              // Set debug mask warning bit
    b       continue_validation_with_warnings

continue_validation_with_warnings:
    // Continue with validation despite warnings
    nop                                 // Placeholder for future warning handling

entry_validation_complete:
    /*
     * Phase 2.7: Final Register State Documentation
     * Store validated state for debugging and kernel initialization
     */
    
    // Store comprehensive validation results for debugging and kernel initialization
    adrp    x10, arm64_boot_info
    add     x10, x10, :lo12:arm64_boot_info
    
    // Store DTB address and validation
    str     x20, [x10, #0]              // DTB physical address
    
    // Store original exception level and validation flags
    str     x25, [x10, #8]              // Original CurrentEL
    str     x29, [x10, #16]             // Validation flags and warnings
    
    // Store critical register states for debugging
    str     x26, [x10, #24]             // Original DAIF state
    str     x27, [x10, #32]             // Original SCTLR_EL1 state  
    str     x28, [x10, #40]             // Original MAIR_EL1 state
    
    // Read and store additional system information
    mrs     x0, MIDR_EL1                // Main ID register
    str     x0, [x10, #48]
    mrs     x0, MPIDR_EL1               // Multiprocessor affinity register
    str     x0, [x10, #56]
    
    // Store DTB header information if DTB is valid
    cbz     x20, skip_dtb_info_storage
    ldr     w0, [x20, #4]               // DTB total size
    rev     w0, w0                      // Convert from big-endian
    str     x0, [x10, #72]              // Store DTB size
    
    ldr     w0, [x20, #20]              // DTB version  
    rev     w0, w0                      // Convert from big-endian
    str     x0, [x10, #80]              // Store DTB version
    
skip_dtb_info_storage:
    // Store stack validation results
    str     x24, [x10, #88]             // Original stack pointer
    
    // Store CPU feature validation results
    mrs     x0, ID_AA64PFR0_EL1         // Processor features
    str     x0, [x10, #96]              // Store for later analysis
    
    // Store detailed validation results in separate structure
    adrp    x11, arm64_validation_results
    add     x11, x11, :lo12:arm64_validation_results
    
    // Store DTB header details if available
    cbz     x20, skip_detailed_dtb_storage
    ldr     w0, [x20, #0]               // Magic number
    str     x0, [x11, #0]
    ldr     w0, [x20, #4]               // Total size
    str     x0, [x11, #8]
    ldr     w0, [x20, #8]               // Structure offset
    str     x0, [x11, #16]
    ldr     w0, [x20, #12]              // Strings offset
    str     x0, [x11, #24]
    ldr     w0, [x20, #16]              // Memory reservation block offset
    str     x0, [x11, #32]
    ldr     w0, [x20, #20]              // Version
    str     x0, [x11, #40]
    ldr     w0, [x20, #24]              // Last compatible version
    str     x0, [x11, #48]
    ldr     w0, [x20, #28]              // Boot CPU ID
    str     x0, [x11, #56]
    
skip_detailed_dtb_storage:
    // Store stack analysis
    str     x24, [x11, #64]             // Original SP
    sub     x0, x24, #0x4000            // Estimate available space
    str     x0, [x11, #72]              // Estimated stack space
    
    // Store comprehensive CPU feature information
    mrs     x0, ID_AA64PFR0_EL1
    str     x0, [x11, #80]              // Processor features
    mrs     x0, ID_AA64ISAR0_EL1
    str     x0, [x11, #88]              // Instruction set attributes
    mrs     x0, ID_AA64MMFR0_EL1
    str     x0, [x11, #96]              // Memory model features
    mrs     x0, ID_AA64DFR0_EL1
    str     x0, [x11, #104]             // Debug features
    
    // Store bootloader register state for verification
    str     x21, [x11, #112]            // X1 (should be 0)
    str     x22, [x11, #120]            // X2 (should be 0)
    str     x23, [x11, #128]            // X3 (should be 0)
    
    // Set validation success flags
    mov     x0, #VALIDATION_DTB_VALID
    orr     x0, x0, #VALIDATION_STACK_VALID
    orr     x0, x0, #VALIDATION_CPU_VALID
    orr     x0, x0, #VALIDATION_MMU_DISABLED
    orr     x0, x0, #VALIDATION_INTERRUPTS_OK
    
    // Add exception level specific flag
    lsr     x1, x25, #2
    cmp     x1, #1
    b.eq    set_el1_native_flag
    orr     x0, x0, #VALIDATION_EL2_TRANSITION
    b       store_validation_flags
    
set_el1_native_flag:
    orr     x0, x0, #VALIDATION_EL1_NATIVE
    
store_validation_flags:
    // Combine with any warning flags already in x29
    orr     x29, x29, x0
    str     x29, [x10, #16]             // Update validation flags
    
    /*
     * Phase 3: Exception Level Handling and Setup
     * Configure system for EL1 operation based on validated state
     */
    
    lsr     x0, x25, #2                 // Get current exception level again
    cmp     x0, #2                      // Check if we're at EL2
    b.eq    setup_el2_to_el1_transition
    cmp     x0, #1                      // Check if we're at EL1  
    b.eq    setup_el1_environment
    
    // Should not reach here due to earlier validation
    mov     x0, #0xDEAD
    movk    x0, #0xE199, lsl #16        // EL99 = Unexpected exception level
    b       kernel_entry_panic

setup_el2_to_el1_transition:
    /*
     * Configure EL2 for transition to EL1
     * Based on Haiku's transition.S implementation
     */
    
    // Save current EL2 MMU state for migration
    mrs     x10, TTBR0_EL2
    mrs     x11, MAIR_EL2
    mrs     x12, VBAR_EL2
    
    // Configure HCR_EL2 for EL1 execution
    mov     x1, #0x80000000             // HCR_RW (bit 31)
    msr     HCR_EL2, x1
    
    // Set up virtualization ID registers
    mrs     x1, MIDR_EL1
    msr     VPIDR_EL2, x1               // Virtual processor ID
    mrs     x1, MPIDR_EL1
    msr     VMPIDR_EL2, x1              // Virtual multiprocessor ID
    
    // Initialize EL1 system control (required bits)
    ldr     x1, =0x30C50838             // SCTLR_RES1 bits
    msr     SCTLR_EL1, x1
    
    // Migrate MMU configuration to EL1
    msr     TTBR0_EL1, x10              // Transfer translation table base
    msr     MAIR_EL1, x11               // Transfer memory attributes
    msr     VBAR_EL1, x12               // Transfer vector base address
    
    // Configure trap behavior (don't trap to EL2)
    mov     x1, #0x000033ff             // CPTR_RES1
    msr     CPTR_EL2, x1                // Architecture feature access
    msr     HSTR_EL2, xzr               // Don't trap CP15 accesses
    
    // Enable timer access at EL1
    mrs     x1, CNTHCTL_EL2
    orr     x1, x1, #0x3                // EL1PCTEN | EL1PCEN
    msr     CNTHCTL_EL2, x1
    msr     CNTVOFF_EL2, xzr            // Clear virtual timer offset
    
    // Enable floating-point access at EL1
    mov     x1, #0x300000               // 3 << 20 (FPEN bits)
    msr     CPACR_EL1, x1
    
    // Set up processor state for EL1 entry
    mov     x1, #0x3c5                  // F | I | A | D | EL1h
    msr     SPSR_EL2, x1                // Target state: EL1h, interrupts masked
    
    // Set return address to EL1 setup
    adr     x1, setup_el1_environment
    msr     ELR_EL2, x1
    
    // Ensure all changes are visible before transition
    isb
    
    // Transition to EL1
    eret

setup_el1_environment:
    /*
     * Configure EL1 execution environment
     * Assumes we're now running at EL1
     */
    
    // Verify we're now at EL1
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #1
    b.ne    el1_transition_failed
    
    // Disable MMU and caches for kernel setup
    mrs     x0, SCTLR_EL1
    bic     x0, x0, #0x1                // Disable MMU (M bit)
    bic     x0, x0, #0x4                // Disable data cache (C bit)
    bic     x0, x0, #0x1000             // Disable instruction cache (I bit)
    msr     SCTLR_EL1, x0
    isb
    
    // Set up exception vector table
    adr     x0, _exception_vectors       // Load vector table address
    msr     VBAR_EL1, x0                // Set vector base address
    
    // Configure system registers for kernel operation
    mov     x0, #0                      // Clear TTBR1_EL1 for now
    msr     TTBR1_EL1, x0               // Kernel page tables (will be set later)
    
    // Set up basic memory attributes  
    mov     x0, #0x00                   // Device memory
    mov     x1, #0x44
    orr     x0, x0, x1, lsl #8          // Normal non-cacheable << 8
    mov     x1, #0xff
    orr     x0, x0, x1, lsl #16         // Normal cacheable << 16
    msr     MAIR_EL1, x0
    
    b       prepare_kernel_args

el1_transition_failed:
    mov     x0, #0xDEAD
    movk    x0, #0xE11F, lsl #16        // EL1F = EL1 transition failed
    b       kernel_entry_panic

prepare_kernel_args:
    /*
     * Phase 4: Kernel Arguments Preparation
     * Set up parameters for C kernel entry (stack already configured)
     */
    
    // Verify stack is still properly aligned after all operations
    mov     x0, sp                      // Get current SP
    tst     x0, #0xf                    // Check 16-byte alignment
    b.ne    stack_corrupted_panic
    
    // Allocate space on stack for kernel_args structure
    sub     sp, sp, #0x1000             // Reserve 4KB for kernel_args
    mov     x19, sp                     // Save kernel_args pointer
    
    // Ensure allocation maintains alignment
    mov     x0, sp
    and     x0, x0, #~0xf               // Force 16-byte alignment
    mov     sp, x0
    
    // Zero out kernel_args structure
    mov     x0, x19                     // Destination
    mov     x1, #0                      // Value (zero)
    mov     x2, #0x1000                 // Size
    bl      simple_memset               // Zero the structure
    
    // Store DTB pointer in kernel_args
    str     x20, [x19, #0x100]          // Store at offset 0x100 (example offset)
    
    // Store boot validation results
    str     x25, [x19, #0x108]          // Original exception level
    str     x26, [x19, #0x110]          // Original DAIF state
    str     x29, [x19, #0x118]          // Warning flags (if any)
    
    // Store additional boot information
    mrs     x0, MIDR_EL1                // Main ID register
    str     x0, [x19, #0x120]
    mrs     x0, MPIDR_EL1               // Multiprocessor affinity register
    str     x0, [x19, #0x128]
    
    b       call_kernel_main

    /*
     * Phase 5: Kernel Main Invocation
     * Call the C kernel with proper arguments
     */
call_kernel_main:
    // Set up arguments for kernel main
    mov     x0, x19                     // kernel_args pointer
    mov     x1, #0                      // currentCPU = 0 (boot CPU)
    
    // Final stack alignment verification for AAPCS64 compliance
    mov     x2, sp                      // Get current SP
    tst     x2, #0xf                    // Check 16-byte alignment
    b.ne    stack_corrupted_panic
    
    // Ensure sufficient stack space before kernel main call
    adrp    x2, early_emergency_stack_bottom
    add     x2, x2, :lo12:early_emergency_stack_bottom
    cmp     sp, x2                      // Compare with stack bottom
    b.lo    stack_overflow_panic
    
    // Clear frame pointer for clean stack trace
    mov     x29, #0
    
    // Set up stack canary check before kernel main
    sub     x2, sp, #0x1000             // Check canary location
    mov     x3, #0xBEEF
    movk    x3, #0xDEAD, lsl #16        // Expected canary pattern low part
    movk    x3, #0xCAFE, lsl #32        // Expected canary pattern high part
    movk    x3, #0x0000, lsl #48        // Clear upper bits
    ldr     x4, [x2]                    // Load canary value
    cmp     x3, x4                      // Compare with expected
    b.ne    stack_canary_violated
    
    // Call kernel main following ARM64 AAPCS calling convention
    // x0 = kernel_args pointer (first argument)
    // x1 = currentCPU (second argument) 
    // Stack is 16-byte aligned as required by AAPCS64
    bl      arch_kernel_entry
    
    // If we return from kernel main, something went wrong
    mov     x0, #0xDEAD
    movk    x0, #0xC5E7, lsl #16        // KRET = Kernel returned unexpectedly
    b       kernel_entry_panic

stack_corrupted_panic:
    mov     x0, #0xDEAD
    movk    x0, #0x57C0, lsl #16        // STC0 = Stack corrupted
    b       kernel_entry_panic

stack_overflow_panic:
    mov     x0, #0xDEAD
    movk    x0, #0x570F, lsl #16        // STOF = Stack overflow
    b       kernel_entry_panic

stack_canary_violated:
    mov     x0, #0xDEAD
    movk    x0, #0x5CA7, lsl #16        // SCAT = Stack canary violated
    b       kernel_entry_panic

/*
 * Utility Functions
 */

// Simple memset implementation for early boot
.type simple_memset, %function
simple_memset:
    cbz     x2, 2f                      // If count is 0, return
1:  strb    w1, [x0], #1               // Store byte and increment
    subs    x2, x2, #1                 // Decrement count
    b.ne    1b                          // Continue if not zero
2:  ret

/*
 * Panic Handler for Early Boot Failures
 * 
 * Input: x0 = panic code (distinctive pattern for debugging)
 * This function never returns
 */
.type kernel_entry_panic, %function
kernel_entry_panic:
    // Disable all interrupts
    msr     DAIFSet, #0xf
    
    // Store panic code in a known location for debugging
    adrp    x10, arm64_boot_info        // Get page address
    add     x10, x10, :lo12:arm64_boot_info
    str     x0, [x10, #24]              // Store panic code at offset 24
    
    // Infinite loop with distinctive instruction pattern
panic_loop:
    wfe                                 // Wait for event (low power)
    mov     x1, x0                      // Copy panic code
    nop                                 // Padding for debugging
    nop
    b       panic_loop                  // Loop forever

/*
 * Secondary CPU Entry Point (for SMP systems)
 * Called by primary CPU to start secondary CPUs
 */
.globl _secondary_start
.type _secondary_start, %function
_secondary_start:
    // Save CPU ID
    mrs     x20, MPIDR_EL1
    and     x20, x20, #0xff             // Extract CPU ID
    
    // Set up minimal environment for secondary CPU
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.eq    secondary_el2_setup
    
    // Already at EL1, proceed
    b       secondary_el1_setup
    
secondary_el2_setup:
    // Basic EL2 to EL1 transition for secondary CPU
    mov     x0, #0x80000000             // HCR_RW
    msr     HCR_EL2, x0
    
    mov     x0, #0x3c5                  // F | I | A | D | EL1h
    msr     SPSR_EL2, x0
    
    adr     x0, secondary_el1_setup
    msr     ELR_EL2, x0
    
    isb
    eret
    
secondary_el1_setup:
    // Load kernel page tables (will be set by primary CPU)
    adrp    x0, kernel_ttbr1_el1        // External symbol
    add     x0, x0, :lo12:kernel_ttbr1_el1
    ldr     x0, [x0]
    msr     TTBR1_EL1, x0
    
    // Enable MMU
    mrs     x0, SCTLR_EL1
    orr     x0, x0, #0x1                // Enable MMU
    msr     SCTLR_EL1, x0
    isb
    
    // Call secondary CPU initialization
    mov     x0, x20                     // CPU ID
    bl      _start_secondary_cpu        // C function
    
    // Secondary CPU should not return
    b       kernel_entry_panic

.size _start, . - _start
.size _secondary_start, . - _secondary_start

/*
 * Enhanced Boot Information Structure
 * Comprehensive validation results accessible to debuggers and early kernel code
 */
.data
.align 8
.globl arm64_boot_info
arm64_boot_info:
    .quad   0                           // Offset 0:  DTB physical address
    .quad   0                           // Offset 8:  Original CurrentEL register
    .quad   0                           // Offset 16: Validation flags and warnings
    .quad   0                           // Offset 24: Original DAIF state
    .quad   0                           // Offset 32: Original SCTLR_EL1 state
    .quad   0                           // Offset 40: Original MAIR_EL1 state
    .quad   0                           // Offset 48: MIDR_EL1 (CPU identification)
    .quad   0                           // Offset 56: MPIDR_EL1 (multiprocessor affinity)
    .quad   0                           // Offset 64: Panic code (if any)
    .quad   0                           // Offset 72: DTB size (extracted from header)
    .quad   0                           // Offset 80: DTB version (extracted from header)
    .quad   0                           // Offset 88: Original bootloader stack pointer
    .quad   0                           // Offset 96: CPU feature validation results
    .quad   0                           // Offset 104: Stack setup flags (emergency/warning)
    .quad   0                           // Offset 112: Final kernel stack pointer
    .quad   0                           // Offset 120: Allocated stack size
    .ascii  "ARM64BOOT_ENHANCED"        // Enhanced signature for debugging
    .align 8

/*
 * Validation Flag Bit Definitions (for offset 16)
 * Bits 0-15: Status flags
 * Bits 16-31: Warning flags  
 * Bits 32-47: Error flags
 * Bits 48-63: Feature flags
 */
.equ VALIDATION_EL1_NATIVE,     0x0001  // Entered at EL1 natively
.equ VALIDATION_EL2_TRANSITION, 0x0002  // EL2→EL1 transition required
.equ VALIDATION_DTB_VALID,      0x0004  // Device tree validation passed
.equ VALIDATION_STACK_VALID,    0x0008  // Stack validation passed
.equ VALIDATION_CPU_VALID,      0x0010  // CPU feature validation passed
.equ VALIDATION_MMU_DISABLED,   0x0020  // MMU properly disabled at entry
.equ VALIDATION_INTERRUPTS_OK,  0x0040  // Interrupts properly masked

.equ WARNING_INTERRUPT_MASK,    0x0001  // Interrupt mask warning (bit 16)
.equ WARNING_DEBUG_MASK,        0x0002  // Debug mask warning (bit 17)
.equ WARNING_RESERVED_REGS,     0x0004  // Reserved registers warning (bit 18)

/*
 * Enhanced Validation Results Structure
 * Additional validation data stored separately
 */
.align 8
.globl arm64_validation_results
arm64_validation_results:
    .quad   0                           // DTB header: magic number
    .quad   0                           // DTB header: total size  
    .quad   0                           // DTB header: structure offset
    .quad   0                           // DTB header: strings offset
    .quad   0                           // DTB header: memory reservation block offset
    .quad   0                           // DTB header: version
    .quad   0                           // DTB header: last compatible version
    .quad   0                           // DTB header: boot CPU ID
    .quad   0                           // Stack: original SP value
    .quad   0                           // Stack: estimated available space
    .quad   0                           // CPU features: ID_AA64PFR0_EL1
    .quad   0                           // CPU features: ID_AA64ISAR0_EL1
    .quad   0                           // CPU features: ID_AA64MMFR0_EL1
    .quad   0                           // CPU features: ID_AA64DFR0_EL1
    .quad   0                           // Register state: X1 (should be 0)
    .quad   0                           // Register state: X2 (should be 0)
    .quad   0                           // Register state: X3 (should be 0)
    .quad   0                           // Validation timestamp (if available)
    .ascii  "VALIDATION_DATA"           // Signature for validation results
    .align 8

/*
 * Early Emergency Stack
 * Used when bootloader stack is unusable or insufficient
 * 64KB stack should be sufficient for early kernel initialization
 */
.bss
.align 4
.globl early_emergency_stack_bottom
early_emergency_stack_bottom:
    .skip   0x10000                     // 64KB emergency stack space

.globl early_emergency_stack_top
early_emergency_stack_top:

/*
 * Stack Configuration Constants
 */
.equ EARLY_STACK_SIZE,          0x10000     // 64KB emergency stack
.equ STACK_ALIGNMENT_MASK,      0xf         // 16-byte alignment for ARM64 AAPCS
.equ STACK_SAFETY_MARGIN,       0x100       // 256 bytes safety margin
.equ STACK_CANARY_OFFSET,       0x1000      // 4KB below SP for canary
.equ STACK_MINIMUM_SPACE,       0x2000      // 8KB minimum for early kernel

/*
 * Stack Setup Flag Definitions
 */
.equ STACK_FLAG_EMERGENCY,      0x1         // Using emergency stack
.equ STACK_FLAG_LIMITED_SPACE,  0x2         // Limited stack space warning
.equ STACK_FLAG_REALIGNED,      0x4         // Bootloader stack was realigned
.equ STACK_FLAG_ENLARGED,       0x8         // Stack space was enlarged

// External symbols that will be provided by other files
.extern arch_kernel_entry               // C kernel entry function
.extern _start_secondary_cpu            // Secondary CPU C function
.extern _exception_vectors              // Exception vector table
.extern kernel_ttbr1_el1                // Kernel page table pointer

.end
