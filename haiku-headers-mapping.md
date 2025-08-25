# Haiku Build System Header Directory Mapping

This document maps all header directories in the Haiku build system for proper CMake configuration.

## Core Haiku Headers

### 1. Public OS Headers
```
/home/ruslan/haiku/headers/
├── os/
│   ├── app/           # Application Kit headers
│   ├── device/        # Device headers  
│   ├── drivers/       # Driver headers
│   ├── game/          # Game Kit headers
│   ├── interface/     # Interface Kit headers
│   ├── kernel/        # Kernel headers
│   ├── locale/        # Locale Kit headers
│   ├── media/         # Media Kit headers
│   ├── midi/          # MIDI headers
│   ├── midi2/         # MIDI 2 headers
│   ├── net/           # Network headers
│   ├── storage/       # Storage Kit headers
│   ├── support/       # Support Kit headers
│   └── translation/   # Translation Kit headers
├── posix/             # POSIX compatibility headers
└── glibc/            # glibc compatibility headers
```

### 2. Private Headers
```
/home/ruslan/haiku/headers/private/
├── app/              # Private Application Kit
├── interface/        # Private Interface Kit  
├── kernel/           # Private kernel headers
├── libroot/          # Private libroot headers
├── locale/           # Private Locale Kit
├── media/            # Private Media Kit
├── shared/           # Shared private headers
├── storage/          # Private Storage Kit
├── support/          # Private Support Kit (ZlibCompressionAlgorithm.h, etc.)
├── tracker/          # Private Tracker headers
└── system/           # Private system headers
    └── arch/
        └── x86_64/   # Architecture-specific headers
```

### 3. Build Headers
```
/home/ruslan/haiku/headers/build/
├── config_headers/   # Build configuration headers  
└── private/
    └── support/      # Build-time private support headers (ZlibCompressionAlgorithm.h, etc.)
```

### 4. Library Headers
```
/home/ruslan/haiku/headers/libs/
├── agg/             # Anti-Grain Geometry library
└── icon/            # Icon library
```

## Generated Build Package Headers

### 1. Third-party Dependencies
```
/home/ruslan/haiku/generated/build_packages/
├── icu74-74.1-3-x86_64/develop/headers/
│   └── unicode/     # ICU Unicode headers (uversion.h, etc.)
├── zlib-1.3.1-3-x86_64/develop/headers/
│   ├── zlib.h       # Main zlib header
│   └── zconf.h      # zlib configuration
├── zstd-1.5.6-1-x86_64/develop/headers/
│   ├── zstd.h       # Main zstd header
│   ├── zstd_errors.h
│   └── zdict.h
├── openssl3-3.0.14-2-x86_64/develop/headers/
├── freetype-2.13.2-1-x86_64/develop/headers/
├── mesa-22.0.5-3-x86_64/develop/headers/
└── gcc_syslibs_devel-13.3.0_2023_08_10-1-x86_64/develop/headers/
    └── c++/         # C++ standard library headers
```

## Cross-Compiler Sysroot Headers

### 1. Installed Headers  
```
/home/ruslan/haiku/generated/cross-tools-x86_64/sysroot/boot/system/develop/headers/
├── config/          # System configuration
├── os/              # Minimal OS headers for cross-compilation
└── ... (subset of main headers)
```

## Generated Packaging Headers

### 1. Package Build Headers
```  
/home/ruslan/haiku/generated/objects/haiku/x86_64/packaging/packages_build/regular/
├── hpkg_-haiku_devel.hpkg/contents/develop/headers/
└── hpkg_-userland_fs.hpkg/contents/develop/headers/
```

## CMake Include Path Mapping

Based on Jam build analysis (`jam -dx libbe.so`), the correct include order is:

