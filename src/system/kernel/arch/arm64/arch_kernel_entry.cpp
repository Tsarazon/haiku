/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Kernel C Entry Point
 * 
 * This module provides the C function entry point that receives control from
 * the assembly boot code (arch_start.S) and transitions to generic kernel
 * initialization following proper ARM64 calling conventions.
 *
 * Authors:
 *   ARM64 Kernel Development Team
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Basic Haiku types for kernel compilation
typedef int32_t status_t;
#define B_OK                     0
#define B_ERROR                 -1
#define B_NO_MEMORY             -2147483646

// Forward declarations for kernel functions
extern "C" {
    // Main kernel initialization (will be provided by kernel/main.cpp)
    int kernel_main(struct kernel_args* args, int current_cpu);
    
    // Early debug UART functions (from our implementation)
    status_t arch_debug_uart_init(void);
    status_t arch_debug_uart_printf(const char* format, ...);
    bool arch_debug_uart_available(void);
    
    // Exception level management functions
    status_t arch_detect_exception_levels(void);
    status_t arch_configure_el1_system_registers(void);
    
    // PSCI power management functions
    status_t arch_psci_init(void);
}

/*
 * ARM64 Kernel Arguments Structure
 * 
 * This structure contains all the information passed from the bootloader
 * and early boot validation, formatted for the generic kernel initialization.
 */
struct kernel_args {
    // Boot information
    uint64_t    dtb_phys_addr;          // Device tree physical address
    uint32_t    dtb_size;               // Device tree size
    uint32_t    current_cpu;            // Current CPU ID (should be 0 for boot CPU)
    
    // Memory layout information
    uint64_t    kernel_phys_base;       // Kernel physical base address
    uint64_t    kernel_virt_base;       // Kernel virtual base address
    uint64_t    kernel_size;            // Kernel size in bytes
    
    // Boot validation results  
    uint64_t    boot_validation_flags;  // Validation flags from arch_start.S
    uint64_t    original_exception_level; // Original EL from bootloader
    uint64_t    original_stack_pointer; // Original SP from bootloader
    
    // Hardware information
    uint64_t    cpu_midr;               // Main ID Register
    uint64_t    cpu_mpidr;              // Multiprocessor Affinity Register
    uint64_t    cpu_features;           // CPU feature flags
    
    // Memory regions (simplified for early boot)
    struct {
        uint64_t    phys_addr;
        uint64_t    size;
        uint32_t    type;               // Memory type (RAM, reserved, etc.)
        uint32_t    flags;              // Memory flags
    } memory_regions[16];               // Up to 16 memory regions
    uint32_t    num_memory_regions;
    
    // Platform-specific information
    uint64_t    platform_data[8];       // Platform-specific data
    
    // Debug information
    char        debug_output[256];      // Early debug messages
    uint32_t    debug_uart_type;        // Debug UART type detected
    uint64_t    debug_uart_base;        // Debug UART base address
    
    // Reserved for future expansion
    uint64_t    reserved[16];
};

/*
 * Boot Information Structure (defined in arch_start.S)
 */
extern "C" struct arm64_boot_info {
    uint64_t    dtb_phys_addr;          // Offset 0
    uint64_t    original_current_el;    // Offset 8
    uint64_t    validation_flags;       // Offset 16
    uint64_t    original_daif;          // Offset 24
    uint64_t    original_sctlr_el1;     // Offset 32
    uint64_t    original_mair_el1;      // Offset 40
    uint64_t    cpu_midr;               // Offset 48
    uint64_t    cpu_mpidr;              // Offset 56
    uint64_t    panic_code;             // Offset 64
    uint64_t    dtb_size;               // Offset 72
    uint64_t    dtb_version;            // Offset 80
    uint64_t    original_stack_ptr;     // Offset 88
    uint64_t    cpu_features;           // Offset 96
    uint64_t    stack_flags;            // Offset 104
    uint64_t    final_stack_ptr;        // Offset 112
    uint64_t    stack_size;             // Offset 120
    char        signature[18];          // "ARM64BOOT_ENHANCED"
} arm64_boot_info;

/*
 * Early Kernel Initialization Functions
 */

// Initialize early debugging infrastructure
static status_t
init_early_debug(void)
{
    // Try to initialize early debug UART
    status_t status = arch_debug_uart_init();
    if (status == B_OK) {
        arch_debug_uart_printf("\r\n=== ARM64 Kernel C Entry Point ===\r\n");
        arch_debug_uart_printf("Early debug UART initialized successfully\r\n");
        return B_OK;
    }
    
    return status; // Continue even if debug UART fails
}

