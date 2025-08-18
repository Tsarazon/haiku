# ARM64 Kernel Architecture Test

This directory contains tests to verify that ARM64 kernel components can be compiled and that basic header inclusion works correctly.

## Files

- `arch_test.cpp` - Main test program that validates ARM64 kernel structures and constants
- `Jamfile` - Build configuration for the ARM64 architecture test
- `validate_arm64.sh` - Validation script to check component availability
- `README.md` - This documentation file

## Test Coverage

The test program validates:

1. **Header Inclusion**
   - `arch/aarch64/arch_config.h` - ARM64 architectural constants
   - `arch/arm64/arch_cpu.h` - CPU information structures
   - `arch/arm64/arch_thread_types.h` - Thread context structures

2. **Structure Validation**
   - `arch_thread` - Thread context for context switching
   - `arm64_register_context` - Complete register context
   - `arch_cpu_info` - CPU identification and features
   - Structure sizes and alignment requirements

3. **Constant Verification**
   - Page size options (4KB, 16KB, 64KB)
   - Cache line size (64 bytes)
   - ARM64 architectural parameters
   - CPU implementer and feature flags

4. **Function Declaration Tests**
   - Context switching functions
   - Exception handling stubs
   - Memory barrier functions
   - FPU/SIMD state management

## Building

To build the test with the Haiku build system:

```bash
# From the Haiku source root
jam arm64_arch_test
```

To run the validation script:

```bash
cd src/tests/system/kernel/arch/arm64
./validate_arm64.sh
```

## Expected Results

When running successfully, the test should:
- Compile without errors
- Print detailed information about ARM64 structures
- Validate all constants and feature flags
- Confirm assembly function availability
- Display structure sizes and alignment

## ARM64 Features Tested

- **General-Purpose Registers**: X0-X30, SP, PC
- **Processor State**: PSTATE, exception levels
- **Floating-Point/SIMD**: V0-V31 vector registers, FPCR, FPSR
- **Cryptographic Extensions**: AES, SHA256/512, etc.
- **Pointer Authentication**: Address and generic authentication
- **System Features**: PAN, LSE, MTE, SVE support

## Integration

This test is part of the ARM64 kernel architecture implementation and verifies that:
- All required headers are properly structured
- ARM64-specific constants are correctly defined
- Structure layouts match ARM64 requirements
- Build system integration works correctly

## Troubleshooting

If compilation fails:
1. Check that ARM64 headers are in the correct locations
2. Verify that the Haiku build system is configured for ARM64
3. Ensure all required ARM64 kernel files are present
4. Check that cross-compilation tools are available

## Future Enhancements

The test can be extended to include:
- Runtime testing of assembly functions (in kernel mode)
- Performance benchmarking of context switch operations
- Validation of exception handling paths
- Testing of ARM64-specific CPU features