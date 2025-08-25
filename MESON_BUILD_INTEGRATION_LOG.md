# Haiku OS Meson Build System Integration

## Project Goal
Create Meson build system for Haiku OS that produces working libbe.so shared library with all core functionality.

## Current Status: ❌ NEEDS FIXES
- **libbe.so.1.0.0**: Does NOT build - glibc components failing
- **Build System**: Meson integration partially working
- **Resource Handling**: Universal .rdef compilation system working
- **Dependency Management**: Dynamic external library detection working

## Architecture Overview

### Directory Structure
```
/home/ruslan/haiku/
├── builddir/                           # Meson build directory
├── src/kits/                          # Kit source files
│   ├── meson.build                    # Builds libbe.so
│   ├── app/meson.build               # App Kit
│   ├── storage/meson.build           # Storage Kit
│   ├── support/meson.build           # Support Kit
│   ├── interface/meson.build         # Interface Kit
│   └── locale/meson.build            # Locale Kit
├── build/meson/modules/              # Build system modules
│   ├── HaikuCommon.py               # Core build configuration
│   ├── ResourceHandler.py           # Universal .rdef handling
│   └── BuildFeatures.py             # Dynamic dependency detection
└── build/meson/haiku-x86_64-cross.ini # Cross-compilation config
```

### Build Components

#### 1. Kit Object Files
Each kit compiles to merged object file:
- **App Kit**: app_kit.o (application framework)
- **Storage Kit**: storage_kit.o (file system + MIME)
- **Support Kit**: support_kit.o (utility classes)
- **Interface Kit**: interface_kit.o (GUI components)
- **Locale Kit**: locale_kit.o (internationalization)

#### 2. libbe.so Assembly
```meson
libbe = shared_library('be',
    shared_sources,                    # Shared source files
    version_resource,                  # Compiled .rdef resources
    icons_resource,
    country_flags_resource,
    language_flags_resource,
    
    objects: [                         # Kit object files
        app_kit_merged,
        storage_kit_merged,
        support_kit_merged,
        interface_kit_merged,
        locale_kit_merged,
    ],
    
    link_with: additional_libs,        # Static libraries (libicon, libagg)
    dependencies: system_deps,         # External deps (zlib, zstd, icu)
)
```

#### 3. Resource Compilation System
**ResourceHandler.py** provides universal .rdef handling:
- Auto-detects rc compiler location
- Creates standardized resource targets
- Generates Meson custom_target configurations

#### 4. Dynamic Dependencies
**BuildFeatures.py** mimics Jam's BuildFeatureAttribute:
- Auto-detects build packages (zlib, zstd, ICU)
- Creates dependency declarations with proper linking
- Generates pkg-config compatible information

## Build Commands

### Setup
```bash
cd /home/ruslan/haiku
meson setup builddir --cross-file build/meson/haiku-x86_64-cross.ini
```

### Build libbe.so
```bash
meson compile -C builddir src/kits/be
```

### Results
- **builddir/src/kits/libbe.so.1.0.0**: 6.0MB shared library
- **builddir/meson-private/libbe.pc**: PKG-config file
- **builddir/meson-uninstalled/libbe-uninstalled.pc**: Development PKG-config

## Technical Requirements

### Cross-Compilation
Uses Haiku's x86_64-unknown-haiku-gcc toolchain:
```ini
[binaries]
c = '/home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-gcc'
cpp = '/home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++'
```

### Critical Compiler Flags
```meson
cpp_args: [
    '-O2',                           # Release optimization
    '-fno-strict-aliasing',         # Haiku requirement
]
```

### External Dependencies
- **ZLIB**: Auto-detected from build_packages/zlib-*
- **ZSTD**: Auto-detected from build_packages/zstd-*
- **ICU**: Auto-detected from build_packages/icu74-*
- **Threads**: System threading library

## Key Implementation Details

### 1. Kit Object Merging
Each kit uses `ld -r` to merge all object files:
```meson
kit_merged = custom_target('kit_name',
    input: kit_temp_lib,
    output: 'kit_name.o',
    command: [
        'sh', '-c',
        cross_ld + ' -r -o @OUTPUT@ @INPUT@.p/*.o'
    ]
)
```

### 2. Resource Compilation
Uses ResourceHandler for .rdef files:
```meson
version_resource = custom_target('libbe_version_rsrc',
    input: 'libbe_version.rdef',
    output: 'libbe_version.rsrc',
    command: [rc_compiler, '-o', '@OUTPUT@', '@INPUT@']
)
```

### 3. PKG-config Generation
Meson pkgconfig module creates libbe.pc:
```meson
pkg = import('pkgconfig')
pkg.generate(
    libraries: libbe,
    name: 'libbe',
    description: 'Haiku Application Framework Library'
)
```

## Troubleshooting

### Common Issues
1. **RC compiler not found**: Ensure tools are built in generated/objects/linux/x86_64/release/tools/rc/
2. **Cross-compilation failure**: Verify cross-tools exist in generated/cross-tools-x86_64/
3. **Missing dependencies**: Run build package extraction first
4. **Empty libbe.so**: Ensure `objects` parameter used instead of `dependencies`

### Build Package Requirements
External dependencies must be extracted to:
- `/home/ruslan/haiku/generated/build_packages/zlib-*`
- `/home/ruslan/haiku/generated/build_packages/zstd-*`
- `/home/ruslan/haiku/generated/build_packages/icu74-*`

## Current Issues (August 2025)
- ❌ **Main glibc library empty**: libposix_glibc.a only 8 bytes - build fails
- ✅ **Extensions module working**: Fixed with `-DNOT_IN_libc=1` flag
- ✅ **Regex module working**: Compiles successfully, symbols match Jam build
- ❌ **Stdlib module failing**: Compilation errors with glibc macros and headers
- ❌ **Libio module failing**: Not yet tested after stdlib fix
- ❌ **Other glibc modules**: iconv, stdio-common, wcsmbs, arch modules failing

## Success Metrics
- ✅ Individual kits (app, interface, storage, support, locale) work
- ✅ Cross-compilation working
- ✅ Resource compilation functional
- ✅ PKG-config files generated
- ✅ Dynamic dependency detection working
- ✅ glibc extensions module: Fixed and working
- ✅ glibc regex module: Working correctly
- ❌ glibc integration: Major blocker for libbe.so build

## Next Steps
1. Fix stdlib compilation issues (glibc macro compatibility)
2. Apply `-DNOT_IN_libc=1` fix to all failing glibc modules
3. Investigate why main libposix_glibc.a is empty
4. Complete glibc module integration
5. Test full libbe.so build