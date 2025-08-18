/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Kernel Start Implementation
 * C entry point called from arch_start.S
 */

#include <kernel.h>
#include <boot/kernel_args.h>
#include <arch/cpu.h>
#include <arch/debug.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>

#include <arch/aarch64/arch_cpu.h>
#include <arch/aarch64/arm_registers.h>

// External symbols from arch_start.S
extern "C" void _start(kernel_args* kernelArgs, int currentCPU);
extern "C" uint64 arm64_boot_info[8];

// Forward declarations
static status_t validate_kernel_args(kernel_args* args);
static status_t setup_early_debug_output(kernel_args* args);
static status_t validate_device_tree(kernel_args* args);
static void print_boot_validation_info(kernel_args* args);

/*
 * ARM64 Kernel Main Entry Point
 * Called from assembly _start function in arch_start.S
 * 
 * Parameters:
 * - kernelArgs: Pointer to kernel arguments structure set up in assembly
 * - currentCPU: CPU number (0 for boot CPU)
 */
extern "C" void
_start_kernel_main(kernel_args* kernelArgs, int currentCPU)
{
    // Validate input parameters
    if (kernelArgs == NULL) {
        // Can't do much without kernel args - halt
        arch_debug_panic("NULL kernel_args passed to kernel main");
        return;
    }
    
    if (currentCPU != 0) {
        // Secondary CPU startup should use different path
        arch_debug_panic("Secondary CPU used boot CPU entry path");
        return;
    }
    
    // Set up early debug output as soon as possible
    status_t status = setup_early_debug_output(kernelArgs);
    if (status != B_OK) {
        // Continue anyway, but note the failure
        dprintf("Warning: Early debug output setup failed: %s\n", strerror(status));
    }
    
    // Print boot banner
    dprintf("Haiku ARM64 Kernel Starting...\n");
    dprintf("Kernel entry validation passed\n");
    
    // Validate kernel arguments structure
    status = validate_kernel_args(kernelArgs);
    if (status != B_OK) {
        panic("Kernel arguments validation failed: %s", strerror(status));
        return;
    }
    
    // Validate device tree if provided
    status = validate_device_tree(kernelArgs);
    if (status != B_OK) {
        dprintf("Warning: Device tree validation failed: %s\n", strerror(status));
        // Continue anyway - device tree issues are often non-fatal
    }
    
    // Print detailed boot validation information
    print_boot_validation_info(kernelArgs);
    
    // Store boot information in global structure for debugging
    if (kernelArgs->arch_args.fdt != NULL) {
        arm64_boot_info[0] = (uint64)kernelArgs->arch_args.fdt;
    }
    
    // Extract boot validation results from kernel_args
    uint64* boot_data = (uint64*)((char*)kernelArgs + 0x108);
    arm64_boot_info[1] = boot_data[0];  // Original exception level
    arm64_boot_info[2] = boot_data[1];  // Original DAIF state  
    arm64_boot_info[3] = boot_data[2];  // Warning flags
    
    // Initialize basic CPU features detection
    arch_cpu_info* cpu_info = &gKernelArgs.arch_args.cpu_info;
    memset(cpu_info, 0, sizeof(arch_cpu_info));
    
    // Read basic CPU identification
    cpu_info->midr = arm64_read_midr_el1();
    cpu_info->mpidr = arm64_read_mpidr_el1();
    cpu_info->revidr = arm64_read_revidr_el1();
    
    // Detect CPU features
    arch_cpu_detect_features(cpu_info);
    
    dprintf("ARM64 CPU: MIDR=0x%016lx MPIDR=0x%016lx\n", 
            cpu_info->midr, cpu_info->mpidr);
    
    // Call the main kernel initialization
    dprintf("Calling main kernel initialization...\n");
    _start(kernelArgs, currentCPU);
    
    // Should never return
    panic("Kernel main returned unexpectedly");
}

/*
 * Secondary CPU Entry Point
 * Called from assembly _secondary_start function
 */
extern "C" void
_start_secondary_cpu(int cpuId)
{
    dprintf("Secondary CPU %d starting...\n", cpuId);
    
    // Initialize secondary CPU
    // TODO: Implement secondary CPU initialization
    
    // For now, just halt secondary CPUs
    dprintf("Secondary CPU %d halted (not yet implemented)\n", cpuId);
    while (true) {
        asm volatile("wfe");  // Wait for event
    }
}

/*
 * Validate kernel arguments structure
 */
