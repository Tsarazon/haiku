/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Exception Level Management
 * 
 * This module provides functions to detect and manage ARM64 exception levels,
 * with particular focus on proper EL1 system register configuration for
 * kernel operation.
 *
 * Authors:
 *   ARM64 Kernel Development Team
 */

#include <OS.h>

#include <arch_cpu.h>
#include <debug.h>

#include <stdio.h>
#include <string.h>

// ARM64 Exception Level Definitions
#define ARM64_EL0    0
#define ARM64_EL1    1
#define ARM64_EL2    2
#define ARM64_EL3    3

// System Register Bit Definitions
#define SCTLR_EL1_M       (1UL << 0)   // MMU enable
#define SCTLR_EL1_A       (1UL << 1)   // Alignment check enable
#define SCTLR_EL1_C       (1UL << 2)   // Data cache enable
#define SCTLR_EL1_SA      (1UL << 3)   // Stack alignment check
#define SCTLR_EL1_SA0     (1UL << 4)   // Stack alignment check for EL0
#define SCTLR_EL1_CP15BEN (1UL << 5)   // CP15 barrier enable
#define SCTLR_EL1_ITD     (1UL << 7)   // IT disable
#define SCTLR_EL1_SED     (1UL << 8)   // SETEND disable
#define SCTLR_EL1_UMA     (1UL << 9)   // User mask access
#define SCTLR_EL1_I       (1UL << 12)  // Instruction cache enable
#define SCTLR_EL1_DZE     (1UL << 14)  // DZ enable
#define SCTLR_EL1_UCT     (1UL << 15)  // User cache type register access
#define SCTLR_EL1_nTWI    (1UL << 16)  // Not trap WFI
#define SCTLR_EL1_nTWE    (1UL << 18)  // Not trap WFE
#define SCTLR_EL1_WXN     (1UL << 19)  // Write permission implies XN
#define SCTLR_EL1_E0E     (1UL << 24)  // EL0 endianness
#define SCTLR_EL1_EE      (1UL << 25)  // EL1 endianness
#define SCTLR_EL1_UCI     (1UL << 26)  // User cache instructions
#define SCTLR_EL1_EnDA    (1UL << 27)  // Enable pointer authentication (data, A key)
#define SCTLR_EL1_EnDB    (1UL << 30)  // Enable pointer authentication (data, B key)
#define SCTLR_EL1_EnIA    (1UL << 31)  // Enable pointer authentication (instruction, A key)

// HCR_EL2 bit definitions
#define HCR_EL2_RW        (1UL << 31)  // Register width control
#define HCR_EL2_ID        (1UL << 33)  // Stage 2 Instruction access Disable

// CPACR_EL1 bit definitions
#define CPACR_EL1_FPEN_SHIFT  20
#define CPACR_EL1_FPEN_MASK   (3UL << CPACR_EL1_FPEN_SHIFT)
#define CPACR_EL1_FPEN_NONE   (0UL << CPACR_EL1_FPEN_SHIFT)  // No access
#define CPACR_EL1_FPEN_EL1    (1UL << CPACR_EL1_FPEN_SHIFT)  // EL1 access only
#define CPACR_EL1_FPEN_FULL   (3UL << CPACR_EL1_FPEN_SHIFT)  // Full access

// TCR_EL1 bit definitions (Translation Control Register)
#define TCR_EL1_T0SZ_SHIFT    0
#define TCR_EL1_T0SZ_MASK     (0x3FUL << TCR_EL1_T0SZ_SHIFT)
#define TCR_EL1_EPD0          (1UL << 7)   // Translation table walk disable for TTBR0_EL1
#define TCR_EL1_IRGN0_SHIFT   8
#define TCR_EL1_IRGN0_MASK    (3UL << TCR_EL1_IRGN0_SHIFT)
#define TCR_EL1_IRGN0_NC      (0UL << TCR_EL1_IRGN0_SHIFT)  // Non-cacheable
#define TCR_EL1_IRGN0_WBWA    (1UL << TCR_EL1_IRGN0_SHIFT)  // Write-Back Write-Allocate
#define TCR_EL1_IRGN0_WT      (2UL << TCR_EL1_IRGN0_SHIFT)  // Write-Through
#define TCR_EL1_IRGN0_WB      (3UL << TCR_EL1_IRGN0_SHIFT)  // Write-Back
#define TCR_EL1_ORGN0_SHIFT   10
#define TCR_EL1_ORGN0_MASK    (3UL << TCR_EL1_ORGN0_SHIFT)
#define TCR_EL1_ORGN0_NC      (0UL << TCR_EL1_ORGN0_SHIFT)  // Non-cacheable
#define TCR_EL1_ORGN0_WBWA    (1UL << TCR_EL1_ORGN0_SHIFT)  // Write-Back Write-Allocate
#define TCR_EL1_ORGN0_WT      (2UL << TCR_EL1_ORGN0_SHIFT)  // Write-Through
#define TCR_EL1_ORGN0_WB      (3UL << TCR_EL1_ORGN0_SHIFT)  // Write-Back
#define TCR_EL1_SH0_SHIFT     12
#define TCR_EL1_SH0_MASK      (3UL << TCR_EL1_SH0_SHIFT)
#define TCR_EL1_SH0_NS        (0UL << TCR_EL1_SH0_SHIFT)    // Non-shareable
#define TCR_EL1_SH0_OS        (2UL << TCR_EL1_SH0_SHIFT)    // Outer shareable
#define TCR_EL1_SH0_IS        (3UL << TCR_EL1_SH0_SHIFT)    // Inner shareable
#define TCR_EL1_TG0_SHIFT     14
#define TCR_EL1_TG0_MASK      (3UL << TCR_EL1_TG0_SHIFT)
#define TCR_EL1_TG0_4K        (0UL << TCR_EL1_TG0_SHIFT)    // 4KB granule
#define TCR_EL1_TG0_64K       (1UL << TCR_EL1_TG0_SHIFT)    // 64KB granule
#define TCR_EL1_TG0_16K       (2UL << TCR_EL1_TG0_SHIFT)    // 16KB granule
#define TCR_EL1_T1SZ_SHIFT    16
#define TCR_EL1_T1SZ_MASK     (0x3FUL << TCR_EL1_T1SZ_SHIFT)
#define TCR_EL1_A1            (1UL << 22)   // ASID selection
#define TCR_EL1_EPD1          (1UL << 23)   // Translation table walk disable for TTBR1_EL1
#define TCR_EL1_IRGN1_SHIFT   24
#define TCR_EL1_IRGN1_MASK    (3UL << TCR_EL1_IRGN1_SHIFT)
#define TCR_EL1_IRGN1_NC      (0UL << TCR_EL1_IRGN1_SHIFT)
#define TCR_EL1_IRGN1_WBWA    (1UL << TCR_EL1_IRGN1_SHIFT)
#define TCR_EL1_IRGN1_WT      (2UL << TCR_EL1_IRGN1_SHIFT)
#define TCR_EL1_IRGN1_WB      (3UL << TCR_EL1_IRGN1_SHIFT)
#define TCR_EL1_ORGN1_SHIFT   26
#define TCR_EL1_ORGN1_MASK    (3UL << TCR_EL1_ORGN1_SHIFT)
#define TCR_EL1_ORGN1_NC      (0UL << TCR_EL1_ORGN1_SHIFT)
#define TCR_EL1_ORGN1_WBWA    (1UL << TCR_EL1_ORGN1_SHIFT)
#define TCR_EL1_ORGN1_WT      (2UL << TCR_EL1_ORGN1_SHIFT)
#define TCR_EL1_ORGN1_WB      (3UL << TCR_EL1_ORGN1_SHIFT)
#define TCR_EL1_SH1_SHIFT     28
#define TCR_EL1_SH1_MASK      (3UL << TCR_EL1_SH1_SHIFT)
#define TCR_EL1_SH1_NS        (0UL << TCR_EL1_SH1_SHIFT)
#define TCR_EL1_SH1_OS        (2UL << TCR_EL1_SH1_SHIFT)
#define TCR_EL1_SH1_IS        (3UL << TCR_EL1_SH1_SHIFT)
#define TCR_EL1_TG1_SHIFT     30
#define TCR_EL1_TG1_MASK      (3UL << TCR_EL1_TG1_SHIFT)
#define TCR_EL1_TG1_16K       (1UL << TCR_EL1_TG1_SHIFT)
#define TCR_EL1_TG1_4K        (2UL << TCR_EL1_TG1_SHIFT)
#define TCR_EL1_TG1_64K       (3UL << TCR_EL1_TG1_SHIFT)
#define TCR_EL1_IPS_SHIFT     32
#define TCR_EL1_IPS_MASK      (7UL << TCR_EL1_IPS_SHIFT)
#define TCR_EL1_IPS_32BIT     (0UL << TCR_EL1_IPS_SHIFT)    // 32 bits, 4GB
#define TCR_EL1_IPS_36BIT     (1UL << TCR_EL1_IPS_SHIFT)    // 36 bits, 64GB
#define TCR_EL1_IPS_40BIT     (2UL << TCR_EL1_IPS_SHIFT)    // 40 bits, 1TB
#define TCR_EL1_IPS_42BIT     (3UL << TCR_EL1_IPS_SHIFT)    // 42 bits, 4TB
#define TCR_EL1_IPS_44BIT     (4UL << TCR_EL1_IPS_SHIFT)    // 44 bits, 16TB
#define TCR_EL1_IPS_48BIT     (5UL << TCR_EL1_IPS_SHIFT)    // 48 bits, 256TB
#define TCR_EL1_IPS_52BIT     (6UL << TCR_EL1_IPS_SHIFT)    // 52 bits, 4PB
#define TCR_EL1_AS            (1UL << 36)   // ASID Size (1 = 16 bit, 0 = 8 bit)
#define TCR_EL1_TBI0          (1UL << 37)   // Top Byte Ignore for TTBR0_EL1
#define TCR_EL1_TBI1          (1UL << 38)   // Top Byte Ignore for TTBR1_EL1