// Initialize ARM64-specific hardware
static status_t
init_arm64_hardware(void)
{
    status_t status;
    
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Initializing ARM64 hardware...\r\n");
    }
    
    // Initialize exception level management
    status = arch_detect_exception_levels();
    if (status == B_OK) {
        if (arch_debug_uart_available()) {
            arch_debug_uart_printf("Exception level management initialized\r\n");
        }
        
        // Configure EL1 system registers
        status = arch_configure_el1_system_registers();
        if (status == B_OK && arch_debug_uart_available()) {
            arch_debug_uart_printf("EL1 system registers configured\r\n");
        }
    } else if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Warning: Exception level detection failed\r\n");
    }
    
    // Initialize PSCI power management
    status = arch_psci_init();
    if (status == B_OK && arch_debug_uart_available()) {
        arch_debug_uart_printf("PSCI power management initialized\r\n");
    } else if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Warning: PSCI initialization failed\r\n");
    }
    
    return B_OK;
}

// Parse device tree and extract essential information
static status_t
parse_device_tree_basics(uint64_t dtb_addr, struct kernel_args* args)
{
    if (dtb_addr == 0) {
        return B_ERROR;
    }
    
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Device tree at physical address 0x%llx\r\n", dtb_addr);
    }
    
    // Basic DTB header validation (already done in assembly, but double-check)
    uint32_t* dtb = (uint32_t*)dtb_addr;
    
    // Read magic number (big-endian)
    uint32_t magic = __builtin_bswap32(dtb[0]);
    if (magic != 0xd00dfeed) {
        if (arch_debug_uart_available()) {
            arch_debug_uart_printf("Invalid DTB magic: 0x%x\r\n", magic);
        }
        return B_ERROR;
    }
    
    // Read size (big-endian)
    uint32_t size = __builtin_bswap32(dtb[1]);
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("DTB size: %u bytes\r\n", size);
    }
    
    // Store DTB information in kernel args
    args->dtb_phys_addr = dtb_addr;
    args->dtb_size = size;
    
    // TODO: Parse memory regions, CPU information, etc.
    // For now, just mark that we have a valid DTB
    
    return B_OK;
}

// Set up initial memory layout information
static status_t
setup_memory_layout(struct kernel_args* args)
{
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Setting up initial memory layout...\r\n");
    }
    
    // For early boot, we'll set up basic memory regions
    // This will be expanded once we have proper memory management
    
    args->num_memory_regions = 1;
    
    // Basic RAM region (will be refined later)
    args->memory_regions[0].phys_addr = 0x40000000;  // Common ARM64 RAM base
    args->memory_regions[0].size = 0x40000000;       // 1GB for now
    args->memory_regions[0].type = 1;                // RAM type
    args->memory_regions[0].flags = 0;               // No special flags
    
    // Set kernel addresses (these will be determined by linker/memory manager)
    args->kernel_phys_base = 0x40080000;  // Common kernel physical base
    args->kernel_virt_base = 0xFFFFFF8000000000ULL; // Kernel virtual base
    args->kernel_size = 0x02000000;       // 32MB kernel size estimate
    
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Kernel physical base: 0x%llx\r\n", args->kernel_phys_base);
        arch_debug_uart_printf("Kernel virtual base: 0x%llx\r\n", args->kernel_virt_base);
        arch_debug_uart_printf("Memory regions: %u\r\n", args->num_memory_regions);
    }
    
    return B_OK;
}

// Validate and copy boot information to kernel args
static status_t
setup_kernel_args(struct kernel_args* kernel_args_stack, uint32_t current_cpu)
{
    // Clear the kernel args structure
    memset(kernel_args_stack, 0, sizeof(struct kernel_args));
    
    // Copy boot information from assembly boot info structure
    kernel_args_stack->current_cpu = current_cpu;
    kernel_args_stack->boot_validation_flags = arm64_boot_info.validation_flags;
    kernel_args_stack->original_exception_level = arm64_boot_info.original_current_el;
    kernel_args_stack->original_stack_pointer = arm64_boot_info.original_stack_ptr;
    kernel_args_stack->cpu_midr = arm64_boot_info.cpu_midr;
    kernel_args_stack->cpu_mpidr = arm64_boot_info.cpu_mpidr;
    kernel_args_stack->cpu_features = arm64_boot_info.cpu_features;
    
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Kernel args setup complete\r\n");
        arch_debug_uart_printf("Current CPU: %u\r\n", current_cpu);
        arch_debug_uart_printf("Original EL: %llu\r\n", 
                              kernel_args_stack->original_exception_level >> 2);
        arch_debug_uart_printf("Validation flags: 0x%llx\r\n", 
                              kernel_args_stack->boot_validation_flags);
    }
    
    return B_OK;
}