static status_t
validate_kernel_args(kernel_args* args)
{
    // Check kernel args size
    if (args->kernel_args_size == 0 || args->kernel_args_size > 0x10000) {
        dprintf("Invalid kernel_args size: %u\n", args->kernel_args_size);
        return B_BAD_DATA;
    }
    
    // Check version
    if (args->version != CURRENT_KERNEL_ARGS_VERSION) {
        dprintf("Kernel args version mismatch: got %u, expected %u\n", 
                args->version, CURRENT_KERNEL_ARGS_VERSION);
        return B_BAD_VERSION;
    }
    
    // Validate memory ranges
    if (args->num_physical_memory_ranges == 0) {
        dprintf("No physical memory ranges defined\n");
        return B_BAD_DATA;
    }
    
    if (args->num_physical_memory_ranges > MAX_PHYSICAL_MEMORY_RANGE) {
        dprintf("Too many physical memory ranges: %u\n", 
                args->num_physical_memory_ranges);
        return B_BAD_DATA;
    }
    
    // Validate at least one CPU is defined
    if (args->num_cpus == 0) {
        dprintf("No CPUs defined in kernel args\n");
        return B_BAD_DATA;
    }
    
    dprintf("Kernel args validation passed\n");
    return B_OK;
}

/*
 * Set up early debug output
 */
static status_t
setup_early_debug_output(kernel_args* args)
{
    // Initialize early serial output if available
    // TODO: Implement early serial debug output
    
    // For now, assume debug output is available
    return B_OK;
}

/*
 * Validate device tree blob
 */
static status_t
validate_device_tree(kernel_args* args)
{
    void* fdt = (void*)args->arch_args.fdt;
    
    if (fdt == NULL) {
        dprintf("No device tree provided\n");
        return B_ENTRY_NOT_FOUND;
    }
    
    // Check FDT magic number
    uint32_t magic = *((uint32_t*)fdt);
    // Convert from big-endian to host byte order
    magic = __builtin_bswap32(magic);
    
    if (magic != 0xd00dfeed) {
        dprintf("Invalid device tree magic: 0x%08x (expected 0xd00dfeed)\n", magic);
        return B_BAD_DATA;
    }
    
    // Basic size validation
    uint32_t* fdt_header = (uint32_t*)fdt;
    uint32_t total_size = __builtin_bswap32(fdt_header[1]);  // totalsize field
    
    if (total_size < 64 || total_size > 2 * 1024 * 1024) {  // Max 2MB
        dprintf("Invalid device tree size: %u bytes\n", total_size);
        return B_BAD_DATA;
    }
    
    dprintf("Device tree validation passed: %u bytes at %p\n", total_size, fdt);
    return B_OK;
}

/*
 * Print detailed boot validation information
 */
static void
print_boot_validation_info(kernel_args* args)
{
    dprintf("=== ARM64 Boot Validation Information ===\n");
    
    // Extract boot validation data
    uint64* boot_data = (uint64*)((char*)args + 0x108);
    uint64 original_el = boot_data[0];
    uint64 original_daif = boot_data[1];
    uint64 warning_flags = boot_data[2];
    uint64 midr = boot_data[3];
    uint64 mpidr = boot_data[4];
    
    // Print exception level information
    uint64 el = (original_el >> 2) & 0x3;
    dprintf("Boot Exception Level: EL%lu\n", el);
    
    switch (el) {
        case 1:
            dprintf("  Entered directly at EL1 (kernel level)\n");
            break;
        case 2:
            dprintf("  Entered at EL2 (hypervisor level), transitioned to EL1\n");
            break;
        default:
            dprintf("  Unexpected exception level: %lu\n", el);
            break;
    }
    
    // Print interrupt mask state
    dprintf("Boot DAIF State: 0x%02lx\n", (original_daif >> 4) & 0xf);
    if ((original_daif & 0xf0) == 0xf0) {
        dprintf("  All interrupts properly masked at boot\n");
    } else {
        dprintf("  WARNING: Interrupts not fully masked at boot\n");
    }
    
    // Print any warnings
    if (warning_flags != 0) {
        dprintf("Boot Warnings: 0x%016lx\n", warning_flags);
        if ((warning_flags >> 16) == 0x1NT0) {
            dprintf("  Interrupt mask warning detected\n");
        }
    } else {
        dprintf("No boot warnings detected\n");
    }
    
    // Print memory information
    dprintf("Physical Memory Ranges: %u\n", args->num_physical_memory_ranges);
    for (uint32_t i = 0; i < args->num_physical_memory_ranges && i < 8; i++) {
        dprintf("  Range %u: 0x%016lx - 0x%016lx (%lu MB)\n", 
                i,
                args->physical_memory_range[i].start,
                args->physical_memory_range[i].start + args->physical_memory_range[i].size,
                args->physical_memory_range[i].size / (1024 * 1024));
    }
    
    // Print CPU information
    dprintf("Number of CPUs: %u\n", args->num_cpus);
    dprintf("Boot CPU Stack: 0x%016lx - 0x%016lx (%lu KB)\n",
            args->cpu_kstack[0].start,
            args->cpu_kstack[0].start + args->cpu_kstack[0].size,
            args->cpu_kstack[0].size / 1024);
    
    // Print device tree information
    if (args->arch_args.fdt != NULL) {
        dprintf("Device Tree: %p\n", (void*)args->arch_args.fdt);
    } else {
        dprintf("No device tree provided\n");
    }
    
    dprintf("=== End Boot Validation Information ===\n");
}