// ESR_EL1 bit definitions (Exception Syndrome Register)
#define ESR_EL1_EC_SHIFT      26
#define ESR_EL1_EC_MASK       (0x3FUL << ESR_EL1_EC_SHIFT)

// VBAR_EL1 alignment requirement
#define VBAR_EL1_ALIGNMENT    2048  // Must be 2KB aligned

// Additional system register bit definitions
#define CONTEXTIDR_EL1_ASID_SHIFT  0
#define CONTEXTIDR_EL1_ASID_MASK   (0xFFFFUL << CONTEXTIDR_EL1_ASID_SHIFT)
#define CONTEXTIDR_EL1_PROCID_SHIFT 0
#define CONTEXTIDR_EL1_PROCID_MASK  (0xFFFFFFFFUL << CONTEXTIDR_EL1_PROCID_SHIFT)

// Exception level information structure
struct arm64_exception_level_info {
    uint32_t current_el;
    uint32_t target_el;
    bool el2_present;
    bool el3_present;
    uint64_t sctlr_el1;
    uint64_t hcr_el2;
    uint64_t scr_el3;
};

static struct arm64_exception_level_info s_el_info;

// Forward declarations
static void configure_el1_system_registers(void);
static void configure_el1_memory_attributes(void);
static void configure_el1_floating_point(void);
static void configure_el1_security_features(void);
static void configure_el1_translation_control(void);
static void configure_el1_exception_handling(void);
static void configure_el1_context_registers(void);
static void initialize_el1_safe_defaults(void);
static uint64_t detect_physical_address_size(void);
static status_t validate_el1_configuration(void);
static status_t validate_system_register_values(void);

/*
 * Get current exception level
 * Returns the current exception level (0-3)
 */
extern "C" uint32_t
arch_get_current_exception_level(void)
{
    uint64_t current_el;
    
    // Read CurrentEL register
    asm volatile("mrs %0, CurrentEL" : "=r" (current_el));
    
    // Extract exception level (bits 3:2)
    uint32_t el = (current_el >> 2) & 0x3;
    
    return el;
}

/*
 * Check if a specific exception level is available
 * Returns true if the specified exception level is implemented
 */
extern "C" bool
arch_exception_level_available(uint32_t exception_level)
{
    uint64_t pfr0;
    
    if (exception_level > ARM64_EL3)
        return false;
    
    // Read processor feature register 0
    asm volatile("mrs %0, ID_AA64PFR0_EL1" : "=r" (pfr0));
    
    switch (exception_level) {
        case ARM64_EL0:
            // EL0 is always available if we're running
            return ((pfr0 & 0xF) == 2); // AArch64 support
            
        case ARM64_EL1:
            // EL1 is always available if we're running
            return (((pfr0 >> 4) & 0xF) == 2); // AArch64 support
            
        case ARM64_EL2:
            // Check if EL2 is implemented
            return (((pfr0 >> 8) & 0xF) == 2); // AArch64 support
            
        case ARM64_EL3:
            // Check if EL3 is implemented
            return (((pfr0 >> 12) & 0xF) == 2); // AArch64 support
    }
    
    return false;
}

/*
 * Detect and initialize exception level information
 * This function analyzes the current system configuration and
 * prepares for proper EL1 operation
 */
