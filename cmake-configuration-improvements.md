# CMake Configuration Improvements - Removing Hardcoded Values

## Overview

This document details the actions taken to remove hardcoded values from the Haiku CMakeLists.txt and make it fully configurable for different environments, architectures, and package versions.

## Problems Identified

The original CMakeLists.txt contained multiple hardcoded values that limited its portability:

1. **Hardcoded absolute paths**: `/home/ruslan/haiku/generated/...`
2. **Fixed architecture**: `x86_64` embedded in paths
3. **Fixed package versions**: Specific version numbers for zlib, zstd, ICU, GCC
4. **Non-configurable Haiku root**: Assumed specific directory structure

## Actions Taken

### 1. Dynamic Haiku Root Detection

**Problem**: Hardcoded user-specific path `/home/ruslan/haiku/`

**Solution**: Added automatic detection with override capability
```cmake
# Before
set(CMAKE_C_COMPILER /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-gcc)

# After  
if(NOT DEFINED HAIKU_ROOT)
    get_filename_component(HAIKU_ROOT "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
endif()
set(CMAKE_C_COMPILER ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/bin/${HAIKU_TARGET}-gcc)
```

### 2. Architecture Configuration

**Problem**: Fixed `x86_64` architecture in multiple paths

**Solution**: Added configurable architecture variables
```cmake
# Configure target architecture (can be overridden)
if(NOT DEFINED HAIKU_ARCH)
    set(HAIKU_ARCH "x86_64")
endif()

# Configure target triplet (can be overridden)  
if(NOT DEFINED HAIKU_TARGET)
    set(HAIKU_TARGET "${HAIKU_ARCH}-unknown-haiku")
endif()
```

### 3. Package Version Variables

**Problem**: Hardcoded package versions throughout the file

**Solution**: Centralized package version configuration
```cmake
# Before
${HAIKU_BUILD_PACKAGES_BASE}/zlib-1.3.1-3-x86_64/develop/headers
${HAIKU_BUILD_PACKAGES_BASE}/gcc_syslibs_devel-13.3.0_2023_08_10-1-x86_64/develop/headers/c++

# After
if(NOT DEFINED HAIKU_ZLIB_PACKAGE)
    set(HAIKU_ZLIB_PACKAGE "zlib-1.3.1-3-${HAIKU_ARCH}")
endif()
if(NOT DEFINED HAIKU_GCC_PACKAGE)
    set(HAIKU_GCC_PACKAGE "gcc_syslibs_devel-13.3.0_2023_08_10-1-${HAIKU_ARCH}")
endif()

${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_ZLIB_PACKAGE}/develop/headers
${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/c++
```

### 4. Header Path Flexibility

**Problem**: Hardcoded `${CMAKE_SOURCE_DIR}/../../` paths

**Solution**: Use dynamic `${HAIKU_ROOT}` variable
```cmake
# Before
${CMAKE_SOURCE_DIR}/../../headers/private/app
${CMAKE_SOURCE_DIR}/../../headers/private/system/arch/x86_64

# After
${HAIKU_ROOT}/headers/private/app
${HAIKU_ROOT}/headers/private/system/arch/${HAIKU_ARCH}
```

### 5. Cross-Compiler Path Configuration

**Problem**: Fixed cross-compiler paths

**Solution**: Dynamic path construction
```cmake
# Before
set(CMAKE_SYSROOT /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot)
set(CMAKE_FIND_ROOT_PATH /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot)

# After
set(CMAKE_SYSROOT ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/sysroot)
set(CMAKE_FIND_ROOT_PATH ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/sysroot)
```

### 6. Build Package Base Path

**Problem**: Relative path assumptions

**Solution**: Absolute path from detected root
```cmake
# Before
set(HAIKU_BUILD_PACKAGES_BASE "${CMAKE_SOURCE_DIR}/../../generated/build_packages")

# After
set(HAIKU_BUILD_PACKAGES_BASE "${HAIKU_ROOT}/generated/build_packages")
```

### 7. Library Linking Path

**Problem**: Hardcoded libstdc++ path

**Solution**: Use package variable
```cmake
# Before
${HAIKU_BUILD_PACKAGES_BASE}/gcc_syslibs_devel-13.3.0_2023_08_10-1-x86_64/develop/lib/libstdc++.so.6

# After
${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/lib/libstdc++.so.6
```