/*
 * CPU Feature Detection Functions
 */
static void
arch_cpu_detect_features(arch_cpu_info* cpu_info)
{
    // Read feature registers
    uint64_t id_aa64pfr0 = arm64_read_id_aa64pfr0_el1();
    uint64_t id_aa64isar0 = arm64_read_id_aa64isar0_el1();
    uint64_t id_aa64isar1 = arm64_read_id_aa64isar1_el1();
    uint64_t id_aa64mmfr0 = arm64_read_id_aa64mmfr0_el1();
    
    // Detect crypto features
    uint32_t aes = (id_aa64isar0 >> 4) & 0xf;
    uint32_t sha1 = (id_aa64isar0 >> 8) & 0xf;
    uint32_t sha256 = (id_aa64isar0 >> 12) & 0xf;
    uint32_t sha512 = (id_aa64isar0 >> 21) & 0xf;
    
    cpu_info->crypto_features.aes = (aes >= 1);
    cpu_info->crypto_features.sha1 = (sha1 >= 1);
    cpu_info->crypto_features.sha256 = (sha256 >= 1);
    cpu_info->crypto_features.sha512 = (sha512 >= 1);
    cpu_info->crypto_features.pmull = (aes >= 2);  // PMULL requires AES >= 2
    
    // Detect pointer authentication
    uint32_t pauth_addr = (id_aa64isar1 >> 4) & 0xf;
    uint32_t pauth_generic = (id_aa64isar1 >> 8) & 0xf;
    
    cpu_info->pauth_features.address_auth = (pauth_addr != 0);
    cpu_info->pauth_features.generic_auth = (pauth_generic != 0);
    cpu_info->pauth_features.num_keys = 5;  // ARM64 typically has 5 keys
    
    // Detect other features
    uint32_t fp = id_aa64pfr0 & 0xf;
    uint32_t asimd = (id_aa64pfr0 >> 4) & 0xf;
    uint32_t sve = (id_aa64pfr0 >> 32) & 0xf;
    
    cpu_info->has_fp = (fp != 0xf);
    cpu_info->has_asimd = (asimd != 0xf);
    cpu_info->has_sve = (sve != 0);
    
    dprintf("CPU Features: FP=%s ASIMD=%s SVE=%s\n",
            cpu_info->has_fp ? "yes" : "no",
            cpu_info->has_asimd ? "yes" : "no", 
            cpu_info->has_sve ? "yes" : "no");
    
    dprintf("Crypto Features: AES=%s SHA1=%s SHA256=%s SHA512=%s PMULL=%s\n",
            cpu_info->crypto_features.aes ? "yes" : "no",
            cpu_info->crypto_features.sha1 ? "yes" : "no",
            cpu_info->crypto_features.sha256 ? "yes" : "no",
            cpu_info->crypto_features.sha512 ? "yes" : "no",
            cpu_info->crypto_features.pmull ? "yes" : "no");
}

/*
 * Inline assembly functions for reading system registers
 */
static inline uint64_t arm64_read_midr_el1(void)
{
    uint64_t value;
    asm volatile("mrs %0, midr_el1" : "=r"(value));
    return value;
}

static inline uint64_t arm64_read_mpidr_el1(void)
{
    uint64_t value;
    asm volatile("mrs %0, mpidr_el1" : "=r"(value));
    return value;
}

static inline uint64_t arm64_read_revidr_el1(void)
{
    uint64_t value;
    asm volatile("mrs %0, revidr_el1" : "=r"(value));
    return value;
}

static inline uint64_t arm64_read_id_aa64pfr0_el1(void)
{
    uint64_t value;
    asm volatile("mrs %0, id_aa64pfr0_el1" : "=r"(value));
    return value;
}

static inline uint64_t arm64_read_id_aa64isar0_el1(void)
{
    uint64_t value;
    asm volatile("mrs %0, id_aa64isar0_el1" : "=r"(value));
    return value;
}

static inline uint64_t arm64_read_id_aa64isar1_el1(void)
{
    uint64_t value;
    asm volatile("mrs %0, id_aa64isar1_el1" : "=r"(value));
    return value;
}

static inline uint64_t arm64_read_id_aa64mmfr0_el1(void)
{
    uint64_t value;
    asm volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(value));
    return value;
}

/*
 * Debug panic function for early boot failures
 */
extern "C" void
arch_debug_panic(const char* message)
{
    // Disable interrupts
    asm volatile("msr daifset, #0xf");
    
    // Try to output message if possible
    dprintf("KERNEL PANIC: %s\n", message);
    
    // Store panic message address for debugging
    arm64_boot_info[7] = (uint64)message;
    
    // Halt with wait-for-event
    while (true) {
        asm volatile("wfe");
    }
}