extern "C" status_t
arch_detect_exception_levels(void)
{
    memset(&s_el_info, 0, sizeof(s_el_info));
    
    // Get current exception level
    s_el_info.current_el = arch_get_current_exception_level();
    s_el_info.target_el = ARM64_EL1; // Kernel always runs at EL1
    
    dprintf("ARM64: Current exception level: EL%u\n", s_el_info.current_el);
    
    // Detect available exception levels
    s_el_info.el2_present = arch_exception_level_available(ARM64_EL2);
    s_el_info.el3_present = arch_exception_level_available(ARM64_EL3);
    
    dprintf("ARM64: EL2 %s, EL3 %s\n",
        s_el_info.el2_present ? "present" : "not present",
        s_el_info.el3_present ? "present" : "not present");
    
    // Validate current state
    if (s_el_info.current_el == ARM64_EL0) {
        panic("ARM64: Kernel cannot run at EL0");
        return B_ERROR;
    }
    
    // Read current system register states for analysis
    if (s_el_info.current_el >= ARM64_EL1) {
        asm volatile("mrs %0, SCTLR_EL1" : "=r" (s_el_info.sctlr_el1));
    }
    
    if (s_el_info.current_el >= ARM64_EL2 && s_el_info.el2_present) {
        asm volatile("mrs %0, HCR_EL2" : "=r" (s_el_info.hcr_el2));
    }
    
    if (s_el_info.current_el >= ARM64_EL3 && s_el_info.el3_present) {
        asm volatile("mrs %0, SCR_EL3" : "=r" (s_el_info.scr_el3));
    }
    
    return B_OK;
}

/*
 * Configure EL1 system registers for kernel operation
 * Sets up SCTLR_EL1 and other essential system registers
 */
static void
configure_el1_system_registers(void)
{
    uint64_t sctlr_el1 = 0;
    
    // Set required reserved bits (RES1) for SCTLR_EL1
    // These bits must be 1 according to ARM architecture reference
    sctlr_el1 |= (1UL << 11); // RES1
    sctlr_el1 |= (1UL << 20); // RES1
    sctlr_el1 |= (1UL << 22); // RES1
    sctlr_el1 |= (1UL << 23); // RES1
    sctlr_el1 |= (1UL << 28); // RES1
    sctlr_el1 |= (1UL << 29); // RES1
    
    // Configure essential control bits
    sctlr_el1 |= SCTLR_EL1_SA;      // Stack alignment check enable
    sctlr_el1 |= SCTLR_EL1_SA0;     // Stack alignment check for EL0
    sctlr_el1 |= SCTLR_EL1_nTWI;    // Don't trap WFI instructions
    sctlr_el1 |= SCTLR_EL1_nTWE;    // Don't trap WFE instructions
    sctlr_el1 |= SCTLR_EL1_DZE;     // Enable DC ZVA instruction at EL0
    sctlr_el1 |= SCTLR_EL1_UCT;     // EL0 access to CTR_EL0
    sctlr_el1 |= SCTLR_EL1_UCI;     // EL0 access to DC CVAU, DC CIVAC, DC CVAC, IC IVAU
    
    // Leave MMU and caches disabled for now - they'll be enabled later
    // during memory management initialization
    // sctlr_el1 |= SCTLR_EL1_M;    // MMU enable (later)
    // sctlr_el1 |= SCTLR_EL1_C;    // Data cache enable (later)
    // sctlr_el1 |= SCTLR_EL1_I;    // Instruction cache enable (later)
    
    dprintf("ARM64: Configuring SCTLR_EL1 = 0x%016lx\n", sctlr_el1);
    
    // Write SCTLR_EL1
    asm volatile(
        "msr SCTLR_EL1, %0\n"
        "isb\n"
        :
        : "r" (sctlr_el1)
        : "memory"
    );
    
    s_el_info.sctlr_el1 = sctlr_el1;
}

/*
 * Configure EL1 memory attribute registers
 * Sets up MAIR_EL1 for proper memory type handling
 */
static void
configure_el1_memory_attributes(void)
{
    uint64_t mair_el1 = 0;
    
    // Configure memory attribute indirection register
    // This defines the memory types that can be used in page tables
    
    // Attr0: Device-nGnRnE (strongly ordered)
    mair_el1 |= (0x00UL << 0);
    
    // Attr1: Device-nGnRE (device memory)
    mair_el1 |= (0x04UL << 8);
    
    // Attr2: Device-GRE (device memory with gather/reorder)
    mair_el1 |= (0x0CUL << 16);
    
    // Attr3: Normal memory, Inner/Outer Non-cacheable
    mair_el1 |= (0x44UL << 24);
    
    // Attr4: Normal memory, Inner/Outer Write-through Cacheable
    mair_el1 |= (0xAAUL << 32);
    
    // Attr5: Normal memory, Inner/Outer Write-back Cacheable
    mair_el1 |= (0xEEUL << 40);
    
    // Attr6: Normal memory, Inner Write-back Cacheable, Outer Non-cacheable
    mair_el1 |= (0x4EUL << 48);
    
    // Attr7: Normal memory, Inner Non-cacheable, Outer Write-back Cacheable
    mair_el1 |= (0xE4UL << 56);
    
    dprintf("ARM64: Configuring MAIR_EL1 = 0x%016lx\n", mair_el1);
    
    asm volatile(
        "msr MAIR_EL1, %0\n"
        "isb\n"
        :
        : "r" (mair_el1)
        : "memory"
    );
}

/*
 * Configure floating point and SIMD access
 * Enables FP/SIMD access at EL1 and EL0
 */
static void
configure_el1_floating_point(void)
{
    uint64_t cpacr_el1;
    
    // Read current CPACR_EL1
    asm volatile("mrs %0, CPACR_EL1" : "=r" (cpacr_el1));
    
    // Enable full FP/SIMD access at EL1 and EL0
    cpacr_el1 &= ~CPACR_EL1_FPEN_MASK;
    cpacr_el1 |= CPACR_EL1_FPEN_FULL;
    
    dprintf("ARM64: Configuring CPACR_EL1 = 0x%016lx\n", cpacr_el1);
    
    asm volatile(
        "msr CPACR_EL1, %0\n"
        "isb\n"
        :
        : "r" (cpacr_el1)
        : "memory"
    );
}

/*
 * Configure security and pointer authentication features
 * Enables available security features for EL1 operation
 */
