# Haiku Jam Build System Rules Documentation - Part 1: Core Rules

## Overview

This is Part 1 of the comprehensive Haiku Jam rules documentation, covering core application building, library creation, and kernel rules. The complete documentation is divided into three parts to manage complexity:

- **Part 1**: Core Rules, Application Building, Libraries, Kernel (this document)
- **Part 2**: Package Management, Image Building, File Operations
- **Part 3**: Advanced Features, Cross-compilation, Platform Configuration

## Table of Contents - Part 1

1. [Build System Overview](#build-system-overview)
2. [Core Application Rules](#core-application-rules)
3. [Static Library Rules](#static-library-rules)
4. [Shared Library Rules](#shared-library-rules)
5. [Kernel Build Rules](#kernel-build-rules)
6. [Boot Loader Rules](#boot-loader-rules)
7. [Object and Compilation Rules](#object-and-compilation-rules)
8. [Build Platform Rules](#build-platform-rules)

## Build System Overview

The Haiku build system is based on Jam with extensive custom rules defined in `/build/jam/`. The main entry point is the root `Jamfile` which includes build features, packages, and source directories.

### Key Rule Files:
- **MainBuildRules**: Core application and library build rules (30+ rules)
- **KernelRules**: Kernel-specific build rules (8 rules)
- **BootRules**: Boot loader build rules (8 rules)
- **OverriddenJamRules**: Enhanced versions of standard Jam rules (15+ rules)
- **ArchitectureRules**: Multi-architecture support (7 rules)
- **SystemLibraryRules**: System library management (12 rules)

### Build Flow:
1. **Configure**: Set up toolchain and architecture variables
2. **Compile**: Process source files into objects
3. **Link**: Create executables, libraries, or kernel modules
4. **Package**: Bundle files into .hpkg packages
5. **Image**: Assemble complete system image

## Core Application Rules

### AddSharedObjectGlueCode
**File**: MainBuildRules:1
```jam
AddSharedObjectGlueCode <target> : <isExecutable>
```

**Purpose**: Adds platform-specific startup/cleanup code to executables and libraries.

**Parameters**:
- `<target>`: The executable or library target
- `<isExecutable>`: true for executables, false for libraries

**Key Features**:
- Adds crti.o/crtn.o glue code for Haiku targets
- Links against libroot.so automatically
- Special handling for libroot (doesn't link against itself)
- Sets up -nostdlib linking with manual library specification

### Application
**File**: MainBuildRules:44
```jam
Application <name> : <sources> : <libraries> : <resources>
```

**Purpose**: Builds standard Haiku GUI applications and command-line tools.

**Parameters**:
- `<name>`: Application executable name
- `<sources>`: Source files (.cpp, .c, .S, .asm, .nasm, etc.)
- `<libraries>`: Libraries to link against ("be", "translation", etc.)
- `<resources>`: Resource files (.rdef, .rsrc)

**Generated Commands**:
```bash
# Compilation (per source file)
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) \
  $(TARGET_WARNING_CCFLAGS_$(TARGET_PACKAGING_ARCH)) $(TARGET_DEFINES_$(TARGET_PACKAGING_ARCH)) \
  -I$(headers) -c source.cpp -o object.o

# Linking
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) $(TARGET_LINKFLAGS_$(TARGET_PACKAGING_ARCH)) \
  -nostdlib -Xlinker --no-undefined -Xlinker -soname=_APP_ \
  -o executable $(LINK_BEGIN_GLUE) objects... $(NEEDLIBS) $(LINKLIBS) $(LINK_END_GLUE)
```

**Key Features**:
- Automatically links against libroot.so and libgcc.a
- Sets soname to "_APP_" for applications
- Supports resource embedding via xres
- Platform-aware compilation (host vs target)

### StdBinCommands
**File**: MainBuildRules:63
```jam
StdBinCommands <sources> : <libraries> : <resources>
```

**Purpose**: Builds multiple simple command-line utilities from individual source files.

**Parameters**:
- `<sources>`: List of source files (each becomes separate executable)
- `<libraries>`: Libraries to link all executables against
- `<resources>`: Resources to embed in all executables

**Usage Example**:
```jam
StdBinCommands cat.cpp ls.cpp mkdir.cpp : $(TARGET_LIBSUPC++) ;
```

### Addon
**File**: MainBuildRules:77
```jam
Addon <target> : <sources> : <libraries> : <isExecutable>
```

**Purpose**: Builds add-ons (shared libraries that can optionally be executable).

**Parameters**:
- `<target>`: Add-on name
- `<sources>`: Source files
- `<libraries>`: Libraries to link against
- `<isExecutable>`: "true" if add-on should also be executable

**Generated Commands**:
```bash
# Non-executable add-on
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -shared -Xlinker -soname="target_name" \
  -nostdlib -o target objects... $(NEEDLIBS) $(LINKLIBS)

# Executable add-on
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -Xlinker -soname="target_name" \
  -nostdlib -o target objects... $(NEEDLIBS) $(LINKLIBS)
```

### Translator
**File**: MainBuildRules:101
```jam
Translator <target> : <sources> : <libraries> : <isExecutable>
```

**Purpose**: Alias for Addon rule, specifically for Translation Kit translators.

### ScreenSaver
**File**: MainBuildRules:107
```jam
ScreenSaver <target> : <sources> : <libraries>
```

**Purpose**: Builds screen saver add-ons (always non-executable).

## Static Library Rules

### StaticLibrary
**File**: MainBuildRules:113
```jam
StaticLibrary <lib> : <sources> : <otherObjects>
```

**Purpose**: Creates static libraries from source files with automatic symbol hiding.

**Parameters**:
- `<lib>`: Static library name (e.g., "libmath.a")
- `<sources>`: Source files to compile
- `<otherObjects>`: Additional pre-compiled object files

**Generated Commands**:
```bash
# Compilation (with hidden visibility for modern GCC)
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) \
  -fvisibility=hidden -I$(headers) -c source.cpp -o object.o

# Archive creation
$(TARGET_AR_$(TARGET_PACKAGING_ARCH)) $(ARFLAGS) library.a objects...
$(TARGET_RANLIB_$(TARGET_PACKAGING_ARCH)) library.a
```

**Key Features**:
- Automatically adds `-fvisibility=hidden` for modern GCC compilers
- Skips visibility flags for legacy GCC (x86_gcc2)
- Supports inclusion of additional object files

### StaticLibraryFromObjects
**File**: MainBuildRules:142
```jam
StaticLibraryFromObjects <lib> : <objects>
```

**Purpose**: Creates static library from pre-compiled object files.

### BuildPlatformStaticLibrary
**File**: MainBuildRules:698
```jam
BuildPlatformStaticLibrary <lib> : <sources> : <otherObjects>
```

**Purpose**: Builds static libraries for the host platform (build tools).

**Generated Commands**:
```bash
$(HOST_CC) $(HOST_CCFLAGS) $(HOST_WARNING_CCFLAGS) -c source.cpp -o object.o
$(HOST_AR) $(HOST_ARFLAGS) library.a objects...
```

### BuildPlatformStaticLibraryPIC
**File**: MainBuildRules:721
```jam
BuildPlatformStaticLibraryPIC <target> : <sources> : <otherObjects>
```

**Purpose**: Builds position-independent static libraries for host platform.

## Shared Library Rules

### SharedLibrary
**File**: MainBuildRules:434
```jam
SharedLibrary <lib> : <sources> : <libraries> : <version>
```

**Purpose**: Creates shared libraries (.so files) with version support.

**Parameters**:
- `<lib>`: Shared library name (e.g., "libtranslation.so")
- `<sources>`: Source files to compile
- `<libraries>`: Libraries to link against
- `<version>`: Version string for soname

**Generated Commands**:
```bash
# Compilation with PIC
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) \
  -fPIC -I$(headers) -c source.cpp -o object.o

# Linking
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) $(TARGET_LINKFLAGS_$(TARGET_PACKAGING_ARCH)) \
  -shared -nostdlib -Xlinker -soname=libname.so.version \
  -o library.so $(LINK_BEGIN_GLUE) objects... $(NEEDLIBS) $(LINKLIBS) $(LINK_END_GLUE)
```

### SharedLibraryFromObjects
**File**: MainBuildRules:413
```jam
SharedLibraryFromObjects <lib> : <objects> : <libraries> : <version>
```

**Purpose**: Creates shared library from pre-compiled object files.

### BuildPlatformSharedLibrary
**File**: MainBuildRules:629
```jam
BuildPlatformSharedLibrary <lib> : <sources> : <libraries>
```

**Purpose**: Builds shared libraries for the host platform.

## Kernel Build Rules

### SetupKernel
**File**: KernelRules:1
```jam
SetupKernel <sources_or_objects> : <extra_cc_flags> : <include_private_headers>
```

**Purpose**: Configures source files for kernel compilation with special flags and headers.

**Parameters**:
- `<sources_or_objects>`: Source files or objects to configure
- `<extra_cc_flags>`: Additional compiler flags
- `<include_private_headers>`: false to skip private kernel headers

**Key Features**:
- Adds private kernel headers by default
- Sets TARGET_PACKAGING_ARCH to TARGET_KERNEL_ARCH
- Applies kernel-specific compiler flags
- Overrides regular CCFLAGS/C++FLAGS

### KernelObjects
**File**: KernelRules:38
```jam
KernelObjects <sources> : <extra_cc_flags>
```

**Purpose**: Compiles source files for kernel use with kernel-specific settings.

### KernelLd
**File**: KernelRules:44
```jam
KernelLd <name> : <objects> : <linkerscript> : <args>
```

**Purpose**: Links kernel objects with custom linker scripts and kernel libraries.

**Parameters**:
- `<name>`: Output kernel module name
- `<objects>`: Object files to link
- `<linkerscript>`: Custom linker script (.ld file)
- `<args>`: Additional linker arguments

**Generated Commands**:
```bash
$(TARGET_LD_$(HAIKU_KERNEL_ARCH)) $(LINKFLAGS) --script=linker.ld \
  -o kernel_module objects... $(LINKLIBS) --version-script=VERSION_SCRIPT
```

**Key Features**:
- Uses kernel-specific linker and architecture
- Links against kernel libgcc and optionally libsupc++
- Supports custom linker scripts for memory layout
- Handles kernel symbol versioning

### KernelSo
**File**: KernelRules:93
```jam
KernelSo <target> : <source>
```

**Purpose**: Creates shared object version of kernel for module linking.

### KernelAddon
**File**: KernelRules:111
```jam
KernelAddon <addon> : <sources> : <libraries>
```

**Purpose**: Builds kernel add-ons (drivers, file systems, etc.).

**Generated Commands**:
```bash
# Links as shared object with kernel-specific settings
$(TARGET_LD_$(HAIKU_KERNEL_ARCH)) -shared -nostdlib \
  -o addon objects... kernel_libraries...
```

### KernelMergeObject
**File**: KernelRules:160
```jam
KernelMergeObject <object> : <sources> : <extra_cc_flags>
```

**Purpose**: Compiles and merges multiple source files into single object for kernel.

### KernelStaticLibrary
**File**: KernelRules:178
```jam
KernelStaticLibrary <lib> : <sources> : <extra_cc_flags>
```

**Purpose**: Creates static libraries for kernel use.

### KernelStaticLibraryObjects
**File**: KernelRules:196
```jam
KernelStaticLibraryObjects <lib> : <objects>
```

**Purpose**: Creates kernel static library from pre-compiled objects.

## Boot Loader Rules

### MultiBootSubDirSetup
**File**: BootRules:1
```jam
MultiBootSubDirSetup <bootTargets>
```

**Purpose**: Sets up subdirectory for building multiple boot targets.

### MultiBootGristFiles
**File**: BootRules:58
```jam
MultiBootGristFiles <files>
```

**Purpose**: Adds appropriate grist to boot loader files.

### SetupBoot
**File**: BootRules:63
```jam
SetupBoot <sources> : <extra_cc_flags> : <extra_c++_flags>
```

**Purpose**: Configures sources for boot loader compilation.

**Key Features**:
- Uses boot-specific compiler flags
- Links against boot loader libraries
- Sets up boot-specific include paths

### BootObjects
**File**: BootRules:115
```jam
BootObjects <sources> : <extra_cc_flags> : <extra_c++_flags>
```

**Purpose**: Compiles objects for boot loader use.

### BootLd
**File**: BootRules:121
```jam
BootLd <target> : <objects> : <linkerscript> : <args>
```

**Purpose**: Links boot loader with custom linker scripts.

**Generated Commands**:
```bash
$(TARGET_LD_$(TARGET_BOOT_ARCH)) $(LINKFLAGS) --script=boot.ld \
  -o boot_loader objects... $(BOOT_LINKLIBS)
```

### BootMergeObject
**File**: BootRules:165
```jam
BootMergeObject <object> : <sources> : <extra_cc_flags> : <extra_c++_flags>
```

**Purpose**: Merges multiple source files into single boot loader object.

### BootStaticLibrary
**File**: BootRules:184
```jam
BootStaticLibrary <lib> : <sources> : <extra_cc_flags> : <extra_c++_flags>
```

**Purpose**: Creates static libraries for boot loader.

### BootStaticLibraryObjects
**File**: BootRules:202
```jam
BootStaticLibraryObjects <lib> : <objects>
```

**Purpose**: Creates boot static library from objects.

### BuildMBR
**File**: BootRules:227
```jam
BuildMBR <binary> : <source>
```

**Purpose**: Builds Master Boot Record binary.

## Object and Compilation Rules

### AssembleNasm
**File**: MainBuildRules:151
```jam
AssembleNasm <object> : <source>
```

**Purpose**: Assembles NASM assembly files.

**Generated Commands**:
```bash
$(NASM) $(NASMFLAGS) -o object.o source.asm
```

### Ld
**File**: MainBuildRules:169
```jam
Ld <target> : <objects> : <linkerscript> : <args>
```

**Purpose**: Generic linking rule with custom linker script support.

### MergeObject
**File**: MainBuildRules:390
```jam
MergeObject <target> : <objects>
```

**Purpose**: Merges multiple object files into single object.

**Generated Commands**:
```bash
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -r -o merged.o objects...
```

### MergeObjectFromObjects
**File**: MainBuildRules:352
```jam
MergeObjectFromObjects <target> : <objects> : <otherObjects>
```

**Purpose**: Merges objects with additional objects for complex builds.

### CreateAsmStructOffsetsHeader
**File**: MainBuildRules:215
```jam
CreateAsmStructOffsetsHeader <header> : <source> : <architecture>
```

**Purpose**: Generates assembly headers with C structure offsets for kernel/low-level code.

**Generated Commands**:
```bash
# Compile special C file to extract structure layouts
$(TARGET_CC_$(architecture)) -S -o temp.s source.c
# Process assembly to create header with structure offsets
generate_asm_offsets.sh temp.s > header.h
```

## Build Platform Rules

### BuildPlatformObjects
**File**: MainBuildRules:584
```jam
BuildPlatformObjects <sources> : <extra_cc_flags> : <extra_c++_flags>
```

**Purpose**: Compiles objects for host platform (build tools).

### BuildPlatformMain
**File**: MainBuildRules:598
```jam
BuildPlatformMain <target> : <sources> : <libraries>
```

**Purpose**: Builds executables for host platform.

**Generated Commands**:
```bash
$(HOST_CC) $(HOST_CCFLAGS) $(HOST_WARNING_CCFLAGS) -c source.cpp -o object.o
$(HOST_LD) $(HOST_LINKFLAGS) -o executable objects... $(HOST_LINKLIBS)
```

### BuildPlatformMergeObject
**File**: MainBuildRules:655
```jam
BuildPlatformMergeObject <target> : <sources> : <otherObjects>
```

**Purpose**: Merges objects for host platform builds.

### BuildPlatformMergeObjectPIC
**File**: MainBuildRules:681
```jam
BuildPlatformMergeObjectPIC <target> : <sources> : <otherObjects>
```

**Purpose**: Merges position-independent objects for host platform.

### BootstrapStage0PlatformObjects
**File**: MainBuildRules:731
```jam
BootstrapStage0PlatformObjects <sources> : <separateFromStandardSiblings>
```

**Purpose**: Compiles stage 0 bootstrap objects for cross-compilation setup.

## Enhanced Jam Rules (OverriddenJamRules)

The following rules enhance or replace standard Jam rules with Haiku-specific functionality:

### Link (Enhanced)
**File**: OverriddenJamRules:5
- Enhanced to handle Haiku-specific linking requirements
- Supports BeOS/Haiku file attributes
- Integrates with resource system (XRes)
- Platform-aware linking (host vs target)

### Object (Enhanced)
**File**: OverriddenJamRules:68
- Multi-architecture compilation support
- Enhanced header dependency tracking
- Platform-specific compiler selection

### Library (Enhanced)
**File**: OverriddenJamRules:420
- Architecture-aware library creation
- Enhanced dependency management

### As (Enhanced)
**File**: OverriddenJamRules:177
- Architecture-specific assembler flags
- Enhanced include path handling

### Cc (Enhanced)
**File**: OverriddenJamRules:258
- Platform and architecture-specific compilation
- Debug/optimization flag management
- Warning level control

### C++ (Enhanced)
**File**: OverriddenJamRules:335
- C++ specific flags and headers
- Exception handling configuration

## System Library Rules

### TargetLibstdc++
**File**: SystemLibraryRules:18
```jam
TargetLibstdc++ <asPath>
```

**Purpose**: Returns appropriate C++ standard library for target architecture.

### TargetLibsupc++
**File**: SystemLibraryRules:43
```jam
TargetLibsupc++ <asPath>
```

**Purpose**: Returns C++ runtime support library for target.

### TargetLibgcc
**File**: SystemLibraryRules:157
```jam
TargetLibgcc <asPath>
```

**Purpose**: Returns GCC runtime library for target architecture.

### TargetKernelLibsupc++
**File**: SystemLibraryRules:92
```jam
TargetKernelLibsupc++ <asPath>
```

**Purpose**: Returns kernel-specific C++ runtime support library.

### TargetKernelLibgcc
**File**: SystemLibraryRules:214
```jam
TargetKernelLibgcc <asPath>
```

**Purpose**: Returns kernel-specific GCC runtime library.

### TargetBootLibgcc
**File**: SystemLibraryRules:237
```jam
TargetBootLibgcc <architecture> : <asPath>
```

**Purpose**: Returns boot loader specific GCC library for architecture.

---

This concludes Part 1 of the Haiku Jam Rules Documentation. Continue with Part 2 for Package Management, Image Building, and File Operations, and Part 3 for Advanced Features and Cross-compilation setup.