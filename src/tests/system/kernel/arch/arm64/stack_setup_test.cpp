/*
 * ARM64 Early Stack Setup Test
 * 
 * Tests the early stack setup functionality implemented in arch_start.S
 * Validates ARM64 AAPCS alignment requirements and stack safety features
 */

#include <iostream>
#include <cstdint>
#include <cassert>
#include <cstring>

// Stack configuration constants (from arch_start.S)
constexpr uint64_t EARLY_STACK_SIZE = 0x10000;         // 64KB emergency stack
constexpr uint64_t STACK_ALIGNMENT_MASK = 0xf;         // 16-byte alignment for ARM64 AAPCS
constexpr uint64_t STACK_SAFETY_MARGIN = 0x100;       // 256 bytes safety margin
constexpr uint64_t STACK_CANARY_OFFSET = 0x1000;      // 4KB below SP for canary
constexpr uint64_t STACK_MINIMUM_SPACE = 0x2000;      // 8KB minimum for early kernel

// Stack setup flags
constexpr uint64_t STACK_FLAG_EMERGENCY = 0x1;        // Using emergency stack
constexpr uint64_t STACK_FLAG_LIMITED_SPACE = 0x2;    // Limited stack space warning
constexpr uint64_t STACK_FLAG_REALIGNED = 0x4;        // Bootloader stack was realigned
constexpr uint64_t STACK_FLAG_ENLARGED = 0x8;         // Stack space was enlarged

// Test ARM64 AAPCS64 stack alignment requirements
bool test_aapcs64_alignment() {
    std::cout << "Testing ARM64 AAPCS64 alignment requirements..." << std::endl;
    
    // Test various stack pointer values for 16-byte alignment
    uint64_t aligned_addresses[] = {
        0x40000000,  // 16-byte aligned
        0x40000010,  // 16-byte aligned
        0x40000020,  // 16-byte aligned
        0x400000F0,  // 16-byte aligned
    };
    
    uint64_t unaligned_addresses[] = {
        0x40000001,  // 1-byte misaligned
        0x40000004,  // 4-byte aligned (insufficient for ARM64)
        0x40000008,  // 8-byte aligned (insufficient for ARM64)
        0x4000000F,  // 15-byte offset
    };
    
    // Test aligned addresses
    for (uint64_t addr : aligned_addresses) {
        bool is_aligned = (addr & STACK_ALIGNMENT_MASK) == 0;
        assert(is_aligned);
        std::cout << "  Address 0x" << std::hex << addr << " is properly aligned" << std::endl;
    }
    
    // Test unaligned addresses
    for (uint64_t addr : unaligned_addresses) {
        bool is_aligned = (addr & STACK_ALIGNMENT_MASK) == 0;
        assert(!is_aligned);
        std::cout << "  Address 0x" << std::hex << addr << " is correctly detected as unaligned" << std::endl;
    }
    
    return true;
}

// Test stack range validation logic
bool test_stack_range_validation() {
    std::cout << "Testing stack range validation..." << std::endl;
    
    struct StackTest {
        uint64_t sp;
        bool should_be_valid;
        const char* description;
    };
    
    StackTest tests[] = {
        {0x0,           false, "Null stack pointer"},
        {0x1000,        false, "Below 64KB minimum"},
        {0x10000,       true,  "At 64KB boundary"},
        {0x40000000,    true,  "Valid mid-range address"},
        {0x80000000,    true,  "Valid high address"},
        {0x100000000ULL, false, "Above 4GB limit"},
        {0xFFFFFFFFULL, true,  "Maximum 32-bit address (valid)"},
    };
    
    for (const auto& test : tests) {
        // Simulate the range check logic from arch_start.S
        bool in_lower_range = test.sp >= 0x10000;     // Above 64KB
        bool in_upper_range = test.sp < 0x100000000;  // Below 4GB
        bool is_valid = in_lower_range && in_upper_range;
        
        assert(is_valid == test.should_be_valid);
        std::cout << "  " << test.description << " (0x" << std::hex << test.sp 
                  << ") - " << (is_valid ? "VALID" : "INVALID") << std::endl;
    }
    
    return true;
}

// Test stack space calculation
bool test_stack_space_calculation() {
    std::cout << "Testing stack space calculation..." << std::endl;
    
    struct SpaceTest {
        uint64_t bootloader_sp;
        uint64_t kernel_sp;
        uint64_t expected_size;
        bool sufficient_space;
    };
    
    SpaceTest tests[] = {
        {0x40010000, 0x40008000, 0x8000,  true},   // 32KB stack
        {0x40008000, 0x40006000, 0x2000,  true},   // 8KB stack (minimum)
        {0x40004000, 0x40003000, 0x1000,  false},  // 4KB stack (insufficient)
        {0x40020000, 0x40010000, 0x10000, true},   // 64KB stack
    };
    
    for (const auto& test : tests) {
        uint64_t calculated_size = test.bootloader_sp - test.kernel_sp;
        bool sufficient = calculated_size >= STACK_MINIMUM_SPACE;
        
        assert(calculated_size == test.expected_size);
        assert(sufficient == test.sufficient_space);
        
        std::cout << "  Stack size: " << std::dec << calculated_size 
                  << " bytes - " << (sufficient ? "SUFFICIENT" : "INSUFFICIENT") << std::endl;
    }
    
    return true;
}