static void
configure_el1_security_features(void)
{
    uint64_t id_aa64isar1_el1;
    uint64_t sctlr_el1;
    
    // Read instruction set attribute register to check for pointer auth support
    asm volatile("mrs %0, ID_AA64ISAR1_EL1" : "=r" (id_aa64isar1_el1));
    
    // Check for pointer authentication support
    bool pointer_auth_available = false;
    uint64_t apa_field = (id_aa64isar1_el1 >> 4) & 0xF;   // APA (Address auth, PAuth instruction)
    uint64_t api_field = (id_aa64isar1_el1 >> 8) & 0xF;   // API (Address auth, QARMA algorithm)
    uint64_t gpa_field = (id_aa64isar1_el1 >> 24) & 0xF;  // GPA (Generic auth, PAuth instruction)
    uint64_t gpi_field = (id_aa64isar1_el1 >> 28) & 0xF;  // GPI (Generic auth, QARMA algorithm)
    
    if (apa_field || api_field || gpa_field || gpi_field) {
        pointer_auth_available = true;
        dprintf("ARM64: Pointer authentication available\n");
        
        // Read current SCTLR_EL1
        asm volatile("mrs %0, SCTLR_EL1" : "=r" (sctlr_el1));
        
        // Enable pointer authentication if available
        if (apa_field || api_field) {
            sctlr_el1 |= SCTLR_EL1_EnIA;  // Enable instruction pointer auth
            sctlr_el1 |= SCTLR_EL1_EnDA;  // Enable data pointer auth (A key)
        }
        if (gpa_field || gpi_field) {
            sctlr_el1 |= SCTLR_EL1_EnDB;  // Enable data pointer auth (B key)
        }
        
        dprintf("ARM64: Updated SCTLR_EL1 = 0x%016lx (with pointer auth)\n", sctlr_el1);
        
        asm volatile(
            "msr SCTLR_EL1, %0\n"
            "isb\n"
            :
            : "r" (sctlr_el1)
            : "memory"
        );
        
        s_el_info.sctlr_el1 = sctlr_el1;
    } else {
        dprintf("ARM64: Pointer authentication not available\n");
    }
}

/*
 * Detect physical address size supported by the system
 * Returns the intermediate physical address size field for TCR_EL1
 */
static uint64_t
detect_physical_address_size(void)
{
    uint64_t id_aa64mmfr0_el1;
    
    // Read memory model feature register
    asm volatile("mrs %0, ID_AA64MMFR0_EL1" : "=r" (id_aa64mmfr0_el1));
    
    // Extract PARange field (bits 3:0)
    uint64_t pa_range = id_aa64mmfr0_el1 & 0xF;
    
    // Convert PARange to IPS field value for TCR_EL1
    switch (pa_range) {
        case 0: return TCR_EL1_IPS_32BIT;  // 32 bits, 4GB
        case 1: return TCR_EL1_IPS_36BIT;  // 36 bits, 64GB
        case 2: return TCR_EL1_IPS_40BIT;  // 40 bits, 1TB
        case 3: return TCR_EL1_IPS_42BIT;  // 42 bits, 4TB
        case 4: return TCR_EL1_IPS_44BIT;  // 44 bits, 16TB
        case 5: return TCR_EL1_IPS_48BIT;  // 48 bits, 256TB
        case 6: return TCR_EL1_IPS_52BIT;  // 52 bits, 4PB
        default:
            dprintf("ARM64: Unknown PARange %lu, using 48-bit default\n", pa_range);
            return TCR_EL1_IPS_48BIT;
    }
}

/*
 * Configure EL1 translation control register (TCR_EL1)
 * Sets up virtual memory translation parameters
 */
static void
configure_el1_translation_control(void)
{
    uint64_t tcr_el1 = 0;
    
    dprintf("ARM64: Configuring TCR_EL1 (Translation Control)\n");
    
    // Configure TTBR0_EL1 (user space) parameters
    // T0SZ: Virtual address size for TTBR0_EL1
    // For 48-bit VA space: T0SZ = 64 - 48 = 16
    uint64_t t0sz = 16;  // 48-bit virtual address space
    tcr_el1 |= (t0sz << TCR_EL1_T0SZ_SHIFT) & TCR_EL1_T0SZ_MASK;
    
    // Inner cacheability for TTBR0 table walks: Write-Back Write-Allocate
    tcr_el1 |= TCR_EL1_IRGN0_WBWA;
    
    // Outer cacheability for TTBR0 table walks: Write-Back Write-Allocate  
    tcr_el1 |= TCR_EL1_ORGN0_WBWA;
    
    // Shareability for TTBR0: Inner Shareable
    tcr_el1 |= TCR_EL1_SH0_IS;
    
    // Translation granule for TTBR0: 4KB
    tcr_el1 |= TCR_EL1_TG0_4K;
    
    // Configure TTBR1_EL1 (kernel space) parameters
    // T1SZ: Virtual address size for TTBR1_EL1
    uint64_t t1sz = 16;  // 48-bit virtual address space
    tcr_el1 |= (t1sz << TCR_EL1_T1SZ_SHIFT) & TCR_EL1_T1SZ_MASK;
    
    // Inner cacheability for TTBR1 table walks: Write-Back Write-Allocate
    tcr_el1 |= TCR_EL1_IRGN1_WBWA;
    
    // Outer cacheability for TTBR1 table walks: Write-Back Write-Allocate
    tcr_el1 |= TCR_EL1_ORGN1_WBWA;
    
    // Shareability for TTBR1: Inner Shareable
    tcr_el1 |= TCR_EL1_SH1_IS;
    
    // Translation granule for TTBR1: 4KB
    tcr_el1 |= TCR_EL1_TG1_4K;
    
    // Intermediate physical address size
    tcr_el1 |= detect_physical_address_size();
    
    // ASID size: 16-bit ASIDs for better isolation
    tcr_el1 |= TCR_EL1_AS;
    
    // Top Byte Ignore: disabled for both TTBR0 and TTBR1
    // (can be enabled later for userspace if needed)
    // tcr_el1 |= TCR_EL1_TBI0;
    // tcr_el1 |= TCR_EL1_TBI1;
    
    // Initially disable both translation tables - they'll be enabled
    // when page tables are set up
    tcr_el1 |= TCR_EL1_EPD0;  // Disable TTBR0_EL1 walks initially
    tcr_el1 |= TCR_EL1_EPD1;  // Disable TTBR1_EL1 walks initially
    
    dprintf("ARM64: Setting TCR_EL1 = 0x%016lx\n", tcr_el1);
    
    asm volatile(
        "msr TCR_EL1, %0\n"
        "isb\n"
        :
        : "r" (tcr_el1)
        : "memory"
    );
}

/*
 * Configure EL1 exception handling registers
 * Sets up exception vector and syndrome registers
 */
