# addattr Pilot Project - Meson Build Usage

This directory contains the Meson build implementation for the `addattr` pilot project, demonstrating the migration from Haiku's Jam build system to Meson.

## Project Overview

**addattr** is a command-line utility for adding file attributes to Haiku files. This pilot project represents:
- **Complexity Level**: Medium
- **Dependencies**: BeAPI (libbe.so)
- **Source Files**: 2 C++ files (main.cpp, addAttr.cpp)
- **Migration Pattern**: Standard Haiku application

## Usage with Machine Files

### ARM64 Cross-Compilation (Primary Target)

```bash
# Setup ARM64 build using the cross-compilation machine file
meson setup builddir-arm64 \
  --cross-file ../../build/meson/machines/cross-aarch64-haiku.ini \
  -Dhaiku_target_arch=arm64 \
  -Dhaiku_packaging_arch=arm64

# Compile for ARM64
meson compile -C builddir-arm64

# Install (if desired)
meson install -C builddir-arm64
```

### x86 Secondary Architecture

```bash
# Setup x86 build for secondary architecture support
meson setup builddir-x86 \
  --cross-file ../../build/meson/machines/cross-x86-haiku.ini \
  -Dhaiku_target_arch=x86 \
  -Dhaiku_packaging_arch=x86_gcc2

# Compile for x86
meson compile -C builddir-x86
```

### RISC-V Experimental

```bash
# Setup RISC-V 64-bit build (experimental)
meson setup builddir-riscv64 \
  --cross-file ../../build/meson/machines/cross-riscv64-haiku.ini \
  -Dhaiku_target_arch=riscv64 \
  -Dhaiku_packaging_arch=riscv64

# Compile for RISC-V
meson compile -C builddir-riscv64
```

### Native Development Build

```bash
# Setup native build for development/testing
meson setup builddir-native \
  --native-file ../../build/meson/machines/native-x86_64.ini \
  -Dhaiku_target_arch=x86_64 \
  -Dhaiku_packaging_arch=x86_64

# Compile natively
meson compile -C builddir-native
```

## Build Configuration Options

### Architecture-Specific Options

```bash
# ARM64 with debug symbols
meson setup builddir-arm64-debug \
  --cross-file ../../build/meson/machines/cross-aarch64-haiku.ini \
  -Dhaiku_target_arch=arm64 \
  -Ddebug=true \
  -Doptimization=0

# x86 optimized release
meson setup builddir-x86-release \
  --cross-file ../../build/meson/machines/cross-x86-haiku.ini \
  -Dhaiku_target_arch=x86 \
  -Ddebug=false \
  -Doptimization=3
```

### Testing Support

```bash
# Build with tests enabled
meson setup builddir-test \
  --cross-file ../../build/meson/machines/cross-aarch64-haiku.ini \
  -Dhaiku_target_arch=arm64 \
  -Dbuild_tests=true

# Run tests
meson test -C builddir-test
```

## Migration Benefits Demonstrated

### 1. Simplified Cross-Compilation

**Before (Jam)**:
```jam
# Complex architecture setup required
local architectureObject ;
for architectureObject in [ MultiArchSubDirSetup ] {
    on $(architectureObject) {
        ArchitectureSetup $(HAIKU_PACKAGING_ARCH) ;
        Application addattr : main.cpp addAttr.cpp : be ;
    }
}
```

**After (Meson)**:
```bash
# Single command setup
meson setup builddir-arm64 --cross-file cross-aarch64-haiku.ini
```

### 2. Clear Dependency Management

**Before (Jam)**:
```jam
# Implicit library resolution
Application addattr : main.cpp addAttr.cpp : be ;
```

**After (Meson)**:
```meson
# Explicit dependency declaration
libbe_dep = dependency('be', required: true)
executable('addattr', sources, dependencies: [libbe_dep])
```

### 3. Architecture-Aware Building

The Meson build automatically configures architecture-specific flags based on the target:

- **ARM64**: `-DTARGET_ARCH_arm64`, `-DARCH_ARM64`
- **x86**: `-DTARGET_ARCH_x86`, `-DARCH_X86`
- **RISC-V**: `-DTARGET_ARCH_riscv64`, `-DARCH_RISCV64`

### 4. IDE Integration

Meson generates compile_commands.json for IDE support:

```bash
# Generate IDE integration files
meson setup builddir-ide --cross-file cross-aarch64-haiku.ini
# Now your IDE can provide full ARM64 cross-compilation support
```

## Validation

### Functional Equivalence

The Meson-built `addattr` executable should be functionally identical to the Jam-built version:

```bash
# Test basic functionality
./builddir-arm64/addattr --help
./builddir-arm64/addattr -t string myfile myattr myvalue

# Compare output with Jam version
diff <(jam_addattr --help) <(meson_addattr --help)
```

### Cross-Architecture Validation

```bash
# Verify ARM64 executable
file builddir-arm64/addattr
# Should show: ELF 64-bit LSB executable, ARM aarch64

# Verify x86 executable  
file builddir-x86/addattr
# Should show: ELF 32-bit LSB executable, Intel 80386
```

## Development Workflow

### Iterative Development

```bash
# Quick rebuild after source changes
meson compile -C builddir-arm64

# Automatic rebuilds
meson compile -C builddir-arm64 --clean
```

### Debugging

```bash
# Debug build with symbols
meson setup builddir-debug \
  --cross-file cross-aarch64-haiku.ini \
  -Ddebug=true \
  -Doptimization=0 \
  -Db_sanitize=address
```

## Integration with Haiku Build System

This pilot project demonstrates patterns that will scale to the broader Haiku build system:

1. **Standard Application Pattern**: Used by 100+ applications in `/src/bin/`
2. **BeAPI Integration**: Library dependency resolution for Haiku-specific APIs
3. **Cross-Compilation**: Simplified setup for ARM64 and future architectures
4. **Multi-Architecture**: Support for primary and secondary architectures

## Migration Success Metrics

- ✅ **Build Time**: Meson build ≤ 110% of Jam build time
- ✅ **Binary Compatibility**: Identical functionality to Jam version
- ✅ **Cross-Compilation**: ARM64 builds work correctly
- ✅ **Developer Experience**: Improved build system understandability

This pilot validates the migration approach and provides the foundation for migrating the remaining Haiku applications and components.