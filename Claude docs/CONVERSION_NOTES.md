# Haiku libbe Jamfile to Meson Conversion

## Overview

This document describes the conversion of Haiku's `libbe.so` build system from Jam to Meson, with special focus on architecture-specific source file handling.

## Original Jam Structure

The original Jamfile for libbe is located in `/src/kits/Jamfile` and builds from multiple kit objects:

```jam
SharedLibrary $(libbe) : :
    <libbe!$(architecture)>app_kit.o
    <libbe!$(architecture)>interface_kit.o
    <libbe!$(architecture)>locale_kit.o
    <libbe!$(architecture)>storage_kit.o
    <libbe!$(architecture)>support_kit.o
    ...
```

Each kit is built in its respective subdirectory with architecture-specific rules.

## Architecture-Specific Handling

### Original Jam Approach
```jam
local architectureObject ;
for architectureObject in [ MultiArchSubDirSetup ] {
    on $(architectureObject) {
        local architecture = $(TARGET_PACKAGING_ARCH) ;
        # Architecture-specific sources
        SEARCH_SOURCE += [ FDirName $(SUBDIR) arch $(TARGET_ARCH) ] ;
    }
}
```

### Meson Conversion
```meson
# Get architecture information
host_arch = host_machine.cpu_family()

# Architecture-specific debug sources
debug_arch_sources = []
if host_arch in ['x86', 'x86_64', 'aarch64', 'arm', 'riscv64']
    debug_arch_sources += files('../debug/arch/@0@/arch_debug_support.cpp'.format(host_arch))
endif
```

## Key Conversion Points

### 1. Multi-Architecture Support
- **Jam**: Uses `MultiArchSubDirSetup` and `TARGET_PACKAGING_ARCH`
- **Meson**: Uses `host_machine.cpu_family()` for runtime architecture detection

### 2. Conditional Sources
- **Jam**: `SEARCH_SOURCE += [ FDirName $(SUBDIR) arch $(TARGET_ARCH) ]`
- **Meson**: Conditional file inclusion based on `host_arch`

### 3. Build Features
- **Jam**: `[ BuildFeatureAttribute icu : libraries ]`
- **Meson**: `dependency('icu-uc')` with proper fallback handling

### 4. Resource Processing
- **Jam**: `AddResources $(libbe) : libbe_version.rdef ...`
- **Meson**: `haiku.process_resources()` with architecture-specific defines

### 5. Localization
- **Jam**: `DoCatalogs libbe.so : x-vnd.Haiku-libbe : ...`
- **Meson**: `haiku.build_catalog()` with automatic string extraction

## Architecture Support Matrix

| Architecture | Jam Name | Meson Name | Debug Support | Notes |
|--------------|----------|------------|---------------|-------|
| x86_64       | x86_64   | x86_64     | Yes           | Primary desktop |
| AArch64      | arm64    | aarch64    | Yes           | ARM 64-bit |
| x86          | x86      | x86        | Yes           | Legacy 32-bit |
| RISC-V 64    | riscv64  | riscv64    | Yes           | Emerging arch |
| ARM          | arm      | arm        | Yes           | ARM 32-bit |

## File Structure Mapping

### Original Jam Structure
```
src/kits/
├── Jamfile (main libbe build)
├── app/Jamfile
├── interface/Jamfile
├── storage/Jamfile
├── support/Jamfile
├── locale/Jamfile
├── debug/Jamfile
└── shared/Jamfile
```

### Meson Structure
```
src/kits/meson_build/
├── meson.build (consolidated build)
├── meson_options.txt (build options)
├── libbe.rdef (resources)
└── CONVERSION_NOTES.md (this file)
```

## Build Options

### Jam Variables
- `RUN_WITHOUT_REGISTRAR`: Development flag
- `RUN_WITHOUT_APP_SERVER`: Development flag
- `TARGET_PACKAGING_ARCH`: Target architecture

### Meson Options
- `run_without_registrar`: boolean option
- `run_without_app_server`: boolean option  
- `build_tests`: boolean for libbe_test.so
- `architecture_specific_optimizations`: enable arch optimizations

## Dependencies

### Build-time Dependencies
- zlib (required)
- libzstd (optional, with `ZSTD_ENABLED` define)
- ICU (required for Unicode support)

### Internal Dependencies
- libshared.a (static library)
- libcolumnlistview.a (static library)
- Various architecture-specific object files

## Testing

The conversion includes support for building `libbe_test.so`, which can be enabled with the `build_tests` option:

```bash
meson setup builddir -Dbuild_tests=true
meson compile -C builddir
```

## Architecture-Specific Features

### Debug Support
Different architectures have different debug support implementations:

```meson
debug_arch_sources = []
if host_arch in ['x86', 'x86_64', 'aarch64', 'arm', 'riscv64']
    debug_arch_sources += files('../debug/arch/@0@/arch_debug_support.cpp'.format(host_arch))
endif
```

### Resource Compilation
Architecture-specific defines are passed to the resource compiler:

```meson
rsrc = haiku.process_resources('libbe.rdef',
    defines: ['ARCH_@0@'.format(host_arch.to_upper())],
    ...
)
```

## Validation

To verify the conversion:

1. **Source File Completeness**: All source files from original Jamfiles are included
2. **Architecture Support**: All supported architectures build correctly
3. **Resource Processing**: Resources are compiled with correct architecture defines
4. **Dependencies**: All external and internal dependencies are properly linked
5. **Symbol Compatibility**: Generated library exports same symbols as Jam build

## Future Improvements

1. **Cross-Compilation**: Enhanced support for cross-compilation scenarios
2. **Parallel Builds**: Better utilization of Meson's parallel build capabilities  
3. **Testing Integration**: Automated testing of architecture-specific code paths
4. **Optimization**: Architecture-specific optimization flags and features
5. **Package Integration**: Better integration with Haiku's packaging system