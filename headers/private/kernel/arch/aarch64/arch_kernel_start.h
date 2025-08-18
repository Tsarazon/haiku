/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Kernel Start Definitions
 */

#ifndef _ARCH_AARCH64_KERNEL_START_H
#define _ARCH_AARCH64_KERNEL_START_H

#include <SupportDefs.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ARM64 Boot Information Structure
 * Stores critical boot state for debugging and validation
 */
struct arm64_boot_info {
    uint64  dtb_physical_address;      // Device tree blob physical address
    uint64  original_exception_level;  // Exception level at kernel entry
    uint64  boot_flags;                // Boot validation flags
    uint64  panic_code;                // Panic code if boot failed
    char    signature[8];              // "ARM64BOOT" signature
    uint64  midr_el1;                  // Main ID register
    uint64  mpidr_el1;                 // Multiprocessor affinity register
    uint64  panic_message;             // Pointer to panic message (if any)
};

/*
 * ARM64 CPU Features Structure
 * Comprehensive CPU feature detection results
 */
struct arm64_crypto_features {
    bool    aes;                       // AES instructions available
    bool    sha1;                      // SHA1 instructions available
    bool    sha256;                    // SHA256 instructions available
    bool    sha512;                    // SHA512 instructions available
    bool    sha3;                      // SHA3 instructions available
    bool    sm3;                       // SM3 instructions available
    bool    sm4;                       // SM4 instructions available
    bool    pmull;                     // Polynomial multiply long available
};

struct arm64_pointer_auth_features {
    bool    address_auth;              // Address authentication available
    bool    generic_auth;              // Generic authentication available
    uint32  num_keys;                  // Number of pointer auth keys
};

struct arm64_cpu_features {
    bool    has_fp;                    // Floating point available
    bool    has_asimd;                 // Advanced SIMD available
    bool    has_sve;                   // Scalable Vector Extensions available
    bool    has_sve2;                  // SVE2 available
    bool    has_mte;                   // Memory Tagging Extensions available
    bool    has_bti;                   // Branch Target Identification available
    bool    has_paca;                  // Pointer Authentication (address) available
    bool    has_pacg;                  // Pointer Authentication (generic) available
    bool    has_dpb;                   // Data cache clean to Point of Persistence
    bool    has_dpb2;                  // Data cache clean to Point of Deep Persistence
    bool    has_lse;                   // Large System Extensions available
    bool    has_pan;                   // Privileged Access Never available
    bool    has_lor;                   // Limited Ordering Regions available
    bool    has_vh;                    // Virtualization Host Extensions available
};

/*
 * Complete ARM64 CPU Information Structure
 */
typedef struct arch_cpu_info {
    // Basic CPU identification
    uint64  midr;                      // Main ID register
    uint64  mpidr;                     // Multiprocessor affinity register  
    uint64  revidr;                    // Revision ID register
    uint64  aidr;                      // Auxiliary ID register
    
    // Cache hierarchy information
    struct {
        uint32  line_size;             // Cache line size in bytes
        uint32  sets;                  // Number of sets
        uint32  ways;                  // Number of ways
        uint32  size;                  // Total cache size
        uint32  type;                  // Cache type (I, D, unified)
    } cache_info[4];                   // L1I, L1D, L2, L3
    
    // Feature detection results
    struct arm64_crypto_features crypto_features;
    struct arm64_pointer_auth_features pauth_features;
    struct arm64_cpu_features features;
    
    // Memory management features
    struct {
        uint32  pa_range;              // Physical address range
        uint32  granule_4kb;           // 4KB granule support
        uint32  granule_16kb;          // 16KB granule support
        uint32  granule_64kb;          // 64KB granule support
        bool    has_ttbr1;             // TTBR1_EL1 available
        uint32  vmid_bits;             // VMID bits
        uint32  asid_bits;             // ASID bits
    } mmu_features;
    
    // Debug and performance features
    struct {
        uint32  num_breakpoints;       // Number of breakpoints
        uint32  num_watchpoints;       // Number of watchpoints
        uint32  num_pmu_counters;      // Number of PMU counters
        bool    has_debug_v8;          // Debug architecture v8
        bool    has_spe;               // Statistical Profiling Extension
        bool    has_trace;             // Trace support
    } debug_features;
    
    // Virtualization features
    struct {
        bool    has_el2;               // EL2 implemented
        bool    has_el3;               // EL3 implemented
        bool    has_vh;                // Virtualization Host Extensions
        bool    has_vmid16;            // 16-bit VMID support
        uint32  ipa_range;             // Intermediate physical address range
    } virt_features;
    
} arch_cpu_info;

/*
 * Boot Entry Point Functions
 */

// Main kernel entry (called from assembly)
void _start_kernel_main(kernel_args* kernelArgs, int currentCPU);

// Secondary CPU entry (called from assembly)
void _start_secondary_cpu(int cpuId);

// Debug panic for early boot failures
void arch_debug_panic(const char* message) __attribute__((noreturn));

/*
 * Register State Validation Functions
 */

// Validate kernel entry state
status_t arm64_validate_entry_state(kernel_args* args);

// Validate exception level configuration
status_t arm64_validate_exception_level(uint64 currentEL);

