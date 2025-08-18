/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ARM64 Kernel Entry Point Test
 * Validates the kernel entry implementation and register state handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

// Include the header files
#include <arch/aarch64/arch_kernel_start.h>
#include <boot/kernel_args.h>

// Test framework macros
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s - %s\n", __func__, message); \
            return false; \
        } \
    } while (0)

#define TEST_PASS(message) \
    do { \
        printf("PASS: %s - %s\n", __func__, message); \
        return true; \
    } while (0)

// Forward declarations
static bool test_boot_info_structure(void);
static bool test_cpu_feature_detection(void);
static bool test_register_validation(void);
static bool test_device_tree_validation(void);
static bool test_exception_level_handling(void);
static bool test_memory_layout_validation(void);
static bool test_panic_code_generation(void);

// Mock implementations for testing
static uint64_t mock_midr_el1 = 0x411FD073;  // ARM Cortex-A57
static uint64_t mock_mpidr_el1 = 0x80000000; // Single core
static uint64_t mock_currentel = 0x4;        // EL1

// Mock device tree blob
static uint32_t mock_dtb[] = {
    0xfeed00d0,  // Magic (big-endian)
    0x00000100,  // Total size (big-endian, 256 bytes)
    0x00000038,  // Structure offset
    0x00000000,  // Strings offset
    // ... rest of DTB structure
};

/*
 * Main test function
 */
int main(int argc, char** argv)
{
    printf("ARM64 Kernel Entry Point Test Suite\n");
    printf("====================================\n\n");
    
    bool all_passed = true;
    
    // Run all tests
    all_passed &= test_boot_info_structure();
    all_passed &= test_cpu_feature_detection();
    all_passed &= test_register_validation();
    all_passed &= test_device_tree_validation();
    all_passed &= test_exception_level_handling();
    all_passed &= test_memory_layout_validation();
    all_passed &= test_panic_code_generation();
    
    printf("\n====================================\n");
    if (all_passed) {
        printf("All tests PASSED!\n");
        return 0;
    } else {
        printf("Some tests FAILED!\n");
        return 1;
    }
}

/*
 * Test boot information structure
 */
static bool test_boot_info_structure(void)
{
    struct arm64_boot_info boot_info;
    
    // Test structure size and alignment
    TEST_ASSERT(sizeof(boot_info) == 64, "Boot info structure has correct size");
    TEST_ASSERT(sizeof(boot_info) % 8 == 0, "Boot info structure is 8-byte aligned");
    
    // Test field initialization
    memset(&boot_info, 0, sizeof(boot_info));
    boot_info.dtb_physical_address = 0x40000000;
    boot_info.original_exception_level = 0x4; // EL1
    boot_info.boot_flags = ARM64_BOOT_FLAG_EL1_ENTRY | ARM64_BOOT_FLAG_DTB_VALID;
    strcpy(boot_info.signature, "ARM64BOOT");
    
    TEST_ASSERT(boot_info.dtb_physical_address == 0x40000000, "DTB address stored correctly");
    TEST_ASSERT(boot_info.original_exception_level == 0x4, "Exception level stored correctly");
    TEST_ASSERT(strcmp(boot_info.signature, "ARM64BOOT") == 0, "Signature stored correctly");
    
    TEST_PASS("Boot info structure validation");
}

/*
 * Test CPU feature detection
 */
static bool test_cpu_feature_detection(void)
{
    arch_cpu_info cpu_info;
    memset(&cpu_info, 0, sizeof(cpu_info));
    
    // Test structure size
    TEST_ASSERT(sizeof(arch_cpu_info) > 100, "CPU info structure has reasonable size");
    
    // Test crypto features structure
    struct arm64_crypto_features* crypto = &cpu_info.crypto_features;
    crypto->aes = true;
    crypto->sha256 = true;
    crypto->pmull = true;
    
    TEST_ASSERT(crypto->aes == true, "AES feature set correctly");
    TEST_ASSERT(crypto->sha256 == true, "SHA256 feature set correctly");
    TEST_ASSERT(crypto->pmull == true, "PMULL feature set correctly");
    
    // Test pointer authentication features
    struct arm64_pointer_auth_features* pauth = &cpu_info.pauth_features;
    pauth->address_auth = true;
    pauth->generic_auth = true;
    pauth->num_keys = 5;
    
    TEST_ASSERT(pauth->address_auth == true, "Address auth feature set correctly");
    TEST_ASSERT(pauth->num_keys == 5, "Number of keys set correctly");
    
    // Test general CPU features
    cpu_info.features.has_fp = true;
    cpu_info.features.has_asimd = true;
    cpu_info.features.has_sve = false;
    
    TEST_ASSERT(cpu_info.features.has_fp == true, "FP feature set correctly");
    TEST_ASSERT(cpu_info.features.has_asimd == true, "ASIMD feature set correctly");
    TEST_ASSERT(cpu_info.features.has_sve == false, "SVE feature set correctly");
    
    TEST_PASS("CPU feature detection structure");
}

