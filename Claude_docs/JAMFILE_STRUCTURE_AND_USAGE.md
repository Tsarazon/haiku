# Haiku Jamfile Structure and Usage Guide

## Overview

The Haiku operating system uses a sophisticated build system based on Perforce Jam with custom extensions and rules. This document analyzes the structure and usage of Jamfiles throughout the Haiku source tree, identifying the most complex custom Jam rules and their purposes.

## Build System Architecture

### Main Components

1. **Root Jamfile** (`/Jamfile`) - Entry point for the build system
2. **Jamrules** (`/Jamrules`) - Main configuration and rule loading
3. **Build Rules** (`/build/jam/`) - Custom rule definitions organized by category
4. **Source Jamfiles** - Distributed throughout the source tree for specific components

### Directory Structure

```
haiku/
├── Jamfile                    # Main build entry point
├── Jamrules                   # Central rule loader and configuration
├── build/jam/                 # Custom build rule definitions
│   ├── ArchitectureRules      # Multi-architecture support rules
│   ├── BuildSetup             # Build environment configuration
│   ├── ImageRules             # OS image generation rules
│   ├── KernelRules            # Kernel-specific build rules
│   ├── MainBuildRules         # Core application/library rules
│   ├── PackageRules           # Package management rules
│   └── ...                    # Additional specialized rule files
└── src/                       # Source code with distributed Jamfiles
    ├── system/kernel/Jamfile  # Kernel build configuration
    ├── kits/Jamfile          # System libraries build
    └── ...                   # Component-specific Jamfiles
```

## Core Jamfile Patterns

### 1. SubDir Declaration
Every Jamfile starts with a SubDir declaration to establish the current directory context:
```jam
SubDir HAIKU_TOP src system kernel ;
```

### 2. Architecture-Specific Configuration
Multi-architecture builds are handled through specialized patterns:
```jam
local architectureObject ;
for architectureObject in [ MultiArchSubDirSetup ] {
    on $(architectureObject) {
        # Architecture-specific rules
    }
}
```

### 3. Component Building
Common patterns for building different types of components:
```jam
# Kernel objects
KernelMergeObject kernel_core.o : source_files.cpp ;

# Applications
Application AppName : sources : libraries : resources ;

# Shared libraries
SharedLibrary libname.so : sources : libraries ;

# Static libraries
StaticLibrary libname.a : sources ;
```

## Top 10 Most Complex/Custom Jam Rules

### 1. **BuildHaikuImage** (`ImageRules:1294`)
**Purpose**: Creates complete Haiku OS images including boot loader, kernel, and filesystem
**Complexity**: High - Orchestrates multiple scripts and handles different image types (VMware, regular)
**Key Features**:
- Manages image vs. directory builds
- Handles VMware-specific image generation
- Coordinates multiple build scripts
- Supports both development and release image configurations

### 2. **ArchitectureSetup** (`ArchitectureRules:1`)
**Purpose**: Configures compilation flags and environment for specific CPU architectures
**Complexity**: Very High - Handles x86, ARM64, PowerPC, RISC-V, and other architectures
**Key Features**:
- Sets architecture-specific compiler flags (-march, -mcpu)
- Configures cross-compilation toolchains
- Manages warning flags per architecture
- Handles legacy GCC vs modern compiler differences
- Sets up debugging and optimization levels

### 3. **MultiArchSubDirSetup** (`ArchitectureRules:913`)
**Purpose**: Enables building the same code for multiple architectures simultaneously
**Complexity**: High - Creates isolated build contexts for each target architecture
**Key Features**:
- Creates separate object directories per architecture
- Manages cross-architecture dependencies
- Handles primary vs secondary architecture builds
- Supports hybrid x86/x86_64 systems

### 4. **KernelLd** (`KernelRules:44`)
**Purpose**: Links kernel objects with special kernel-specific requirements
**Complexity**: High - Handles kernel linking with custom linker scripts and libraries
**Key Features**:
- Uses custom linker scripts for memory layout
- Links against kernel-specific libgcc/libsupc++
- Manages kernel symbol versioning
- Handles different kernel architectures

### 5. **AddSharedObjectGlueCode** (`MainBuildRules:1`)
**Purpose**: Adds platform-specific startup/cleanup code to executables and libraries
**Complexity**: Medium-High - Handles cross-platform executable format differences
**Key Features**:
- Manages Haiku-specific runtime linking
- Adds begin/end glue code for executables vs libraries
- Handles libroot linking (special case for libroot itself)
- Configures compatibility libraries