static void
configure_el1_exception_handling(void)
{
    dprintf("ARM64: Configuring EL1 exception handling\n");
    
    // Clear exception syndrome register
    asm volatile("msr ESR_EL1, xzr");
    
    // Clear fault address register  
    asm volatile("msr FAR_EL1, xzr");
    
    // Note: VBAR_EL1 (Vector Base Address Register) will be set up
    // later when the actual exception vector table is available
    // It must be 2KB aligned
    
    // Clear any pending exceptions
    asm volatile("msr ELR_EL1, xzr");
    asm volatile("msr SPSR_EL1, xzr");
    
    dprintf("ARM64: Exception handling registers initialized\n");
}

/*
 * Configure EL1 context and identification registers
 * Sets up process context and thread identification
 */
static void
configure_el1_context_registers(void)
{
    dprintf("ARM64: Configuring EL1 context registers\n");
    
    // Clear context ID register (will be set by thread management)
    asm volatile("msr CONTEXTIDR_EL1, xzr");
    
    // Clear thread pointer register (will be set by thread management)
    asm volatile("msr TPIDR_EL1, xzr");
    
    // Clear user read-only thread pointer (will be set by userspace)
    asm volatile("msr TPIDRRO_EL0, xzr");
    
    // Clear user read-write thread pointer (will be set by userspace)
    asm volatile("msr TPIDR_EL0, xzr");
    
    dprintf("ARM64: Context registers cleared\n");
}

/*
 * Initialize EL1 registers with safe default values
 * This ensures all registers start in a known, safe state
 */
static void
initialize_el1_safe_defaults(void)
{
    dprintf("ARM64: Initializing EL1 safe defaults\n");
    
    // Clear translation table base registers
    asm volatile("msr TTBR0_EL1, xzr");
    asm volatile("msr TTBR1_EL1, xzr");
    
    // Clear saved program status register
    asm volatile("msr SPSR_EL1, xzr");
    
    // Clear exception link register
    asm volatile("msr ELR_EL1, xzr");
    
    // Clear stack pointer for EL0
    asm volatile("msr SP_EL0, xzr");
    
    // Clear performance monitoring registers if accessible
    // Note: These might trap if performance monitoring is not implemented
    // or not accessible at EL1
    uint64_t id_aa64dfr0_el1;
    asm volatile("mrs %0, ID_AA64DFR0_EL1" : "=r" (id_aa64dfr0_el1));
    
    // Check if PMU is implemented (PMUVer field, bits 11:8)
    uint64_t pmu_ver = (id_aa64dfr0_el1 >> 8) & 0xF;
    if (pmu_ver != 0 && pmu_ver != 0xF) {
        // PMU is implemented, initialize basic registers
        dprintf("ARM64: PMU version %lu detected, initializing PMU registers\n", pmu_ver);
        
        // These might be trapped, so we should check PMCR_EL0 accessibility
        // For now, just clear what we can safely access
        asm volatile("msr PMCR_EL0, xzr" ::: "memory");
        asm volatile("msr PMCNTENSET_EL0, xzr" ::: "memory");
        asm volatile("msr PMINTENSET_EL1, xzr" ::: "memory");
    }
    
    dprintf("ARM64: Safe defaults initialized\n");
}

/*
 * Validate system register values after configuration
 * Performs comprehensive validation of all configured registers
 */
static status_t
validate_system_register_values(void)
{
    uint64_t sctlr_el1, tcr_el1, mair_el1, cpacr_el1;
    uint64_t ttbr0_el1, ttbr1_el1;
    
    dprintf("ARM64: Validating system register configuration\n");
    
    // Read all configured registers
    asm volatile("mrs %0, SCTLR_EL1" : "=r" (sctlr_el1));
    asm volatile("mrs %0, TCR_EL1" : "=r" (tcr_el1));
    asm volatile("mrs %0, MAIR_EL1" : "=r" (mair_el1));
    asm volatile("mrs %0, CPACR_EL1" : "=r" (cpacr_el1));
    asm volatile("mrs %0, TTBR0_EL1" : "=r" (ttbr0_el1));
    asm volatile("mrs %0, TTBR1_EL1" : "=r" (ttbr1_el1));
    
    dprintf("ARM64: System register validation results:\n");
    dprintf("  SCTLR_EL1 = 0x%016lx\n", sctlr_el1);
    dprintf("  TCR_EL1   = 0x%016lx\n", tcr_el1);
    dprintf("  MAIR_EL1  = 0x%016lx\n", mair_el1);
    dprintf("  CPACR_EL1 = 0x%016lx\n", cpacr_el1);
    dprintf("  TTBR0_EL1 = 0x%016lx\n", ttbr0_el1);
    dprintf("  TTBR1_EL1 = 0x%016lx\n", ttbr1_el1);
    
    // Validate critical bits
    bool validation_ok = true;
    
    // SCTLR_EL1 validation
    if ((sctlr_el1 & SCTLR_EL1_SA) == 0) {
        dprintf("ARM64: ERROR - Stack alignment check not enabled\n");
        validation_ok = false;
    }
    
    // TCR_EL1 validation
    uint64_t t0sz = (tcr_el1 & TCR_EL1_T0SZ_MASK) >> TCR_EL1_T0SZ_SHIFT;
    uint64_t t1sz = (tcr_el1 & TCR_EL1_T1SZ_MASK) >> TCR_EL1_T1SZ_SHIFT;
    if (t0sz > 39 || t1sz > 39) {
        dprintf("ARM64: ERROR - Invalid T0SZ (%lu) or T1SZ (%lu)\n", t0sz, t1sz);
        validation_ok = false;
    }
    
    // CPACR_EL1 validation
    uint64_t fpen = (cpacr_el1 & CPACR_EL1_FPEN_MASK) >> CPACR_EL1_FPEN_SHIFT;
    if (fpen == 0) {
        dprintf("ARM64: WARNING - FP/SIMD access disabled\n");
    }
    
    // TTBR validation (should be zero initially)
    if (ttbr0_el1 != 0 || ttbr1_el1 != 0) {
        dprintf("ARM64: WARNING - TTBR registers not zero (TTBR0=0x%lx, TTBR1=0x%lx)\n",
                ttbr0_el1, ttbr1_el1);
    }
    
    if (!validation_ok) {
        dprintf("ARM64: System register validation FAILED\n");
        return B_ERROR;
    }
    
    dprintf("ARM64: System register validation PASSED\n");
    return B_OK;
}

/*
 * Validate EL1 configuration
 * Verifies that EL1 system registers are properly configured
 */