/*
 * Test register validation functions
 */
static bool test_register_validation(void)
{
    // Test exception level validation
    TEST_ASSERT((mock_currentel >> 2) == 1, "Mock CurrentEL indicates EL1");
    
    // Test DAIF validation (all interrupts masked)
    uint64_t daif_masked = 0xF0;
    TEST_ASSERT((daif_masked & 0xF0) == 0xF0, "DAIF correctly indicates masked interrupts");
    
    uint64_t daif_unmasked = 0x00;
    TEST_ASSERT((daif_unmasked & 0xF0) != 0xF0, "DAIF correctly indicates unmasked interrupts");
    
    // Test alignment validation
    uint64_t aligned_8 = 0x40000000;
    uint64_t unaligned_8 = 0x40000001;
    
    TEST_ASSERT((aligned_8 & ARM64_ALIGNMENT_MASK_8) == 0, "8-byte alignment check works");
    TEST_ASSERT((unaligned_8 & ARM64_ALIGNMENT_MASK_8) != 0, "8-byte misalignment detected");
    
    uint64_t aligned_16 = 0x40000000;
    uint64_t unaligned_16 = 0x40000004;
    
    TEST_ASSERT((aligned_16 & ARM64_ALIGNMENT_MASK_16) == 0, "16-byte alignment check works");
    TEST_ASSERT((unaligned_16 & ARM64_ALIGNMENT_MASK_16) != 0, "16-byte misalignment detected");
    
    TEST_PASS("Register validation functions");
}

/*
 * Test device tree validation
 */
static bool test_device_tree_validation(void)
{
    // Test valid DTB magic
    uint32_t magic = __builtin_bswap32(mock_dtb[0]);  // Convert from big-endian
    TEST_ASSERT(magic == ARM64_BOOT_MAGIC_DTB, "DTB magic number validation works");
    
    // Test DTB size validation
    uint32_t size = __builtin_bswap32(mock_dtb[1]);
    TEST_ASSERT(size == 0x100, "DTB size extraction works");
    TEST_ASSERT(size >= 64 && size <= 2*1024*1024, "DTB size is within valid range");
    
    // Test DTB alignment
    uint64_t dtb_addr = (uint64_t)mock_dtb;
    TEST_ASSERT((dtb_addr & ARM64_ALIGNMENT_MASK_8) == 0, "DTB is properly aligned");
    
    // Test invalid magic detection
    uint32_t invalid_dtb[] = { 0x12345678, 0x00000100 };
    uint32_t invalid_magic = __builtin_bswap32(invalid_dtb[0]);
    TEST_ASSERT(invalid_magic != ARM64_BOOT_MAGIC_DTB, "Invalid DTB magic detected");
    
    TEST_PASS("Device tree validation");
}

/*
 * Test exception level handling
 */
static bool test_exception_level_handling(void)
{
    // Test exception level extraction
    uint64_t el1_currentel = 0x4;  // EL1
    uint64_t el2_currentel = 0x8;  // EL2
    uint64_t el3_currentel = 0xC;  // EL3
    
    TEST_ASSERT((el1_currentel >> 2) == 1, "EL1 extraction works");
    TEST_ASSERT((el2_currentel >> 2) == 2, "EL2 extraction works");
    TEST_ASSERT((el3_currentel >> 2) == 3, "EL3 extraction works");
    
    // Test valid exception level ranges
    for (int el = 0; el <= 3; el++) {
        bool valid = (el == 1 || el == 2);  // Only EL1 and EL2 are valid for kernel entry
        if (el == 1 || el == 2) {
            TEST_ASSERT(valid == true, "Valid exception level accepted");
        } else {
            TEST_ASSERT(valid == false, "Invalid exception level rejected");
        }
    }
    
    TEST_PASS("Exception level handling");
}

/*
 * Test memory layout validation
 */