/*
 * Main ARM64 Kernel Entry Point
 * 
 * This function is called from arch_start.S following ARM64 AAPCS calling convention:
 * - x0: kernel_args pointer (allocated on stack by assembly code)
 * - x1: current_cpu (should be 0 for boot CPU)
 * - Stack: 16-byte aligned, sufficient space for C function calls
 * - Exception Level: EL1 
 * - MMU: Disabled
 * - Interrupts: Masked
 */
extern "C" int
arch_kernel_entry(struct kernel_args* kernel_args_from_asm, uint32_t current_cpu)
{
    /*
     * Phase 1: Early Environment Setup
     * Set up minimal environment for C code execution
     */
    
    // Initialize early debugging first
    status_t status = init_early_debug();
    
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("=== ARM64 Kernel Entry Point ===\r\n");
        arch_debug_uart_printf("Entry parameters:\r\n");
        arch_debug_uart_printf("  kernel_args: 0x%p\r\n", kernel_args_from_asm);
        arch_debug_uart_printf("  current_cpu: %u\r\n", current_cpu);
        
        // Validate ARM64 AAPCS calling convention compliance
        uintptr_t sp_value;
        asm volatile("mov %0, sp" : "=r" (sp_value));
        arch_debug_uart_printf("  stack_ptr: 0x%lx (aligned: %s)\r\n", 
                              sp_value, (sp_value & 0xF) == 0 ? "yes" : "no");
        
        if ((sp_value & 0xF) != 0) {
            arch_debug_uart_printf("ERROR: Stack not 16-byte aligned!\r\n");
            return -1;
        }
    }
    
    /*
     * Phase 2: Boot Information Validation and Setup
     * Validate parameters and set up kernel arguments structure
     */
    
    // Validate input parameters
    if (kernel_args_from_asm == NULL) {
        if (arch_debug_uart_available()) {
            arch_debug_uart_printf("ERROR: kernel_args is NULL\r\n");
        }
        return -1;
    }
    
    if (current_cpu != 0) {
        if (arch_debug_uart_available()) {
            arch_debug_uart_printf("ERROR: Boot CPU should be CPU 0, got %u\r\n", current_cpu);
        }
        return -1;
    }
    
    // Set up kernel arguments structure
    status = setup_kernel_args(kernel_args_from_asm, current_cpu);
    if (status != B_OK) {
        if (arch_debug_uart_available()) {
            arch_debug_uart_printf("ERROR: Failed to set up kernel args\r\n");
        }
        return -1;
    }
    
    /*
     * Phase 3: ARM64 Hardware Initialization
     * Initialize ARM64-specific hardware and system registers
     */
    
    status = init_arm64_hardware();
    if (status != B_OK) {
        if (arch_debug_uart_available()) {
            arch_debug_uart_printf("Warning: ARM64 hardware init had issues\r\n");
        }
        // Continue - some failures are non-fatal
    }
    
    /*
     * Phase 4: Device Tree Processing
     * Parse device tree and extract essential system information
     */
    
    uint64_t dtb_addr = arm64_boot_info.dtb_phys_addr;
    if (dtb_addr != 0) {
        status = parse_device_tree_basics(dtb_addr, kernel_args_from_asm);
        if (status != B_OK) {
            if (arch_debug_uart_available()) {
                arch_debug_uart_printf("Warning: Device tree parsing failed\r\n");
            }
        }
    } else if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Warning: No device tree provided\r\n");
    }
    
    /*
     * Phase 5: Memory Layout Setup  
     * Set up initial memory layout information
     */
    
    status = setup_memory_layout(kernel_args_from_asm);
    if (status != B_OK) {
        if (arch_debug_uart_available()) {
            arch_debug_uart_printf("ERROR: Failed to set up memory layout\r\n");
        }
        return -1;
    }
    
    /*
     * Phase 6: Pre-Kernel Diagnostics
     * Display comprehensive system information before kernel handoff
     */
    
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("\r\n=== Pre-Kernel System Information ===\r\n");
        arch_debug_uart_printf("Boot validation: 0x%llx\r\n", 
                              kernel_args_from_asm->boot_validation_flags);
        arch_debug_uart_printf("CPU MIDR: 0x%llx\r\n", kernel_args_from_asm->cpu_midr);
        arch_debug_uart_printf("CPU MPIDR: 0x%llx\r\n", kernel_args_from_asm->cpu_mpidr);
        arch_debug_uart_printf("Memory regions: %u\r\n", 
                              kernel_args_from_asm->num_memory_regions);
        
        if (kernel_args_from_asm->dtb_size > 0) {
            arch_debug_uart_printf("Device tree: %u bytes at 0x%llx\r\n",
                                  kernel_args_from_asm->dtb_size,
                                  kernel_args_from_asm->dtb_phys_addr);
        }
        
        arch_debug_uart_printf("Transitioning to generic kernel...\r\n");
    }
    
    /*
     * Phase 7: Transition to Generic Kernel
     * Call the generic kernel main function with properly formatted arguments
     */
    
    // Final validation before kernel handoff
    if (kernel_args_from_asm->dtb_phys_addr == 0) {
        if (arch_debug_uart_available()) {
            arch_debug_uart_printf("Warning: Proceeding without device tree\r\n");
        }
    }
    
    // Ensure all ARM64-specific initialization is complete
    asm volatile("dsb sy" ::: "memory");    // Data synchronization barrier
    asm volatile("isb" ::: "memory");       // Instruction synchronization barrier
    
    // Call generic kernel main
    // This follows the standard Haiku kernel_main signature:
    // int kernel_main(struct kernel_args* args, int current_cpu)
    int result = kernel_main(kernel_args_from_asm, (int)current_cpu);
    
    /*
     * Phase 8: Post-Kernel Error Handling
     * Handle unexpected return from kernel main
     */
    
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("ERROR: kernel_main returned %d (should not return)\r\n", result);
        arch_debug_uart_printf("System halted\r\n");
    }
    
    // Disable interrupts and halt
    asm volatile("msr DAIFSet, #0xf" ::: "memory");
    while (1) {
        asm volatile("wfe" ::: "memory");
    }
    
    return result; // Should never reach here
}