static status_t
validate_el1_configuration(void)
{
    uint64_t sctlr_el1, mair_el1, cpacr_el1;
    
    // Read back system registers to validate configuration
    asm volatile("mrs %0, SCTLR_EL1" : "=r" (sctlr_el1));
    asm volatile("mrs %0, MAIR_EL1" : "=r" (mair_el1));
    asm volatile("mrs %0, CPACR_EL1" : "=r" (cpacr_el1));
    
    dprintf("ARM64: EL1 configuration validation:\n");
    dprintf("  SCTLR_EL1 = 0x%016lx\n", sctlr_el1);
    dprintf("  MAIR_EL1  = 0x%016lx\n", mair_el1);
    dprintf("  CPACR_EL1 = 0x%016lx\n", cpacr_el1);
    
    // Basic validation checks
    if ((sctlr_el1 & SCTLR_EL1_SA) == 0) {
        dprintf("ARM64: Warning - Stack alignment check not enabled\n");
    }
    
    if ((cpacr_el1 & CPACR_EL1_FPEN_MASK) == CPACR_EL1_FPEN_NONE) {
        dprintf("ARM64: Warning - FP/SIMD access disabled\n");
    }
    
    // Store validated configuration
    s_el_info.sctlr_el1 = sctlr_el1;
    
    return B_OK;
}

/*
 * Initialize EL1 system configuration
 * Main function to configure all EL1 system registers for kernel operation
 */
extern "C" status_t
arch_configure_el1_system_registers(void)
{
    uint32_t current_el = arch_get_current_exception_level();
    
    dprintf("ARM64: Comprehensive EL1 system register initialization (current EL%u)\n", current_el);
    
    if (current_el != ARM64_EL1) {
        dprintf("ARM64: Warning - configuring EL1 registers from EL%u\n", current_el);
    }
    
    // Phase 1: Initialize safe defaults for all registers
    initialize_el1_safe_defaults();
    
    // Phase 2: Configure system control register
    configure_el1_system_registers();
    
    // Phase 3: Configure memory attributes
    configure_el1_memory_attributes();
    
    // Phase 4: Configure translation control
    configure_el1_translation_control();
    
    // Phase 5: Configure floating point access
    configure_el1_floating_point();
    
    // Phase 6: Configure security features
    configure_el1_security_features();
    
    // Phase 7: Configure exception handling
    configure_el1_exception_handling();
    
    // Phase 8: Configure context registers
    configure_el1_context_registers();
    
    // Phase 9: Comprehensive validation
    status_t status = validate_system_register_values();
    if (status != B_OK) {
        panic("ARM64: System register validation failed");
        return status;
    }
    
    // Phase 10: Legacy validation for compatibility
    status = validate_el1_configuration();
    if (status != B_OK) {
        panic("ARM64: EL1 configuration validation failed");
        return status;
    }
    
    dprintf("ARM64: Comprehensive EL1 system register initialization completed successfully\n");
    return B_OK;
}

/*
 * Get exception level information
 * Returns a copy of the current exception level information structure
 */
extern "C" status_t
arch_get_exception_level_info(struct arm64_exception_level_info* info)
{
    if (info == NULL)
        return B_BAD_VALUE;
    
    memcpy(info, &s_el_info, sizeof(*info));
    return B_OK;
}

/*
 * Transition from EL2 to EL1
 * Configures EL2 registers to enable proper EL1 operation
 */
extern "C" status_t
arch_transition_el2_to_el1(void)
{
    uint32_t current_el = arch_get_current_exception_level();
    
    if (current_el != ARM64_EL2) {
        dprintf("ARM64: Warning - EL2->EL1 transition called from EL%u\n", current_el);
        return B_BAD_VALUE;
    }
    
    if (!s_el_info.el2_present) {
        dprintf("ARM64: EL2 not present, cannot transition\n");
        return B_NOT_SUPPORTED;
    }
    
    dprintf("ARM64: Configuring EL2 for EL1 operation\n");
    
    uint64_t hcr_el2 = 0;
    uint64_t cptr_el2, hstr_el2, cnthctl_el2;
    
    // Configure HCR_EL2 for EL1 AArch64 operation
    hcr_el2 |= HCR_EL2_RW;  // EL1 executes in AArch64 state
    
    asm volatile("msr HCR_EL2, %0" : : "r" (hcr_el2));
    
    // Configure CPTR_EL2 - don't trap FP/SIMD to EL2
    cptr_el2 = 0x33FF; // Set RES1 bits, clear TFP bit
    asm volatile("msr CPTR_EL2, %0" : : "r" (cptr_el2));
    
    // Configure HSTR_EL2 - don't trap any CP15 accesses
    hstr_el2 = 0;
    asm volatile("msr HSTR_EL2, %0" : : "r" (hstr_el2));
    
    // Configure CNTHCTL_EL2 - allow EL1 access to timers
    cnthctl_el2 = 0x3; // EL1PCTEN | EL1PCEN
    asm volatile("msr CNTHCTL_EL2, %0" : : "r" (cnthctl_el2));
    
    // Set virtual timer offset to 0
    asm volatile("msr CNTVOFF_EL2, xzr");
    
    // Set up virtualization processor ID registers
    uint64_t midr_el1, mpidr_el1;
    asm volatile("mrs %0, MIDR_EL1" : "=r" (midr_el1));
    asm volatile("mrs %0, MPIDR_EL1" : "=r" (mpidr_el1));
    asm volatile("msr VPIDR_EL2, %0" : : "r" (midr_el1));
    asm volatile("msr VMPIDR_EL2, %0" : : "r" (mpidr_el1));
    
    // Ensure all changes are visible
    asm volatile("isb");
    
    dprintf("ARM64: EL2 configured for EL1 operation\n");
    dprintf("  HCR_EL2     = 0x%016lx\n", hcr_el2);
    dprintf("  CPTR_EL2    = 0x%016lx\n", cptr_el2);
    dprintf("  CNTHCTL_EL2 = 0x%016lx\n", cnthctl_el2);
    
    return B_OK;
}

/*
 * Enable MMU and caches at EL1
 * This function should be called after memory management is initialized
 */
extern "C" status_t
arch_enable_el1_mmu_caches(void)
{
    uint32_t current_el = arch_get_current_exception_level();
    uint64_t sctlr_el1;
    
    if (current_el != ARM64_EL1) {
        dprintf("ARM64: Warning - enabling EL1 MMU/caches from EL%u\n", current_el);
    }
    
    // Read current SCTLR_EL1
    asm volatile("mrs %0, SCTLR_EL1" : "=r" (sctlr_el1));
    
    // Enable MMU and caches
    sctlr_el1 |= SCTLR_EL1_M;  // MMU enable
    sctlr_el1 |= SCTLR_EL1_C;  // Data cache enable
    sctlr_el1 |= SCTLR_EL1_I;  // Instruction cache enable
    
    dprintf("ARM64: Enabling EL1 MMU and caches (SCTLR_EL1 = 0x%016lx)\n", sctlr_el1);
    
    asm volatile(
        "msr SCTLR_EL1, %0\n"
        "isb\n"
        :
        : "r" (sctlr_el1)
        : "memory"
    );
    
    // Verify MMU and caches are enabled
    asm volatile("mrs %0, SCTLR_EL1" : "=r" (sctlr_el1));
    if ((sctlr_el1 & (SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I)) !=
        (SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I)) {
        panic("ARM64: Failed to enable MMU and caches");
        return B_ERROR;
    }
    
    s_el_info.sctlr_el1 = sctlr_el1;
    dprintf("ARM64: EL1 MMU and caches enabled successfully\n");
    
    return B_OK;
}