// Validate memory configuration
status_t arm64_validate_memory_layout(kernel_args* args);

// Validate device tree blob
status_t arm64_validate_device_tree(void* fdt);

/*
 * CPU Feature Detection Functions
 */

// Detect all CPU features
void arm64_detect_cpu_features(arch_cpu_info* cpu_info);

// Detect crypto capabilities
void arm64_detect_crypto_features(struct arm64_crypto_features* crypto);

// Detect pointer authentication features
void arm64_detect_pauth_features(struct arm64_pointer_auth_features* pauth);

// Detect memory management features
void arm64_detect_mmu_features(arch_cpu_info* cpu_info);

// Detect debug and performance features
void arm64_detect_debug_features(arch_cpu_info* cpu_info);

/*
 * System Register Access Functions
 */

// Read CPU identification registers
uint64 arm64_read_midr_el1(void);
uint64 arm64_read_mpidr_el1(void);
uint64 arm64_read_revidr_el1(void);
uint64 arm64_read_aidr_el1(void);

// Read feature registers
uint64 arm64_read_id_aa64pfr0_el1(void);
uint64 arm64_read_id_aa64pfr1_el1(void);
uint64 arm64_read_id_aa64isar0_el1(void);
uint64 arm64_read_id_aa64isar1_el1(void);
uint64 arm64_read_id_aa64mmfr0_el1(void);
uint64 arm64_read_id_aa64mmfr1_el1(void);
uint64 arm64_read_id_aa64mmfr2_el1(void);
uint64 arm64_read_id_aa64dfr0_el1(void);
uint64 arm64_read_id_aa64dfr1_el1(void);

// Read cache information registers
uint64 arm64_read_ctr_el0(void);
uint64 arm64_read_ccsidr_el1(void);
uint64 arm64_read_clidr_el1(void);

/*
 * Exception Level Management Functions
 */

// Get current exception level
uint64 arm64_get_current_el(void);

// Check if running in hypervisor mode
bool arm64_in_hypervisor_mode(void);

// Transition from EL2 to EL1
status_t arm64_transition_el2_to_el1(void);

// Configure EL1 execution environment
status_t arm64_setup_el1_environment(void);

/*
 * Memory Management Setup Functions
 */

// Set up initial page tables
status_t arm64_setup_initial_page_tables(kernel_args* args);

// Configure memory attributes
status_t arm64_setup_memory_attributes(void);

// Enable MMU
status_t arm64_enable_mmu(void);

/*
 * Boot Validation Constants
 */

// Magic numbers for validation
#define ARM64_BOOT_MAGIC_DTB        0xd00dfeed  // Device tree magic
#define ARM64_BOOT_MAGIC_KERNEL     0x644d5241  // "ARMd" in little endian

// Boot validation flags
#define ARM64_BOOT_FLAG_EL2_ENTRY   (1UL << 0)  // Entered at EL2
#define ARM64_BOOT_FLAG_EL1_ENTRY   (1UL << 1)  // Entered at EL1
#define ARM64_BOOT_FLAG_DTB_VALID   (1UL << 2)  // Device tree valid
#define ARM64_BOOT_FLAG_MMU_OFF     (1UL << 3)  // MMU disabled at entry
#define ARM64_BOOT_FLAG_IRQ_MASKED  (1UL << 4)  // Interrupts masked at entry
#define ARM64_BOOT_FLAG_STACK_OK    (1UL << 5)  // Stack properly aligned

// Boot warning flags
#define ARM64_BOOT_WARN_IRQ_MASK    (1UL << 16) // Interrupt mask warning
#define ARM64_BOOT_WARN_EL_UNKNOWN  (1UL << 17) // Unknown exception level
#define ARM64_BOOT_WARN_DTB_MISSING (1UL << 18) // Device tree missing
#define ARM64_BOOT_WARN_STACK_ALIGN (1UL << 19) // Stack alignment warning

// Panic codes for early boot failures
#define ARM64_PANIC_EL_INVALID      0xDEADEL00  // Invalid exception level
#define ARM64_PANIC_DTB_ALIGN       0xDEADDTB1  // DTB alignment error
#define ARM64_PANIC_DTB_NULL        0xDEADDTB2  // DTB null pointer
#define ARM64_PANIC_DTB_MAGIC       0xDEADDTB3  // DTB magic validation failed
#define ARM64_PANIC_STACK_ALIGN     0xDEADSTK1  // Stack alignment error
#define ARM64_PANIC_EL1_TRANS       0xDEADEL1F  // EL1 transition failed
#define ARM64_PANIC_KERNEL_RET      0xDEADKRET  // Kernel returned unexpectedly

// Register validation masks
#define ARM64_DAIF_ALL_MASKED       0xF0        // All interrupts masked
#define ARM64_CURRENTEL_MASK        0x0C        // Exception level mask
#define ARM64_ALIGNMENT_MASK_8      0x07        // 8-byte alignment mask
#define ARM64_ALIGNMENT_MASK_16     0x0F        // 16-byte alignment mask

/*
 * Global Boot Information
 * Accessible for debugging and diagnostics
 */
extern struct arm64_boot_info arm64_boot_info;

#ifdef __cplusplus
}
#endif

#endif /* _ARCH_AARCH64_KERNEL_START_H */