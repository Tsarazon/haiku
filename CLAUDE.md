# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System Commands

### Primary Build System (JAM)
```bash
# Configure the build (run once or after major changes)
./configure --build-cross-tools x86_64

# Build the entire system
jam

# Build specific targets
jam app_server
jam libbe.so
jam BitmapPainter.o

# Build with specific output directory
HAIKU_OUTPUT_DIR=/path/to/output jam target_name

# Clean build
jam clean
```

### Hybrid Build Systems
This codebase supports multiple build systems:
- **JAM** (primary): Traditional Haiku build system
- **Meson** (experimental): Modern build system for select components
- **CMake** (limited): Specific tools and validation

```bash
# Meson builds (for compatible components)
meson setup builddir
meson compile -C builddir

# CMake builds (for tools/validation)
cmake -B build_cmake
cmake --build build_cmake
```

### Development Commands
```bash
# Run linting and type checks
./buildtools/jam/bin.linuxx86/jam -qa <target>

# Cross-compilation toolchain build
./build_cross_tools.sh x86_64

# Generate build configuration
./configure --help  # See all options
```

## Architecture Overview

### Core System Structure
- **src/kits/**: Haiku API kits (app, interface, storage, network, etc.)
- **src/servers/**: System servers (app_server, registrar, input_server, etc.)  
- **src/add-ons/**: Modular components (translators, media nodes, file system drivers)
- **src/system/**: Kernel, boot loader, and system libraries
- **headers/**: Public and private header files organized by kit/component

### Graphics Subsystem (Active Development)
The graphics subsystem is undergoing major refactoring:
- **Current**: AGG-based rendering in app_server
- **Target**: Abstract IRenderEngine interface to support multiple backends (AGG, Blend2D, Skia)
- **Key files**:
  - `src/servers/app/render/IRenderEngine.h` - Abstract rendering interface
  - `src/servers/app/render/AGGRender.h` - AGG implementation
  - `src/servers/app/drawing/Painter/` - Painter classes for different rendering operations

### JAM Build System Architecture
JAM is Haiku's primary build system with custom rules:
- **Jamfile**: Main build entry point, includes all subdirectories
- **build/jam/**: JAM build rules and configuration
- **UserBuildConfig**: Optional user customizations (copy from `UserBuildConfig.sample`)
- Architecture support: x86, x86_64, arm, arm64, riscv64

## Multi-Architecture Support

### Supported Architectures
- **x86_64**: Primary desktop platform
- **x86**: Legacy 32-bit support
- **arm64**: ARM 64-bit (Raspberry Pi, Apple Silicon)
- **arm**: ARM 32-bit
- **riscv64**: RISC-V 64-bit

### Cross-Compilation
```bash
# Build cross-compilation toolchain
./configure --build-cross-tools <arch>
./configure --build-cross-tools x86_64
./configure --build-cross-tools arm64

# Architecture-specific builds
export HAIKU_OUTPUT_DIR=generated_<arch>
jam -qa <target>
```

## Development Workflow

### Working with JAM
- Use `SubDir` and `SubInclude` for directory hierarchies
- Key JAM rules: `Application`, `SharedLibrary`, `StaticLibrary`, `Objects`
- Conditional compilation with `if $(TARGET_ARCH) = <arch>`
- Resource compilation with `.rdef` files

### Code Organization Patterns
- **Kits follow BeOS API structure**: Each kit has public headers in `headers/os/`
- **Private implementations**: Internal headers in `headers/private/`
- **Server architecture**: Servers use message-passing between client and server components
- **Add-on architecture**: Extensible through translator, codec, and driver add-ons

### Graphics Development (Current Focus)
When working on graphics code:
1. Use abstract `IRenderEngine` interface for new rendering code
2. Implement concrete functionality in `AGGRender_*.cpp` files
3. Consider Blend2D migration compatibility when adding features
4. Test across multiple architectures
5. Follow existing AGG → abstraction → implementation pattern

## Build Configuration

### Environment Variables
- `HAIKU_OUTPUT_DIR`: Override default build output directory
- `TARGET_ARCH`: Target architecture (set automatically by configure)
- `HAIKU_TOP`: Repository root (set automatically)

### UserBuildConfig Options
Copy `build/jam/UserBuildConfig.sample` to `UserBuildConfig` and customize:
- `HAIKU_IMAGE_SIZE`: Disk image size in MB
- `DEBUG`: Enable debugging for specific directories
- Architecture-specific optimizations and feature flags

### Cross-Tools Integration
The build system integrates with cross-compilation toolchains:
```bash
# Tools location after configure
/path/to/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-gcc
```

## Testing and Validation

### Component Testing
```bash
# Build and run tests for specific kits
jam RunTests <kit_name>

# Application server testing
jam app_server
# Run with test data

# Graphics validation tools
./build_cmake/test_abi_validation.sh
```

### Build Validation
- Compare Meson vs JAM builds for equivalent components
- Validate cross-architecture compatibility
- Symbol export validation: `nm -D <library> | grep " T "`

## Current Development Areas

### Active Migrations
1. **Graphics Backend**: AGG → Blend2D migration research complete, implementation in progress
2. **Build System**: JAM → Meson hybrid approach for select components
3. **Render Engine**: Abstraction layer implementation ongoing

### Key Implementation Files
- Graphics abstraction: `src/servers/app/render/`
- Meson integration: `meson.build`, `src/libs/meson.build`
- Cross-architecture support: Architecture-specific directories in `src/system/`

When contributing, follow existing patterns in similar components and maintain compatibility across all supported architectures.