# Build Configuration Tests

This directory contains tests that verify the Haiku build system configuration, particularly for cross-compilation and architecture-specific settings.

## ARM64 Configuration Test

The ARM64 configuration test (`ARM64ConfigTest`) verifies that the build system correctly recognizes and configures ARM64/AArch64 architecture settings.

### What it tests:

1. **Architecture Detection**
   - Verifies `__HAIKU_ARCH_ARM64` constant is properly defined
   - Checks architecture recognition in cross-compilation environment

2. **Compiler Flags**
   - Tests ARMv8.6-A instruction set support
   - Verifies crypto extensions (`__ARM_FEATURE_CRYPTO`)
   - Checks Large System Extensions v2 (LSE2) support
   - Tests pointer authentication (`__ARM_FEATURE_PAC_DEFAULT`)

3. **ABI Settings**
   - Confirms 64-bit architecture
   - Verifies little-endian byte order
   - Tests alignment requirements (8-byte aligned for long long)

4. **Build System Integration**
   - Tests `HAIKU_ARCH_ARM64` constant availability
   - Verifies consistency between preprocessor and build system constants

5. **Performance Features**
   - Tests outline atomics support
   - Verifies NEON SIMD availability
   - Checks FP16 vector arithmetic support

### Running the tests:

#### C++ Test Suite
```bash
# Build and run the test (requires proper build environment)
jam ARM64ConfigTestRunner
```

#### Shell Script Verification
```bash
# Run comprehensive configuration check
cd src/tests/build
./test_arm64_config.sh
```

### Test Script Output

The test script (`test_arm64_config.sh`) performs these checks:

1. **BuildConfig Analysis** - Verifies ARM64 is recognized in build configuration
2. **Cross-tools Verification** - Checks ARM64 cross-compilation toolchain
3. **Compiler Flag Testing** - Tests advanced ARM64 compiler flags
4. **ArchitectureRules Validation** - Verifies ARM64 configuration in build rules
5. **BuildSetup Constants** - Checks `HAIKU_ARCH_ARM64` constant definition

### Expected Results

On a properly configured ARM64 Haiku build environment:
- ✓ ARM64 architecture found in BuildConfig  
- ✓ ARM64 cross-tools directory exists
- ✓ ARM64 GCC compiler found
- ✓ ARM64 memory model flags supported
- ✓ ARM64 security flags supported  
- ✓ Advanced ARM64 architecture flags configured
- ✓ ARM64 security features configured
- ✓ HAIKU_ARCH_ARM64 constant defined

### Architecture-Specific Flags Tested

The configuration includes these ARM64-specific compiler flags:

- `-march=armv8.6-a+crypto+lse2` - Advanced ARM architecture with crypto and LSE2
- `-mcmodel=small` - Small memory model for kernel
- `-mstrict-align` - Strict memory alignment  
- `-mtune=cortex-a78` - Optimization for modern ARM64 cores
- `-moutline-atomics` - Better atomic operations
- `-mbranch-protection=pac-ret+leaf` - Hardware security features

### Files

- `ARM64ConfigTest.cpp` - Main C++ test implementation
- `ARM64ConfigTest.h` - Test class header  
- `BuildTestAddon.cpp` - Test suite addon integration
- `test_arm64_config.sh` - Shell script for configuration verification
- `Jamfile` - Build configuration for tests
- `README.md` - This documentation

### Integration

The build tests are automatically included in the main test suite through:
- Added to `src/tests/Jamfile` as `SubInclude HAIKU_TOP src tests build`
- Creates `libbuildtest.so` test library
- Provides standalone `ARM64ConfigTestRunner` executable