/*
 * Secondary CPU Entry Point (for SMP systems)
 * Called from arch_start.S for secondary CPU initialization
 */
extern "C" void
_start_secondary_cpu(uint32_t cpu_id)
{
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Secondary CPU %u starting...\r\n", cpu_id);
    }
    
    // Initialize ARM64 hardware for secondary CPU
    init_arm64_hardware();
    
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Secondary CPU %u hardware initialized\r\n", cpu_id);
    }
    
    // TODO: Call generic secondary CPU initialization
    // This would typically be something like:
    // secondary_cpu_main(cpu_id);
    
    // For now, just halt the secondary CPU
    if (arch_debug_uart_available()) {
        arch_debug_uart_printf("Secondary CPU %u halted (not yet implemented)\r\n", cpu_id);
    }
    
    asm volatile("msr DAIFSet, #0xf" ::: "memory");
    while (1) {
        asm volatile("wfe" ::: "memory");
    }
}

/*
 * ARM64 Calling Convention Validation Functions
 * These functions help ensure proper ARM64 AAPCS compliance
 */

// Validate that function parameters are passed correctly
extern "C" bool
arch_validate_calling_convention(void* arg0, uint64_t arg1, void* sp_ptr)
{
    uintptr_t sp_value = (uintptr_t)sp_ptr;
    
    // Check stack alignment (must be 16-byte aligned)
    if ((sp_value & 0xF) != 0) {
        return false;
    }
    
    // Check that arguments are reasonable
    if (arg0 == NULL && arg1 != 0) {
        return false; // Suspicious parameter combination
    }
    
    return true;
}

// Get current stack pointer for validation
extern "C" uintptr_t
arch_get_stack_pointer(void)
{
    uintptr_t sp;
    asm volatile("mov %0, sp" : "=r" (sp));
    return sp;
}

// Get current frame pointer
extern "C" uintptr_t
arch_get_frame_pointer(void)
{
    uintptr_t fp;
    asm volatile("mov %0, x29" : "=r" (fp));
    return fp;
}