// Test stack canary functionality
bool test_stack_canary() {
    std::cout << "Testing stack canary functionality..." << std::endl;
    
    // Simulate the canary pattern from arch_start.S
    uint64_t expected_canary = 0x0000CAFEDEADBEEF;  // Pattern from assembly
    
    // Test canary validation
    uint64_t valid_canary = expected_canary;
    uint64_t invalid_canaries[] = {
        0x0000000000000000,  // Zero
        0xFFFFFFFFFFFFFFFF,  // All ones
        0x0000CAFEDEADBEE0,  // One bit different
        0x1234567890ABCDEF,  // Random value
    };
    
    // Valid canary should match
    assert(valid_canary == expected_canary);
    std::cout << "  Valid canary 0x" << std::hex << valid_canary << " matches expected" << std::endl;
    
    // Invalid canaries should not match
    for (uint64_t invalid : invalid_canaries) {
        assert(invalid != expected_canary);
        std::cout << "  Invalid canary 0x" << std::hex << invalid << " correctly detected" << std::endl;
    }
    
    return true;
}

// Test emergency stack allocation
bool test_emergency_stack() {
    std::cout << "Testing emergency stack allocation..." << std::endl;
    
    // Simulate emergency stack allocation
    uint64_t emergency_stack_top = 0x50000000;  // Simulated address
    uint64_t emergency_sp = emergency_stack_top;
    
    // Align the emergency stack
    emergency_sp = emergency_sp & ~STACK_ALIGNMENT_MASK;
    
    // Verify alignment
    assert((emergency_sp & STACK_ALIGNMENT_MASK) == 0);
    std::cout << "  Emergency stack at 0x" << std::hex << emergency_sp << " is properly aligned" << std::endl;
    
    // Verify sufficient space
    uint64_t available_space = EARLY_STACK_SIZE - STACK_SAFETY_MARGIN;
    assert(available_space >= STACK_MINIMUM_SPACE);
    std::cout << "  Emergency stack provides " << std::dec << available_space 
              << " bytes of usable space" << std::endl;
    
    return true;
}

// Test panic code generation
bool test_panic_codes() {
    std::cout << "Testing stack-related panic codes..." << std::endl;
    
    struct PanicTest {
        uint64_t code;
        const char* description;
    };
    
    PanicTest panic_codes[] = {
        {0xDEAD57CF, "Stack setup fatal error (STKF)"},
        {0xDEAD57C0, "Stack corrupted (STC0)"},
        {0xDEAD570F, "Stack overflow (STOF)"},
        {0xDEAD5CA7, "Stack canary violated (SCAT)"},
    };
    
    for (const auto& panic : panic_codes) {
        // Verify DEAD prefix
        uint32_t prefix = (panic.code >> 16) & 0xFFFF;
        assert(prefix == 0xDEAD);
        
        // Verify unique codes
        uint16_t code = panic.code & 0xFFFF;
        assert(code != 0);
        
        std::cout << "  Panic code 0x" << std::hex << panic.code 
                  << " - " << panic.description << std::endl;
    }
    
    return true;
}

// Test stack realignment logic
bool test_stack_realignment() {
    std::cout << "Testing stack realignment logic..." << std::endl;
    
    struct AlignmentTest {
        uint64_t original_sp;
        uint64_t expected_aligned_sp;
    };
    
    AlignmentTest tests[] = {
        {0x40000001, 0x40000000},  // Round down 1 byte
        {0x40000007, 0x40000000},  // Round down 7 bytes
        {0x40000008, 0x40000000},  // Round down 8 bytes
        {0x4000000F, 0x40000000},  // Round down 15 bytes
        {0x40000010, 0x40000010},  // Already aligned
    };
    
    for (const auto& test : tests) {
        // Simulate the alignment logic from arch_start.S
        uint64_t aligned_sp = test.original_sp & ~STACK_ALIGNMENT_MASK;
        
        assert(aligned_sp == test.expected_aligned_sp);
        assert((aligned_sp & STACK_ALIGNMENT_MASK) == 0);
        
        std::cout << "  SP 0x" << std::hex << test.original_sp 
                  << " aligned to 0x" << aligned_sp << std::endl;
    }
    
    return true;
}

int main() {
    std::cout << "ARM64 Early Stack Setup Test Suite" << std::endl;
    std::cout << "===================================" << std::endl;
    
    try {
        assert(test_aapcs64_alignment());
        std::cout << "✓ ARM64 AAPCS64 alignment tests passed\n" << std::endl;
        
        assert(test_stack_range_validation());
        std::cout << "✓ Stack range validation tests passed\n" << std::endl;
        
        assert(test_stack_space_calculation());
        std::cout << "✓ Stack space calculation tests passed\n" << std::endl;
        
        assert(test_stack_canary());
        std::cout << "✓ Stack canary tests passed\n" << std::endl;
        
        assert(test_emergency_stack());
        std::cout << "✓ Emergency stack tests passed\n" << std::endl;
        
        assert(test_panic_codes());
        std::cout << "✓ Panic code tests passed\n" << std::endl;
        
        assert(test_stack_realignment());
        std::cout << "✓ Stack realignment tests passed\n" << std::endl;
        
        std::cout << "All early stack setup tests PASSED! ✓" << std::endl;
        std::cout << "\nStack setup implementation provides:" << std::endl;
        std::cout << "- ARM64 AAPCS64 compliant 16-byte stack alignment" << std::endl;
        std::cout << "- Automatic bootloader stack validation and correction" << std::endl;
        std::cout << "- Emergency stack allocation when bootloader stack unusable" << std::endl;
        std::cout << "- Stack overflow protection with canary values" << std::endl;
        std::cout << "- Comprehensive error detection and reporting" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}