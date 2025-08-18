/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Exception Level Management Header
 */

#ifndef _KERNEL_ARCH_AARCH64_ARCH_EXCEPTIONS_H
#define _KERNEL_ARCH_AARCH64_ARCH_EXCEPTIONS_H

#include <SupportDefs.h>

// ARM64 Exception Level Constants
#define ARM64_EL0    0
#define ARM64_EL1    1
#define ARM64_EL2    2
#define ARM64_EL3    3

// Exception level information structure
struct arm64_exception_level_info {
    uint32_t current_el;      // Current exception level
    uint32_t target_el;       // Target exception level (usually EL1)
    bool el2_present;         // Whether EL2 is implemented
    bool el3_present;         // Whether EL3 is implemented
    uint64_t sctlr_el1;      // System Control Register EL1
    uint64_t hcr_el2;        // Hypervisor Configuration Register
    uint64_t scr_el3;        // Secure Configuration Register
};

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Exception Level Detection and Management
 */

// Get current exception level (0-3)
uint32_t arch_get_current_exception_level(void);

// Check if a specific exception level is available
bool arch_exception_level_available(uint32_t exception_level);

// Detect and initialize exception level information
status_t arch_detect_exception_levels(void);

// Get current exception level information
status_t arch_get_exception_level_info(struct arm64_exception_level_info* info);

/*
 * EL1 System Register Configuration
 */

// Initialize EL1 system registers for kernel operation
status_t arch_configure_el1_system_registers(void);

// Enable MMU and caches at EL1 (called after memory management setup)
status_t arch_enable_el1_mmu_caches(void);

// Enable/disable translation table walks for TTBR0 and TTBR1
status_t arch_enable_el1_translation_tables(bool enable_ttbr0, bool enable_ttbr1);

// Set exception vector base address (must be 2KB aligned)
status_t arch_set_el1_exception_vector_base(uint64_t vector_base);

// Configure Top Byte Ignore for tagged addressing
status_t arch_configure_el1_top_byte_ignore(bool enable_ttbr0_tbi, bool enable_ttbr1_tbi);

/*
 * Exception Level Transitions
 */

// Transition from EL2 to EL1 (configures EL2 registers)
status_t arch_transition_el2_to_el1(void);

/*
 * Debug and Diagnostic Functions
 */

// Dump current EL1 system register state
void arch_dump_el1_registers(void);

#ifdef __cplusplus
}
#endif

#endif /* _KERNEL_ARCH_AARCH64_ARCH_EXCEPTIONS_H */