/*
 * Enable translation table walks for TTBR0_EL1 and/or TTBR1_EL1
 * This should be called after page tables are set up
 */
extern "C" status_t
arch_enable_el1_translation_tables(bool enable_ttbr0, bool enable_ttbr1)
{
    uint64_t tcr_el1;
    
    // Read current TCR_EL1
    asm volatile("mrs %0, TCR_EL1" : "=r" (tcr_el1));
    
    dprintf("ARM64: Enabling translation tables (TTBR0=%s, TTBR1=%s)\n",
            enable_ttbr0 ? "yes" : "no", enable_ttbr1 ? "yes" : "no");
    
    // Enable/disable TTBR0_EL1 translation table walks
    if (enable_ttbr0) {
        tcr_el1 &= ~TCR_EL1_EPD0;  // Clear EPD0 to enable TTBR0 walks
        dprintf("ARM64: TTBR0_EL1 translation table walks enabled\n");
    } else {
        tcr_el1 |= TCR_EL1_EPD0;   // Set EPD0 to disable TTBR0 walks
        dprintf("ARM64: TTBR0_EL1 translation table walks disabled\n");
    }
    
    // Enable/disable TTBR1_EL1 translation table walks
    if (enable_ttbr1) {
        tcr_el1 &= ~TCR_EL1_EPD1;  // Clear EPD1 to enable TTBR1 walks
        dprintf("ARM64: TTBR1_EL1 translation table walks enabled\n");
    } else {
        tcr_el1 |= TCR_EL1_EPD1;   // Set EPD1 to disable TTBR1 walks
        dprintf("ARM64: TTBR1_EL1 translation table walks disabled\n");
    }
    
    // Write updated TCR_EL1
    asm volatile(
        "msr TCR_EL1, %0\n"
        "isb\n"
        :
        : "r" (tcr_el1)
        : "memory"
    );
    
    // Verify the change took effect
    uint64_t new_tcr_el1;
    asm volatile("mrs %0, TCR_EL1" : "=r" (new_tcr_el1));
    
    bool ttbr0_enabled = (new_tcr_el1 & TCR_EL1_EPD0) == 0;
    bool ttbr1_enabled = (new_tcr_el1 & TCR_EL1_EPD1) == 0;
    
    dprintf("ARM64: Translation table status: TTBR0=%s, TTBR1=%s\n",
            ttbr0_enabled ? "enabled" : "disabled",
            ttbr1_enabled ? "enabled" : "disabled");
    
    return B_OK;
}

/*
 * Set exception vector base address (VBAR_EL1)
 * The vector table must be 2KB aligned
 */
extern "C" status_t
arch_set_el1_exception_vector_base(uint64_t vector_base)
{
    // Validate alignment (must be 2KB aligned)
    if (vector_base & (VBAR_EL1_ALIGNMENT - 1)) {
        dprintf("ARM64: ERROR - Vector base address 0x%016lx not 2KB aligned\n", vector_base);
        return B_BAD_VALUE;
    }
    
    dprintf("ARM64: Setting exception vector base to 0x%016lx\n", vector_base);
    
    // Set VBAR_EL1
    asm volatile(
        "msr VBAR_EL1, %0\n"
        "isb\n"
        :
        : "r" (vector_base)
        : "memory"
    );
    
    // Verify it was set correctly
    uint64_t read_back;
    asm volatile("mrs %0, VBAR_EL1" : "=r" (read_back));
    
    if (read_back != vector_base) {
        dprintf("ARM64: ERROR - VBAR_EL1 readback mismatch (expected 0x%016lx, got 0x%016lx)\n",
                vector_base, read_back);
        return B_ERROR;
    }
    
    dprintf("ARM64: Exception vector base set successfully\n");
    return B_OK;
}

/*
 * Configure Top Byte Ignore (TBI) for user and kernel space
 * Enables/disables tagged addressing for virtual addresses
 */
extern "C" status_t
arch_configure_el1_top_byte_ignore(bool enable_ttbr0_tbi, bool enable_ttbr1_tbi)
{
    uint64_t tcr_el1;
    
    // Read current TCR_EL1
    asm volatile("mrs %0, TCR_EL1" : "=r" (tcr_el1));
    
    dprintf("ARM64: Configuring Top Byte Ignore (TTBR0=%s, TTBR1=%s)\n",
            enable_ttbr0_tbi ? "enabled" : "disabled",
            enable_ttbr1_tbi ? "enabled" : "disabled");
    
    // Configure TTBR0 Top Byte Ignore (user space)
    if (enable_ttbr0_tbi) {
        tcr_el1 |= TCR_EL1_TBI0;
    } else {
        tcr_el1 &= ~TCR_EL1_TBI0;
    }
    
    // Configure TTBR1 Top Byte Ignore (kernel space)
    if (enable_ttbr1_tbi) {
        tcr_el1 |= TCR_EL1_TBI1;
    } else {
        tcr_el1 &= ~TCR_EL1_TBI1;
    }
    
    // Write updated TCR_EL1
    asm volatile(
        "msr TCR_EL1, %0\n"
        "isb\n"
        :
        : "r" (tcr_el1)
        : "memory"
    );
    
    dprintf("ARM64: Top Byte Ignore configuration completed\n");
    return B_OK;
}

/*
 * Debug function to dump current system register state
 */