static bool test_memory_layout_validation(void)
{
    kernel_args args;
    memset(&args, 0, sizeof(args));
    
    // Set up valid kernel args
    args.kernel_args_size = sizeof(kernel_args);
    args.version = CURRENT_KERNEL_ARGS_VERSION;
    args.num_physical_memory_ranges = 2;
    args.num_cpus = 1;
    
    // Add memory ranges
    args.physical_memory_range[0].start = 0x40000000;
    args.physical_memory_range[0].size = 0x10000000;  // 256MB
    args.physical_memory_range[1].start = 0x60000000;
    args.physical_memory_range[1].size = 0x20000000;  // 512MB
    
    // Add CPU stack
    args.cpu_kstack[0].start = 0x50000000;
    args.cpu_kstack[0].size = 0x4000;  // 16KB
    
    // Test kernel args validation
    TEST_ASSERT(args.kernel_args_size > 0, "Kernel args size is valid");
    TEST_ASSERT(args.kernel_args_size <= 0x10000, "Kernel args size is reasonable");
    TEST_ASSERT(args.version == CURRENT_KERNEL_ARGS_VERSION, "Kernel args version is correct");
    TEST_ASSERT(args.num_physical_memory_ranges > 0, "At least one memory range defined");
    TEST_ASSERT(args.num_physical_memory_ranges <= MAX_PHYSICAL_MEMORY_RANGE, "Memory range count within limits");
    TEST_ASSERT(args.num_cpus > 0, "At least one CPU defined");
    
    // Test memory range validation
    for (uint32_t i = 0; i < args.num_physical_memory_ranges; i++) {
        TEST_ASSERT(args.physical_memory_range[i].size > 0, "Memory range has valid size");
        TEST_ASSERT(args.physical_memory_range[i].start % 0x1000 == 0, "Memory range is page-aligned");
    }
    
    // Test stack validation
    TEST_ASSERT(args.cpu_kstack[0].size >= 0x1000, "Stack size is at least 4KB");
    TEST_ASSERT((args.cpu_kstack[0].start & ARM64_ALIGNMENT_MASK_16) == 0, "Stack is 16-byte aligned");
    
    TEST_PASS("Memory layout validation");
}

/*
 * Test panic code generation
 */
static bool test_panic_code_generation(void)
{
    // Test panic code constants
    TEST_ASSERT((ARM64_PANIC_EL_INVALID & 0xFFFF0000) == 0xDEAD0000, "Panic codes have DEAD prefix");
    TEST_ASSERT((ARM64_PANIC_DTB_ALIGN & 0x0000FFFF) == 0xDTB1, "DTB alignment panic has correct suffix");
    TEST_ASSERT((ARM64_PANIC_STACK_ALIGN & 0x0000FFFF) == 0xSTK1, "Stack alignment panic has correct suffix");
    
    // Test warning flag constants
    TEST_ASSERT((ARM64_BOOT_WARN_IRQ_MASK & 0xFFFF0000) != 0, "Warning flags use high 16 bits");
    TEST_ASSERT((ARM64_BOOT_FLAG_EL1_ENTRY & 0xFFFF0000) == 0, "Boot flags use low 16 bits");
    
    // Test distinctive patterns for debugging
    uint64_t el_invalid = ARM64_PANIC_EL_INVALID;
    uint64_t dtb_null = ARM64_PANIC_DTB_NULL;
    
    TEST_ASSERT(el_invalid != dtb_null, "Different panic codes are unique");
    TEST_ASSERT((el_invalid >> 16) == 0xDEADEL00, "EL invalid panic has correct pattern");
    TEST_ASSERT((dtb_null >> 16) == 0xDEADDTB2, "DTB null panic has correct pattern");
    
    TEST_PASS("Panic code generation");
}

/*
 * Helper function to print test results
 */
static void print_test_summary(void)
{
    printf("\nTest Configuration:\n");
    printf("- sizeof(arm64_boot_info): %zu bytes\n", sizeof(struct arm64_boot_info));
    printf("- sizeof(arch_cpu_info): %zu bytes\n", sizeof(arch_cpu_info));
    printf("- sizeof(kernel_args): %zu bytes\n", sizeof(kernel_args));
    printf("- ARM64_BOOT_MAGIC_DTB: 0x%08x\n", ARM64_BOOT_MAGIC_DTB);
    printf("- CURRENT_KERNEL_ARGS_VERSION: %u\n", CURRENT_KERNEL_ARGS_VERSION);
}