### 1. Build Package Headers (External Dependencies)
```cmake
# Third-party dependencies from build packages
include_directories(SYSTEM
    ${HAIKU_ROOT}/generated/build_packages/zlib-1.3.1-3-x86_64/develop/headers
    ${HAIKU_ROOT}/generated/build_packages/zstd-1.5.6-1-x86_64/develop/headers  
    ${HAIKU_ROOT}/generated/build_packages/icu74-74.1-3-x86_64/develop/headers
    ${HAIKU_ROOT}/generated/build_packages/gcc_syslibs_devel-13.3.0_2023_08_10-1-x86_64/develop/headers/c++
    ${HAIKU_ROOT}/generated/build_packages/gcc_syslibs_devel-13.3.0_2023_08_10-1-x86_64/develop/headers/c++/x86_64-unknown-haiku
    ${HAIKU_ROOT}/generated/build_packages/gcc_syslibs_devel-13.3.0_2023_08_10-1-x86_64/develop/headers/c++/backward
    ${HAIKU_ROOT}/generated/build_packages/gcc_syslibs_devel-13.3.0_2023_08_10-1-x86_64/develop/headers/c++/ext
)
```

### 2. Private Headers (Haiku Internal)
```cmake  
# Private headers (order matters!)
include_directories(SYSTEM
    headers/private/app
    headers/private/interface  
    headers/private/locale
    headers/private/media
    headers/private/shared
    headers/private/support      # ZlibCompressionAlgorithm.h, etc.
    headers/private/storage
    headers/private/tracker
    headers/private/system
    headers/private/system/arch/x86_64
    headers/build/private/support # Build-time compression headers
)
```

### 3. Compatibility Headers
```cmake
# POSIX and glibc compatibility  
include_directories(SYSTEM
    headers/glibc
    headers/posix
)
```

### 4. GCC Headers
```cmake
# GCC built-in headers
include_directories(SYSTEM
    ${HAIKU_ROOT}/generated/build_packages/gcc_syslibs_devel-13.3.0_2023_08_10-1-x86_64/develop/headers/gcc/include
    ${HAIKU_ROOT}/generated/build_packages/gcc_syslibs_devel-13.3.0_2023_08_10-1-x86_64/develop/headers/gcc/include-fixed
)
```

### 5. Public OS Headers (Last)
```cmake
# Public Haiku headers (should be last to avoid conflicts)
include_directories(SYSTEM
    headers
    headers/os
    headers/os/add-ons
    headers/os/add-ons/file_system
    headers/os/add-ons/graphics
    headers/os/add-ons/input_server  
    headers/os/add-ons/registrar
    headers/os/add-ons/screen_saver
    headers/os/add-ons/tracker
    headers/os/app
    headers/os/device
    headers/os/drivers
    headers/os/game
    headers/os/interface
    headers/os/kernel
    headers/os/locale
    headers/os/media
    headers/os/mail
    headers/os/midi
    headers/os/midi2
    headers/os/net
    headers/os/storage
    headers/os/support
    headers/os/translation
)
```

### 6. Library Headers
```cmake
# Additional library headers
include_directories(SYSTEM
    headers/libs/agg
    headers/libs/icon
)
```

## Key Insights

1. **Build packages come first** - External dependencies (zlib, ICU, etc.) have highest precedence
2. **Private headers before public** - Private Haiku headers override public ones when needed  
3. **Architecture-specific paths** - x86_64 specific headers included explicitly
4. **Build vs Source headers** - Some headers exist in both `/headers/private/` and `/headers/build/private/`
5. **GCC integration** - Cross-compiler's C++ stdlib headers are included explicitly

## CMake Implementation Strategy

```cmake
# Use HAIKU_ROOT variable for portability
set(HAIKU_ROOT "/home/ruslan/haiku")
set(BUILD_PACKAGES "${HAIKU_ROOT}/generated/build_packages")

# Apply all includes in correct order
# (Build packages, private, compatibility, gcc, public, libs)
```

This mapping provides the complete picture of Haiku's header system for proper CMake migration.