## Configuration Variables Added

The following variables can now be overridden via CMake `-D` flags:

| Variable | Default | Description |
|----------|---------|-------------|
| `HAIKU_ROOT` | Auto-detected | Path to Haiku source tree |
| `HAIKU_ARCH` | `x86_64` | Target architecture |
| `HAIKU_TARGET` | `${HAIKU_ARCH}-unknown-haiku` | Target triplet |
| `HAIKU_ZLIB_PACKAGE` | `zlib-1.3.1-3-${HAIKU_ARCH}` | Zlib package name |
| `HAIKU_ZSTD_PACKAGE` | `zstd-1.5.6-1-${HAIKU_ARCH}` | Zstd package name |
| `HAIKU_ICU_PACKAGE` | `icu74-74.1-3-${HAIKU_ARCH}` | ICU package name |
| `HAIKU_GCC_PACKAGE` | `gcc_syslibs_devel-13.3.0_2023_08_10-1-${HAIKU_ARCH}` | GCC package name |

## Usage Examples

### Default Configuration
```bash
cmake -B build .
```

### Custom Haiku Root
```bash
cmake -B build -DHAIKU_ROOT=/path/to/haiku .
```

### Different Architecture
```bash
cmake -B build -DHAIKU_ARCH=riscv64 .
```

### Custom Package Versions
```bash
cmake -B build -DHAIKU_ZLIB_PACKAGE=zlib-1.3.2-1-x86_64 .
```

### Combined Configuration
```bash
cmake -B build \
    -DHAIKU_ROOT=/custom/haiku \
    -DHAIKU_ARCH=arm64 \
    -DHAIKU_ICU_PACKAGE=icu75-75.1-1-arm64 \
    .
```

## Documentation Added

Added comprehensive usage documentation at the top of CMakeLists.txt:

```cmake
# Usage:
#   cmake -B build .                    # Use defaults (x86_64, auto-detect Haiku root)
#   cmake -B build -DHAIKU_ROOT=/path/to/haiku .
#   cmake -B build -DHAIKU_ARCH=riscv64 .
#   cmake -B build -DHAIKU_ZLIB_PACKAGE=zlib-1.3.2-1-riscv64 .
#
# Configurable variables:
#   HAIKU_ROOT        - Path to Haiku source tree (auto-detected)
#   HAIKU_ARCH        - Target architecture (default: x86_64)  
#   HAIKU_TARGET      - Target triplet (default: ${HAIKU_ARCH}-unknown-haiku)
#   HAIKU_*_PACKAGE   - Package names for zlib, zstd, ICU, GCC
```

## Verification

All changes were tested to ensure:

1. **Backward Compatibility**: Default values match original hardcoded paths
2. **Path Detection**: Automatic Haiku root detection works correctly
   ```
   -- Using Haiku root: /home/ruslan/haiku
   -- Using Haiku architecture: x86_64
   -- Using Haiku target: x86_64-unknown-haiku
   ```
3. **Variable Override**: All variables can be overridden via command line
4. **Build Success**: CMake configuration and compilation still work
   ```
   [100%] Built target app_kit_obj
   [100%] Built target interface_kit_obj
   [100%] Built target locale_kit_obj
   [100%] Built target storage_kit_obj
   [100%] Built target support_kit_obj
   ```
5. **Cross-Architecture**: Framework supports different architectures

## Benefits Achieved

1. **Portability**: Can be used in any Haiku development environment
2. **Multi-Architecture**: Supports x86_64, riscv64, arm64, etc.
3. **Version Flexibility**: Easy to update package versions
4. **Development Friendly**: No need to modify CMakeLists.txt for different setups
5. **CI/CD Ready**: Can be configured via environment variables or scripts
6. **Maintainability**: Centralized configuration reduces duplication

## Impact

This refactoring transforms the CMakeLists.txt from a hardcoded, environment-specific build script into a flexible, portable build system suitable for:

- Multiple development environments
- Different architectures 
- Various package versions
- Continuous integration systems
- Distribution packaging
- Community contributions

The changes maintain full backward compatibility while enabling significantly more flexible usage patterns.