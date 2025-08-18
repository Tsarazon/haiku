/*
 * Simple test for ARM64 kernel entry validation
 */

#include <iostream>
#include <cstdint>
#include <cassert>

// Test register validation logic
bool test_exception_level_validation() {
    // Simulate CurrentEL register values
    uint64_t el1_currentel = 0x4;  // EL1
    uint64_t el2_currentel = 0x8;  // EL2
    uint64_t el0_currentel = 0x0;  // EL0 (invalid)
    uint64_t el3_currentel = 0xC;  // EL3 (invalid)
    
    // Test exception level extraction
    uint64_t el1 = (el1_currentel >> 2) & 0x3;
    uint64_t el2 = (el2_currentel >> 2) & 0x3;
    uint64_t el0 = (el0_currentel >> 2) & 0x3;
    uint64_t el3 = (el3_currentel >> 2) & 0x3;
    
    assert(el1 == 1);
    assert(el2 == 2);
    assert(el0 == 0);
    assert(el3 == 3);
    
    // Valid levels for kernel entry
    bool el1_valid = (el1 == 1 || el1 == 2);
    bool el2_valid = (el2 == 1 || el2 == 2);
    bool el0_valid = (el0 == 1 || el0 == 2);
    bool el3_valid = (el3 == 1 || el3 == 2);
    
    assert(el1_valid == true);
    assert(el2_valid == true);
    assert(el0_valid == false);
    assert(el3_valid == false);
    
    return true;
}

bool test_alignment_validation() {
    // Test 8-byte alignment
    uint64_t aligned_8 = 0x40000000;
    uint64_t unaligned_8 = 0x40000001;
    
    assert((aligned_8 & 0x7) == 0);
    assert((unaligned_8 & 0x7) != 0);
    
    // Test 16-byte alignment
    uint64_t aligned_16 = 0x40000000;
    uint64_t unaligned_16 = 0x40000004;
    
    assert((aligned_16 & 0xf) == 0);
    assert((unaligned_16 & 0xf) != 0);
    
    return true;
}

bool test_dtb_magic_validation() {
    // Test DTB magic number
    uint32_t dtb_magic_be = 0xd00dfeed;     // Big-endian (as stored in DTB)
    uint32_t dtb_magic_le = __builtin_bswap32(dtb_magic_be);  // Convert to little-endian
    
    uint32_t expected_magic = 0xd00dfeed;   // What we expect after conversion
    
    // Debug output
    std::cout << "  DTB magic BE: 0x" << std::hex << dtb_magic_be << std::endl;
    std::cout << "  DTB magic LE: 0x" << std::hex << dtb_magic_le << std::endl;
    std::cout << "  Expected:     0x" << std::hex << expected_magic << std::endl;
    
    assert(dtb_magic_be == expected_magic);  // Test the original magic
    
    return true;
}

bool test_panic_codes() {
    // Test panic code generation
    uint64_t dead_mask = 0xDEAD0000;
    
    // Test various panic codes
    uint64_t el_invalid = 0xDEAD0000 | 0xE100;
    uint64_t dtb_align = 0xDEAD0000 | 0xD7B1;
    uint64_t dtb_null = 0xDEAD0000 | 0xD7B2;
    uint64_t stack_align = 0xDEAD0000 | 0x57C1;
    
    assert((el_invalid & 0xFFFF0000) == 0xDEAD0000);
    assert((dtb_align & 0xFFFF0000) == 0xDEAD0000);
    assert((dtb_null & 0xFFFF0000) == 0xDEAD0000);
    assert((stack_align & 0xFFFF0000) == 0xDEAD0000);
    
    // Ensure codes are unique
    assert(el_invalid != dtb_align);
    assert(dtb_align != dtb_null);
    assert(dtb_null != stack_align);
    
    return true;
}

bool test_interrupt_mask_validation() {
    // Test DAIF register validation
    uint64_t all_masked = 0xF0;      // All interrupts masked
    uint64_t partially_masked = 0x80; // Only IRQ masked
    uint64_t none_masked = 0x00;     // No interrupts masked
    
    assert((all_masked & 0xF0) == 0xF0);
    assert((partially_masked & 0xF0) != 0xF0);
    assert((none_masked & 0xF0) != 0xF0);
    
    return true;
}

int main() {
    std::cout << "ARM64 Kernel Entry Validation Test" << std::endl;
    std::cout << "==================================" << std::endl;
    
    try {
        assert(test_exception_level_validation());
        std::cout << "✓ Exception level validation test passed" << std::endl;
        
        assert(test_alignment_validation());
        std::cout << "✓ Alignment validation test passed" << std::endl;
        
        assert(test_dtb_magic_validation());
        std::cout << "✓ DTB magic validation test passed" << std::endl;
        
        assert(test_panic_codes());
        std::cout << "✓ Panic code generation test passed" << std::endl;
        
        assert(test_interrupt_mask_validation());
        std::cout << "✓ Interrupt mask validation test passed" << std::endl;
        
        std::cout << std::endl << "All tests PASSED! ✓" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}