### 6. **HaikuPackage** (`PackageRules:8`)
**Purpose**: Creates Haiku package (.hpkg) files with proper metadata and dependencies
**Complexity**: High - Manages package creation with complex dependency resolution
**Key Features**:
- Handles package metadata and dependencies
- Supports incremental package updates
- Manages primary/secondary architecture packages
- Integrates with repository systems

### 7. **BuildHaikuImagePackageList** (`ImageRules:1191`)
**Purpose**: Generates the list of packages to include in OS images
**Complexity**: Medium-High - Resolves complex package dependencies and profiles
**Key Features**:
- Handles build profiles (minimum, regular, development)
- Resolves package dependencies
- Manages optional vs required packages
- Supports different package repositories

### 8. **PreprocessPackageOrRepositoryInfo** (`PackageRules:83`)
**Purpose**: Processes package info templates with architecture-specific substitutions
**Complexity**: Medium-High - Template processing with conditional compilation
**Key Features**:
- Performs placeholder substitutions
- Supports C preprocessor integration
- Handles architecture-specific package information
- Updates package requirements automatically

### 9. **CreateAsmStructOffsetsHeader** (`MainBuildRules:215`)
**Purpose**: Generates assembly headers with C structure offsets for kernel/low-level code
**Complexity**: Medium-High - Cross-language integration between C and assembly
**Key Features**:
- Compiles special C files to extract structure layouts
- Generates assembly-compatible headers
- Handles architecture-specific structure layouts
- Supports kernel and bootloader development

### 10. **AddDirectoryToHaikuImage** (`ImageRules:936`)
**Purpose**: Adds directory structures to the Haiku filesystem image
**Complexity**: Medium - Manages filesystem layout and attributes
**Key Features**:
- Creates directory hierarchies in images
- Handles Haiku-specific file attributes
- Manages permissions and ownership
- Supports both files and symbolic links

## Jamfile Usage Patterns

### Build Targets

1. **Kernel Components**
   ```jam
   KernelObjects arch_cpu.cpp arch_thread.cpp ;
   KernelMergeObject arch_$(TARGET_ARCH).o : <sources> ;
   ```

2. **User Applications**
   ```jam
   Application Terminal : source_files : be bnetapi : Terminal.rdef ;
   ```

3. **System Libraries**
   ```jam
   SharedLibrary libbe.so : source_files : <system_libs> ;
   ```

4. **Device Drivers**
   ```jam
   KernelAddon usb_hid : driver_sources : kernel_libs ;
   ```

### Architecture Support

The build system supports multiple architectures through:
- Cross-compilation toolchain configuration
- Architecture-specific source file selection
- Platform-specific compiler flags
- Multi-architecture package generation

### Package Management Integration

Jamfiles integrate tightly with Haiku's package management:
- Automatic dependency resolution
- Package metadata generation
- Repository management
- Version tracking and updates

## Advanced Features

### 1. **Conditional Compilation**
Build features can be conditionally enabled:
```jam
if [ FIsBuildFeatureEnabled openssl ] {
    AddHaikuImageSystemPackages openssl3 ;
}
```

### 2. **Cross-Architecture Dependencies**
Primary and secondary architectures are handled seamlessly:
```jam
local architectureObject ;
for architectureObject in [ MultiArchSubDirSetup $(HAIKU_PACKAGING_ARCHS[2-]) ] {
    on $(architectureObject) {
        # Secondary architecture build
    }
}
```

### 3. **Build Profiles**
Different image configurations (minimum, regular, development) are supported through build profiles that determine which packages and features to include.

### 4. **Incremental Builds**
The system supports incremental builds through:
- Smart dependency tracking
- Package update detection
- Selective rebuilding based on changes

## Best Practices

1. **Always use SubDir** at the beginning of Jamfiles
2. **Declare dependencies explicitly** with UsePrivateHeaders/UseHeaders
3. **Use architecture-aware rules** for cross-platform components
4. **Follow naming conventions** for targets and variables
5. **Group related sources** using merge objects for better organization
6. **Handle conditional features** with proper feature detection

## Conclusion

The Haiku build system represents one of the most sophisticated Jam-based build systems, with custom rules that handle:
- Multi-architecture cross-compilation
- Complex package management
- Operating system image generation
- Kernel and user-space integration
- Incremental and distributed builds

The top 10 rules identified demonstrate the system's capability to handle the full spectrum of OS development needs, from low-level kernel linking to high-level package management and image creation.