extern "C" void
arch_dump_el1_registers(void)
{
    uint64_t sctlr_el1, mair_el1, cpacr_el1, ttbr0_el1, ttbr1_el1;
    uint64_t tcr_el1, vbar_el1, esr_el1, far_el1;
    uint64_t contextidr_el1, tpidr_el1, sp_el0;
    uint32_t current_el;
    
    current_el = arch_get_current_exception_level();
    
    if (current_el < ARM64_EL1) {
        dprintf("ARM64: Cannot dump EL1 registers from EL%u\n", current_el);
        return;
    }
    
    // Read EL1 system registers
    asm volatile("mrs %0, SCTLR_EL1" : "=r" (sctlr_el1));
    asm volatile("mrs %0, MAIR_EL1" : "=r" (mair_el1));
    asm volatile("mrs %0, CPACR_EL1" : "=r" (cpacr_el1));
    asm volatile("mrs %0, TTBR0_EL1" : "=r" (ttbr0_el1));
    asm volatile("mrs %0, TTBR1_EL1" : "=r" (ttbr1_el1));
    asm volatile("mrs %0, TCR_EL1" : "=r" (tcr_el1));
    asm volatile("mrs %0, VBAR_EL1" : "=r" (vbar_el1));
    asm volatile("mrs %0, ESR_EL1" : "=r" (esr_el1));
    asm volatile("mrs %0, FAR_EL1" : "=r" (far_el1));
    asm volatile("mrs %0, CONTEXTIDR_EL1" : "=r" (contextidr_el1));
    asm volatile("mrs %0, TPIDR_EL1" : "=r" (tpidr_el1));
    asm volatile("mrs %0, SP_EL0" : "=r" (sp_el0));
    
    dprintf("ARM64 EL1 System Registers (from EL%u):\n", current_el);
    dprintf("===========================================\n");
    dprintf("Control Registers:\n");
    dprintf("  SCTLR_EL1 = 0x%016lx  (System Control)\n", sctlr_el1);
    dprintf("  TCR_EL1   = 0x%016lx  (Translation Control)\n", tcr_el1);
    dprintf("  CPACR_EL1 = 0x%016lx  (Coprocessor Access)\n", cpacr_el1);
    dprintf("\nMemory Management:\n");
    dprintf("  MAIR_EL1  = 0x%016lx  (Memory Attributes)\n", mair_el1);
    dprintf("  TTBR0_EL1 = 0x%016lx  (Translation Table Base 0)\n", ttbr0_el1);
    dprintf("  TTBR1_EL1 = 0x%016lx  (Translation Table Base 1)\n", ttbr1_el1);
    dprintf("\nException Handling:\n");
    dprintf("  VBAR_EL1  = 0x%016lx  (Vector Base Address)\n", vbar_el1);
    dprintf("  ESR_EL1   = 0x%016lx  (Exception Syndrome)\n", esr_el1);
    dprintf("  FAR_EL1   = 0x%016lx  (Fault Address)\n", far_el1);
    dprintf("\nContext Registers:\n");
    dprintf("  CONTEXTIDR_EL1 = 0x%016lx  (Context ID)\n", contextidr_el1);
    dprintf("  TPIDR_EL1      = 0x%016lx  (Thread Pointer)\n", tpidr_el1);
    dprintf("  SP_EL0         = 0x%016lx  (EL0 Stack Pointer)\n", sp_el0);
    dprintf("\n");
    
    // Decode important SCTLR_EL1 bits
    dprintf("SCTLR_EL1 decoded:\n");
    dprintf("  MMU:     %s\n", (sctlr_el1 & SCTLR_EL1_M) ? "enabled" : "disabled");
    dprintf("  Align:   %s\n", (sctlr_el1 & SCTLR_EL1_A) ? "enabled" : "disabled");
    dprintf("  D-Cache: %s\n", (sctlr_el1 & SCTLR_EL1_C) ? "enabled" : "disabled");
    dprintf("  I-Cache: %s\n", (sctlr_el1 & SCTLR_EL1_I) ? "enabled" : "disabled");
    dprintf("  SA:      %s\n", (sctlr_el1 & SCTLR_EL1_SA) ? "enabled" : "disabled");
    dprintf("  SA0:     %s\n", (sctlr_el1 & SCTLR_EL1_SA0) ? "enabled" : "disabled");
    dprintf("  WXN:     %s\n", (sctlr_el1 & SCTLR_EL1_WXN) ? "enabled" : "disabled");
    
    // Decode important TCR_EL1 bits
    dprintf("\nTCR_EL1 decoded:\n");
    uint64_t t0sz = (tcr_el1 & TCR_EL1_T0SZ_MASK) >> TCR_EL1_T0SZ_SHIFT;
    uint64_t t1sz = (tcr_el1 & TCR_EL1_T1SZ_MASK) >> TCR_EL1_T1SZ_SHIFT;
    dprintf("  T0SZ:    %lu (VA size: %lu bits)\n", t0sz, 64 - t0sz);
    dprintf("  T1SZ:    %lu (VA size: %lu bits)\n", t1sz, 64 - t1sz);
    dprintf("  TG0:     %s\n", 
            ((tcr_el1 & TCR_EL1_TG0_MASK) == TCR_EL1_TG0_4K) ? "4KB" :
            ((tcr_el1 & TCR_EL1_TG0_MASK) == TCR_EL1_TG0_16K) ? "16KB" :
            ((tcr_el1 & TCR_EL1_TG0_MASK) == TCR_EL1_TG0_64K) ? "64KB" : "unknown");
    dprintf("  TG1:     %s\n",
            ((tcr_el1 & TCR_EL1_TG1_MASK) == TCR_EL1_TG1_4K) ? "4KB" :
            ((tcr_el1 & TCR_EL1_TG1_MASK) == TCR_EL1_TG1_16K) ? "16KB" :
            ((tcr_el1 & TCR_EL1_TG1_MASK) == TCR_EL1_TG1_64K) ? "64KB" : "unknown");
    dprintf("  EPD0:    %s\n", (tcr_el1 & TCR_EL1_EPD0) ? "disabled" : "enabled");
    dprintf("  EPD1:    %s\n", (tcr_el1 & TCR_EL1_EPD1) ? "disabled" : "enabled");
    dprintf("  AS:      %s\n", (tcr_el1 & TCR_EL1_AS) ? "16-bit ASIDs" : "8-bit ASIDs");
    dprintf("  TBI0:    %s\n", (tcr_el1 & TCR_EL1_TBI0) ? "enabled" : "disabled");
    dprintf("  TBI1:    %s\n", (tcr_el1 & TCR_EL1_TBI1) ? "enabled" : "disabled");
    
    // Decode physical address size
    uint64_t ips = (tcr_el1 & TCR_EL1_IPS_MASK) >> TCR_EL1_IPS_SHIFT;
    const char* ips_str;
    switch (ips) {
        case 0: ips_str = "32 bits (4GB)"; break;
        case 1: ips_str = "36 bits (64GB)"; break;
        case 2: ips_str = "40 bits (1TB)"; break;
        case 3: ips_str = "42 bits (4TB)"; break;
        case 4: ips_str = "44 bits (16TB)"; break;
        case 5: ips_str = "48 bits (256TB)"; break;
        case 6: ips_str = "52 bits (4PB)"; break;
        default: ips_str = "unknown"; break;
    }
    dprintf("  IPS:     %s\n", ips_str);
}