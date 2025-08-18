# Haiku Jam Build System - Complete Reference

## Overview

This document provides a comprehensive reference for all custom variables and functions defined in Haiku's Jam-based build system, with special emphasis on resource file handling, localization catalogs, and package creation.

## Table of Contents

1. [Core Build Variables](#core-build-variables)
2. [Resource File (.rdef) Handling](#resource-file-rdef-handling)
3. [Localization Catalog System](#localization-catalog-system)
4. [Package (.hpkg) Creation](#package-hpkg-creation)
5. [Architecture Support Variables](#architecture-support-variables)
6. [Container and Image Variables](#container-and-image-variables)
7. [Build Configuration Variables](#build-configuration-variables)
8. [Custom Build Functions](#custom-build-functions)

## Core Build Variables

### Main Build System Variables

| Variable | Purpose | Example Value |
|----------|---------|---------------|
| `HAIKU_VERSION` | Current Haiku version string | `r1~beta5` |
| `HAIKU_TOP` | Root directory of source tree | `.` or `..` |
| `HAIKU_OUTPUT_DIR` | Generated files directory | `generated` |
| `HAIKU_BUILD_OUTPUT_DIR` | Build output directory | `generated/build` |
| `HAIKU_OBJECT_DIR` | Object files directory | `generated/objects` |
| `HAIKU_PACKAGING_ARCH` | Primary packaging architecture | `x86_64` |
| `HAIKU_PACKAGING_ARCHS` | All packaging architectures | `x86_gcc2 x86_64` |

### Build Type Variables

| Variable | Purpose | Values |
|----------|---------|--------|
| `HAIKU_BUILD_TYPE` | Current build type | `bootstrap`, `minimum`, `regular` |
| `HAIKU_IS_BOOTSTRAP` | Bootstrap build flag | `1` or empty |
| `HAIKU_BUILD_PROFILE` | Build profile selection | `@minimum`, `@regular`, `@development` |

### Optional Feature Variables

| Variable | Purpose | Usage |
|----------|---------|-------|
| `HAIKU_BUILD_FEATURES` | Enabled build features | `ssl:openssl` `icu` |
| `HAIKU_BUILD_FEATURE_SSL` | SSL support enabled | `1` or `0` |
| `HAIKU_DONT_CLEAR_IMAGE` | Preserve image on rebuild | `1` or empty |
| `HAIKU_DONT_INCLUDE_SRC` | Skip source inclusion | `1` or empty |

## Resource File (.rdef) Handling

### Core Functions

#### `AddResources`
```jam
rule AddResources target : resourceFiles
```
**Purpose**: Adds resource files to a target, automatically compiling .rdef files to .rsrc format.

**Key Features**:
- Automatically detects .rdef files and compiles them
- Sets up proper dependencies and search paths
- Handles grist (unique identifiers) for files

**Variables Set**:
- `RESFILES` on target: List of compiled resource files
- `SEARCH` on resources: Search paths for resource files

#### `ResComp`
```jam
rule ResComp resourceFile : rdefFile
```
**Purpose**: Compiles .rdef (resource definition) files to .rsrc (compiled resource) files.

**Process**:
1. Preprocesses .rdef file through C preprocessor
2. Strips preprocessor directives
3. Compiles to binary resource format using `rc` tool

**Variables Used**:
- `CC`: C compiler for preprocessing
- `CCFLAGS`: Compiler flags
- `CCDEFS`: Preprocessor definitions  
- `HDRS`: Include paths for preprocessing
- `RCHDRS`: Resource compiler include paths

#### `ResAttr`
```jam
rule ResAttr attributeFile : resourceFiles : deleteAttributeFile
```
**Purpose**: Adds resource files as Haiku file attributes.

**Features**:
- Handles both .rdef and .rsrc files
- Can compile .rdef files on-the-fly
- Optional clearing of existing attributes

### Resource Processing Variables

| Variable | Purpose | Scope |
|----------|---------|-------|
| `RESFILES` | List of resource files for target | Per-target |
| `RCHDRS` | Include paths for resource compiler | Per-file |
| `HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR` | Host compatibility setup | Global |

### Actions

#### `ResComp1`
```bash
cat "rdef_files" | $(CC) $(CCFLAGS) -E $(CCDEFS) $(HDRS) - \
    | grep -E -v '^#' | $(RC) $(RCHDRS) --auto-names -o "output" -
```
Preprocesses and compiles resource definition files.

## Localization Catalog System

### Core Functions

#### `DoCatalogs`
```jam
rule DoCatalogs target : signature : sources : sourceLanguage : regexp
```
**Purpose**: Complete localization setup - extracts catalog entries, generates default catalog, and processes translations.

**Parameters**:
- `target`: Target being localized
- `signature`: Application MIME signature (e.g., "application/x-vnd.Haiku-Terminal")
- `sources`: C++ source files to scan for localizable strings
- `sourceLanguage`: Source language code (default: "en")
- `regexp`: Custom regex for string extraction (optional)

**Process**:
1. Extracts translatable strings from source files
2. Generates .catkeys files
3. Finds existing translations in data/catalogs/
4. Compiles all translations to .catalog files
5. Sets up dependencies for image inclusion

#### `ExtractCatalogEntries`
```jam
rule ExtractCatalogEntries target : sources : signature : regexp
```
**Purpose**: Extracts translatable strings from source code.

**Process**:
1. Preprocesses sources with C preprocessor
2. Runs `collectcatkeys` tool to extract B_TRANSLATE() calls
3. Generates .catkeys file with extracted strings

#### `LinkApplicationCatalog`
```jam
rule LinkApplicationCatalog target : sources : signature : language
```
**Purpose**: Compiles .catkeys files to binary .catalog format.

**Variables Used**:
- `HAIKU_CATALOG_SIGNATURE`: Application signature
- `HAIKU_CATALOG_LANGUAGE`: Target language code

### Localization Variables

| Variable | Purpose | Example |
|----------|---------|---------|
| `HAIKU_CATALOG_FILES` | Compiled catalog files for target | `Terminal.catalog` `Terminal-de.catalog` |
| `HAIKU_CATALOG_SIGNATURE` | Application MIME signature | `application/x-vnd.Haiku-Terminal` |
| `HAIKU_CATALOGS_SUBDIR` | Subdirectory for catalogs | `apps Terminal` |
| `HAIKU_CATALOGS_OBJECT_DIR` | Catalog build directory | `generated/objects/catalogs` |
| `HAIKU_LOCALIZED_TARGETS` | Targets with localization | List of targets |
| `HAIKU_LOCALE_CATALOGS` | All catalog files | Global list |
| `HAIKU_LOCALE_OUTPUT_CATKEYS` | Generated catkeys files | Global list |
| `HAIKU_MULTIPLE_LOCALIZED_TARGETS` | Multiple targets per directory | Boolean flag |

### Catalog Processing Actions

#### `ExtractCatalogEntries1`
```bash
$(CC) -E $(CCDEFS) -DB_COLLECTING_CATKEYS $(HDRS) "sources" > temp.pre
$(COLLECTCATKEYS) $(HAIKU_CATALOG_REGEXP) -s $(HAIKU_CATALOG_SIGNATURE) \
    -w -o "output.catkeys" "temp.pre"
rm "temp.pre"
```

#### `LinkApplicationCatalog1`
```bash
$(LINKCATKEYS) "input.catkeys" -l $(HAIKU_CATALOG_LANGUAGE) \
    -s $(HAIKU_CATALOG_SIGNATURE) -o "output.catalog"
```

### Catalog Integration

Catalogs are automatically added to images through:
```jam
# In ImageRules - automatic catalog inclusion
local catalogs = [ on $(target) return $(HAIKU_CATALOG_FILES) ] ;
if $(catalogs) {
    AddFilesToContainer $(container) 
        : $(systemDirTokens) data locale catalogs $(signature)
        : $(catalogs) ;
}
```

## Package (.hpkg) Creation

### Core Functions

#### `HaikuPackage`
```jam
rule HaikuPackage package
```
**Purpose**: Initializes a Haiku package container for building .hpkg files.

**Setup**:
- Creates unique grist for package isolation
- Sets up container variables
- Handles update-only mode
- Manages rebuild prevention

#### `BuildHaikuPackage`
```jam
rule BuildHaikuPackage package : packageInfo
```
**Purpose**: Builds a complete .hpkg package file.

**Process**:
1. Preprocesses package info template
2. Creates temporary build directory
3. Generates shell scripts for package contents
4. Calls main package build script
5. Creates final .hpkg file

#### `PreprocessPackageInfo`
```jam
rule PreprocessPackageInfo source : directory : architecture : secondaryArchitecture
```
**Purpose**: Processes package info templates with architecture-specific substitutions.

**Substitutions**:
- `%HAIKU_PACKAGING_ARCH%` → actual architecture
- `%HAIKU_SECONDARY_PACKAGING_ARCH%` → secondary architecture
- `%HAIKU_VERSION%` → version with revision
- Build feature flags

### Package Variables

| Variable | Purpose | Scope |
|----------|---------|-------|
| `HAIKU_CONTAINER_GRIST` | Unique package identifier | Per-package |
| `HAIKU_CURRENTLY_BUILT_HAIKU_PACKAGE` | Active package being built | Global |
| `HAIKU_PACKAGES_DIR_$(arch)` | Package output directory | Per-architecture |
| `HAIKU_PACKAGES_BUILD_DIR_$(arch)` | Package build temp directory | Per-architecture |
| `HAIKU_PACKAGE_COMPRESSION_LEVEL` | Compression level (0-9) | Per-package |
| `HAIKU_DONT_REBUILD_PACKAGES` | Skip existing packages | Global flag |
| `HAIKU_PACKAGES_UPDATE_ONLY` | Update mode only | Global flag |

### Package Content Functions

#### `AddFilesToPackage`
```jam
rule AddFilesToPackage directory : targets : destName
```
Adds files to package at specified directory.

#### `AddDirectoryToPackage`
```jam
rule AddDirectoryToPackage directoryTokens : attributeFiles
```
Creates directory structure in package.

#### `AddSymlinkToPackage`
```jam
rule AddSymlinkToPackage directoryTokens : linkTarget : linkName
```
Adds symbolic links to package.

#### `CopyDirectoryToPackage`
```jam
rule CopyDirectoryToPackage directoryTokens : sourceDirectory : targetDirectoryName : excludePatterns : flags
```
Copies entire directory trees to package.

### Package Build Scripts

The build system generates several scripts:

1. **Init Variables Script**: Sets up environment variables
2. **Make Directories Script**: Creates directory structure
3. **Copy Files Script**: Copies files to package
4. **Extract Files Script**: Extracts archives within package

#### Variables Added to Init Script

| Variable | Purpose | Source |
|----------|---------|--------|
| `sourceDir` | Source tree root | `$(HAIKU_TOP)` |
| `outputDir` | Build output directory | `$(HAIKU_OUTPUT_DIR)` |
| `tmpDir` | Package temp directory | Generated |
| `compressionLevel` | Package compression | `HAIKU_PACKAGE_COMPRESSION_LEVEL` |
| `addattr` | Path to addattr tool | `<build>addattr` |
| `package` | Path to package tool | `<build>package` |
| `mimeDB` | MIME database | `<mimedb>mime_db` |

### Package Actions

#### `BuildHaikuPackage1`
```bash
$(BUILD_HAIKU_PACKAGE_SCRIPT) "package.hpkg" "package-info" init-script make-dirs-script copy-files-script extract-files-script
```

## Architecture Support Variables

### Per-Architecture Variables

These variables are suffixed with architecture names (e.g., `_x86_64`, `_x86_gcc2`):

| Variable Pattern | Purpose |
|-----------------|---------|
| `HAIKU_CC_$(arch)` | C compiler for architecture |
| `HAIKU_C++_$(arch)` | C++ compiler for architecture |
| `HAIKU_LINK_$(arch)` | Linker for architecture |
| `HAIKU_CCFLAGS_$(arch)` | C compiler flags |
| `HAIKU_C++FLAGS_$(arch)` | C++ compiler flags |
| `HAIKU_LINKFLAGS_$(arch)` | Linker flags |
| `HAIKU_DEFINES_$(arch)` | Preprocessor definitions |
| `HAIKU_ARCH_OBJECT_DIR_$(arch)` | Object directory |
| `HAIKU_STRIP_$(arch)` | Strip tool |

### Architecture Setup Variables

| Variable | Purpose | Example |
|----------|---------|---------|
| `HAIKU_ARCH` | Primary CPU architecture | `x86_64` |
| `HAIKU_ARCHS` | All CPU architectures | `x86 x86_64` |
| `HAIKU_CPU_$(arch)` | CPU type for architecture | `x86`, `arm64`, `ppc` |
| `HAIKU_CC_IS_LEGACY_GCC_$(arch)` | GCC 2.x compatibility | `1` or empty |
| `HAIKU_CC_IS_CLANG_$(arch)` | Clang compiler detection | `1` or empty |

### Kernel Architecture Variables

| Variable | Purpose |
|----------|---------|
| `HAIKU_KERNEL_ARCH` | Kernel target architecture |
| `TARGET_KERNEL_ARCH` | Current kernel architecture |
| `TARGET_KERNEL_CCFLAGS` | Kernel-specific compiler flags |
| `TARGET_KERNEL_DEFINES` | Kernel preprocessor definitions |

## Container and Image Variables

### Container Types

Haiku uses multiple container types for different outputs:

#### Image Container
| Variable | Value |
|----------|-------|
| `HAIKU_IMAGE_CONTAINER_NAME` | `haiku-image-container` |
| `HAIKU_INCLUDE_IN_IMAGE` | Targets included in main image |
| `HAIKU_IMAGE_INSTALL_TARGETS` | Files to install in image |

#### Boot Archive Containers
| Container | Variable |
|-----------|----------|
| Net Boot | `HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME` |
| Floppy Boot | `HAIKU_FLOPPY_BOOT_IMAGE_CONTAINER_NAME` |
| CD Boot | `HAIKU_CD_BOOT_IMAGE_CONTAINER_NAME` |
| MMC Boot | `HAIKU_MMC_BOOT_IMAGE_CONTAINER_NAME` |

### Container Variables (Per-Container)

| Variable Pattern | Purpose |
|-----------------|---------|
| `HAIKU_CONTAINER_GRIST` | Unique container identifier |
| `HAIKU_INCLUDE_IN_CONTAINER_VAR` | Variable controlling inclusion |
| `HAIKU_INSTALL_TARGETS_VAR` | Variable for install targets |
| `HAIKU_CONTAINER_SYSTEM_DIR_TOKENS` | System directory structure |

## Build Configuration Variables

### User Configurable Variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `HAIKU_IMAGE_NAME` | Output image name | `haiku.image` |
| `HAIKU_IMAGE_SIZE` | Image size in MB | Varies by profile |
| `HAIKU_IMAGE_DIR` | Image output directory | `generated` |
| `HAIKU_IMAGE_LABEL` | Volume label | `Haiku` |
| `HAIKU_VMWARE_IMAGE_NAME` | VMware image name | `haiku.vmdk` |
| `HAIKU_INSTALL_DIR` | Installation directory | `/boot` |

### Build Control Variables

| Variable | Purpose | Values |
|----------|---------|--------|
| `HAIKU_REVISION` | Version revision | `hrev66666` |
| `HAIKU_USE_GCC_PIPE` | Enable GCC -pipe | `1` or `0` |
| `HAIKU_USE_GCC_GRAPHITE_$(arch)` | Graphite optimizations | `1` or `0` |
| `HAIKU_NO_DOWNLOADS` | Disable package downloads | `1` or `0` |
| `HAIKU_UPDATE_ALL_PACKAGES` | Force package updates | `1` or `0` |

### Math Variables (Internal)

Haiku implements arbitrary precision arithmetic in Jam:

| Variable | Purpose |
|----------|---------|
| `HAIKU_PAD_9` | Padding for digit operations |
| `HAIKU_DIGIT_GREATER_$(n)` | Digit comparison tables |
| `HAIKU_DIGIT_ADD_$(n)` | Digit addition tables |

## Custom Build Functions

### Core Application Functions

#### `Application`
```jam
rule Application name : sources : libraries : resources
```
Builds a Haiku application with resources and library dependencies.

#### `Addon`
```jam
rule Addon target : sources : libraries : isExecutable
```
Builds add-ons (shared libraries with special properties).

#### `SharedLibrary`
```jam
rule SharedLibrary library : sources : linkWith
```
Builds shared libraries (.so files).

#### `StaticLibrary`
```jam
rule StaticLibrary library : sources
```
Builds static libraries (.a files).

### Kernel Functions

#### `KernelObjects`
```jam
rule KernelObjects sources : flags
```
Compiles objects with kernel-specific flags.

#### `KernelMergeObject`
```jam
rule KernelMergeObject object : sources : flags
```
Creates merged object files for kernel modules.

#### `KernelAddon`
```jam
rule KernelAddon addon : sources : linkWith
```
Builds kernel add-ons (drivers, modules).

#### `KernelLd`
```jam
rule KernelLd executable : objects : linkerScript : flags
```
Links kernel executables with custom linker scripts.

### Utility Functions

#### `Copy`
```jam
rule Copy dest : src
```
Copies files with dependency tracking.

#### `SymLink`
```jam
rule SymLink link : target
```
Creates symbolic links.

#### `DataFileToSourceFile`
```jam
rule DataFileToSourceFile sourceFile : dataFile : dataVariable : sizeVariable
```
Converts binary data to C source arrays.

#### `DownloadFile`
```jam
rule DownloadFile file : url : source
```
Downloads files from URLs during build.

### Container Functions

#### `AddFilesToContainer`
```jam
rule AddFilesToContainer container : directory : targets : destName
```
Generic function for adding files to any container type.

#### `AddDirectoryToContainer`
```jam
rule AddDirectoryToContainer container : directoryTokens : attributeFiles
```
Creates directory structures in containers.

#### `CopyDirectoryToContainer`
```jam
rule CopyDirectoryToContainer container : directoryTokens : sourceDirectory : targetDirectoryName : excludePatterns : flags
```
Copies directory trees to containers.

### Build Profile Functions

#### `DefineDefaultBuildProfiles`
```jam
rule DefineDefaultBuildProfiles
```
Sets up standard build profiles (minimum, regular, development).

#### `DeferredSubInclude`
```jam
rule DeferredSubInclude dir : file : scope
```
Includes Jamfiles after main build setup is complete.

### Architecture Functions

#### `ArchitectureSetup`
```jam
rule ArchitectureSetup architecture
```
Configures build environment for specific CPU architecture.

#### `MultiArchSubDirSetup`
```jam
rule MultiArchSubDirSetup architectures
```
Sets up multi-architecture build contexts.

#### `MultiArchDefaultGristFiles`
```jam
rule MultiArchDefaultGristFiles files : gristPrefix : architecture
```
Creates architecture-specific file targets.

### Test Functions

#### `UnitTest`
```jam
rule UnitTest test : sources : libraries
```
Builds unit test executables.

#### `SimpleTest`
```jam
rule SimpleTest test : sources
```
Builds simple test programs.

#### `BuildPlatformTest`
```jam
rule BuildPlatformTest test : sources : libraries
```
Builds tests for build platform.

### Helper Functions

#### `FFilter`
```jam
rule FFilter list : pattern
```
Filters lists using glob patterns.

#### `FSplitString`
```jam
rule FSplitString string : delimiterChar
```
Splits strings on delimiter characters.

#### `FSplitPath`
```jam
rule FSplitPath path
```
Splits file paths into components.

#### `FDirName`
```jam
rule FDirName path_components
```
Constructs directory paths from components.

## Mathematical Functions

Haiku implements mathematical operations in Jam:

#### `AddNumAbs`
```jam
rule AddNumAbs a : b
```
Adds two positive numbers using lookup tables.

## Advanced Build Rules and Architecture

### Main Build Rules Analysis

#### Application Build System

The `Application` rule represents the core of Haiku's executable build system:

```jam
rule Application
{
    # Application <name> : <sources> : <libraries> : <res> ;
    if ! [ IsPlatformSupportedForTarget $(1) ] {
        return ;
    }
    
    AddResources $(1) : $(4) ;
    Main $(1) : $(2) ;
    LinkAgainst $(1) : $(3) ;
    LINKFLAGS on $(1) = [ on $(1) return $(LINKFLAGS) ]
        -Xlinker -soname=_APP_ ;
    
    AddSharedObjectGlueCode $(1) : true ;
}
```

**Key Features**:
- Platform compatibility checking via `IsPlatformSupportedForTarget`
- Automatic resource integration with `AddResources`
- Special app identifier via `-soname=_APP_` linker flag
- Shared object glue code for Haiku-specific linking

#### Shared Object Glue Code System

```jam
rule AddSharedObjectGlueCode
{
    # Links with -nostdlib and adds required libs manually for Haiku
    local platform ;
    on $(1) {
        platform = $(PLATFORM) ;
        if $(platform) = haiku {
            local stdLibs = [ MultiArchDefaultGristFiles libroot.so ]
                [ TargetLibgcc ] ;
            local type = EXECUTABLE ;
            if $(2) != true {
                type = LIBRARY ;
                # special case for libroot: don't link against itself
                if $(DONT_LINK_AGAINST_LIBROOT) {
                    stdLibs = ;
                }
            }
            
            LINKFLAGS on $(1) = [ on $(1) return $(LINKFLAGS) ] -nostdlib
                -Xlinker --no-undefined ;
        }
    }
}
```

**Critical Architecture Details**:
- Uses `-nostdlib` to avoid default library linking
- Manually controls library dependencies via `libroot.so` and `libgcc`
- Special handling for `libroot` to prevent circular dependencies
- Architecture-specific glue code via `HAIKU_*_BEGIN_GLUE_CODE_*` and `HAIKU_*_END_GLUE_CODE_*`

#### Add-on Build System

```jam
rule Addon target : sources : libraries : isExecutable
{
    Main $(target) : $(sources) ;
    
    local linkFlags = -Xlinker -soname=\"$(target:G=)\" ;
    if $(isExecutable) != true {
        linkFlags = -shared $(linkFlags) ;
    }
    LINKFLAGS on $(target) = [ on $(target) return $(LINKFLAGS) ] $(linkFlags) ;
    LinkAgainst $(target) : $(libraries) ;
    
    AddSharedObjectGlueCode $(target) : $(isExecutable) ;
}
```

**Add-on Specific Features**:
- Flexible executable/non-executable add-ons
- Custom soname based on target name
- Conditional shared library linking

### Kernel Build Architecture

#### Kernel Object Setup

```jam
rule SetupKernel
{
    local sources = [ FGristFiles $(1) ] ;
    local objects = $(sources:S=$(SUFOBJ)) ;
    
    # add private kernel headers
    if $(3) != false {
        SourceSysHdrs $(sources) : $(TARGET_PRIVATE_KERNEL_HEADERS) ;
    }
    
    local object ;
    for object in $(objects) {
        TARGET_PACKAGING_ARCH on $(object) = $(TARGET_KERNEL_ARCH) ;
        
        # add kernel flags for the object
        ObjectCcFlags $(object) : $(TARGET_KERNEL_CCFLAGS) $(2) ;
        ObjectC++Flags $(object) : $(TARGET_KERNEL_C++FLAGS) $(2) ;
        ObjectDefines $(object) : $(TARGET_KERNEL_DEFINES) ;
        
        # override regular CCFLAGS/C++FLAGS
        TARGET_CCFLAGS_$(TARGET_KERNEL_ARCH) on $(object) = ;
        TARGET_C++FLAGS_$(TARGET_KERNEL_ARCH) on $(object) = ;
    }
}
```

**Kernel-Specific Processing**:
- Per-object architecture assignment via `TARGET_PACKAGING_ARCH`
- Kernel-specific compiler flags via `TARGET_KERNEL_CCFLAGS`
- Override of regular user-space flags
- Private kernel header integration

#### Kernel Linking

```jam
rule KernelLd
{
    LINK on $(1) = $(TARGET_LD_$(HAIKU_KERNEL_ARCH)) ;
    
    LINKFLAGS on $(1) = $(4) ;
    if $(3) {
        LINKFLAGS on $(1) += --script=$(3) ;
        Depends $(1) : $(3) ;
    }
    
    # Remove preset LINKLIBS, link against libgcc.a
    local libs ;
    if ! [ on $(1) return $(HAIKU_NO_LIBSUPC++) ] {
        libs += [ TargetKernelLibsupc++ true ] ;
    }
    LINKLIBS on $(1) = $(libs) [ TargetKernelLibgcc true ] ;
}
```

**Kernel Linking Features**:
- Architecture-specific linker via `TARGET_LD_$(HAIKU_KERNEL_ARCH)`
- Custom linker script support
- Minimal library dependencies (only libgcc and optionally libsupc++)
- Explicit libsupc++ opt-out mechanism

### Architecture Configuration System

#### Compiler Flag Architecture

```jam
rule ArchitectureSetup architecture
{
    # Enable GCC -pipe option if requested
    local ccBaseFlags ;
    if $(HAIKU_USE_GCC_PIPE) = 1 {
        ccBaseFlags = -pipe ;
    }
    
    if $(HAIKU_CC_IS_LEGACY_GCC_$(architecture)) != 1 {
        # Disable strict aliasing on newer than gcc 2
        ccBaseFlags += -fno-strict-aliasing ;
        ccBaseFlags += -fno-delete-null-pointer-checks ;
        
        if $(architecture) = x86 {
            ccBaseFlags += -fno-builtin-fork -fno-builtin-vfork ;
        }
    }
    
    # Architecture-specific tuning
    local cpu = $(HAIKU_CPU_$(architecture)) ;
    local archFlags ;
    switch $(cpu) {
        case ppc : archFlags += -mcpu=440fp ;
        case arm : archFlags += -march=armv7-a -mfloat-abi=hard ;
        case aarch64 :
        case arm64 : archFlags += -march=armv8.6-a+crypto+lse2 ;
        case x86 : archFlags += -march=pentium ;
        case riscv64 : archFlags += -march=rv64gc ;
    }
}
```

**Architecture-Specific Optimizations**:
- **ARM64**: Uses ARMv8.6-A with crypto and LSE2 extensions
- **ARM**: ARMv7-A with hard float ABI
- **x86**: Pentium baseline compatibility
- **RISC-V**: RV64GC (64-bit with compressed instructions)
- **PowerPC**: 440FP core targeting

#### Warning Flag Management

```jam
# Warning flags per architecture
HAIKU_WARNING_CCFLAGS_$(architecture) = -Wall
    -Wno-multichar
    -Wpointer-arith -Wsign-compare
    -Wmissing-prototypes ;
    
HAIKU_WARNING_C++FLAGS_$(architecture) = -Wall
    -Wno-multichar
    -Wpointer-arith -Wsign-compare
    -Wno-ctor-dtor-privacy -Woverloaded-virtual ;

# Clang-specific warning suppression
if $(HAIKU_CC_IS_CLANG_$(architecture)) = 1 {
    HAIKU_WARNING_CCFLAGS_$(architecture) +=
        -Wno-unused-private-field -Wno-gnu-designator
        -Wno-builtin-requires-header ;
}
```

### File Operations and Utilities

#### Symbolic Link Management

```jam
rule RelSymLink
{
    # Creates relative symbolic link from <link> to <link target>
    local target = $(1) ;
    local source = $(2) ;
    local targetDir = [ on $(target) FDirName $(LOCATE[1]) $(target:D) ] ;
    local sourceDir = [ on $(source) FDirName $(LOCATE[1]) $(source:D) ] ;
    
    SymLink $(target)
        : [ FRelPath $(targetDirComponents) : $(sourceComponents) ] ;
}
```

#### Path Manipulation Utilities

```jam
rule FSplitPath
{
    # Decomposes a path into its components
    local path = $(1:G=) ;
    local components ;
    while $(path:D) && $(path:D) != $(path) {
        components = $(path:D=) $(components) ;
        path = $(path:D) ;
    }
    components = $(path) $(components) ;
    return $(components) ;
}

rule FSplitString string : delimiterChar
{
    local result ;
    while $(string) {
        local split = [ Match $(delimiterChar)*([^$(delimiterChar)]+)(.*)
            : $(string) ] ;
        result += $(split[1]) ;
        string = $(split[2-]) ;
    }
    return $(result) ;
}
```

### Object Directory Management

#### Multi-Level Directory Structure

```jam
rule SetupObjectsDir
{
    local relPath = [ FDirName $(SUBDIR_TOKENS[2-]) ] ;
    
    COMMON_PLATFORM_LOCATE_TARGET
        = [ FDirName $(HAIKU_COMMON_PLATFORM_OBJECT_DIR) $(relPath) ] ;
    HOST_COMMON_ARCH_LOCATE_TARGET
        = [ FDirName $(HOST_COMMON_ARCH_OBJECT_DIR) $(relPath) ] ;
    TARGET_COMMON_ARCH_LOCATE_TARGET
        = [ FDirName $(TARGET_COMMON_ARCH_OBJECT_DIR) $(relPath) ] ;
    
    local var ;
    for var in COMMON_DEBUG DEBUG_$(HAIKU_DEBUG_LEVELS) {
        HOST_$(var)_LOCATE_TARGET
            = [ FDirName $(HOST_$(var)_OBJECT_DIR) $(relPath) ] ;
        TARGET_$(var)_LOCATE_TARGET
            = [ FDirName $(TARGET_$(var)_OBJECT_DIR_$(TARGET_PACKAGING_ARCH))
                $(relPath) ] ;
    }
}
```

**Directory Organization Strategy**:
- **Platform separation**: Host vs Target builds
- **Architecture separation**: Per-architecture object directories
- **Debug level separation**: Release, debug_1, debug_2, etc.
- **Relative path preservation**: Maintains source tree structure

### Testing Framework Integration

#### Unit Test Infrastructure

```jam
rule UnitTest
{
    # UnitTest <target> : <sources> : <libraries> : <resources> ;
    local target = $(1) ;
    
    # Define TEST_HAIKU depending on platform
    if $(TARGET_PLATFORM) = libbe_test {
        ObjectDefines $(2) : TEST_HAIKU TEST_OBOS ;
        Depends $(target) : <tests!unittests>libbe_test.so ;
    } else {
        ObjectDefines $(2) : TEST_HAIKU TEST_OBOS ;
    }
    
    UseCppUnitObjectHeaders $(sources) ;
    MakeLocate $(target) : $(TARGET_UNIT_TEST_DIR) ;
    SimpleTest $(target) : $(sources) : $(libraries) libcppunit.so ;
    UnitTestDependency $(target) ;
}
```

**Testing Features**:
- Platform-specific test builds (`libbe_test` vs regular)
- Automatic CppUnit integration
- Test-specific preprocessor definitions
- Centralized test dependency management

### Advanced Package Management

#### Package Grist System

```jam
rule FHaikuPackageGrist package
{
    local grist = [ Match "<(.*)>" : $(package:G) ] ;
    return hpkg_$(grist:E="")-$(package:G=) ;
}

rule HaikuPackage package
{
    local grist = [ FHaikuPackageGrist $(package) ] ;
    
    HAIKU_CONTAINER_GRIST on $(package) = $(grist) ;
    HAIKU_INCLUDE_IN_CONTAINER_VAR on $(package) = HAIKU_INCLUDE_IN_PACKAGES ;
    HAIKU_INSTALL_TARGETS_VAR on $(package)
        = $(grist)_HAIKU_PACKAGE_INSTALL_TARGETS ;
    
    # Package rebuild control
    if $(HAIKU_DONT_REBUILD_PACKAGES) {
        local file = [ Glob $(HAIKU_PACKAGES_DIR_$(HAIKU_PACKAGING_ARCH))
            : $(package:BS) ] ;
        if $(file) {
            HAIKU_DONT_REBUILD_PACKAGE on $(package) = 1 ;
        }
    }
}
```

**Package System Features**:
- Unique package identification via grist system
- Container variable management for isolation
- Conditional package rebuild prevention
- Update-only mode support

### Build Feature System

#### Feature-Based Directory Management

```jam
rule SetupFeatureObjectsDir feature
{
    # Updates *{LOCATE,SEARCH}*_{TARGET,SOURCE} variables appending <feature>
    COMMON_PLATFORM_LOCATE_TARGET
        = [ FDirName $(COMMON_PLATFORM_LOCATE_TARGET) $(feature) ] ;
    
    local var ;
    for var in COMMON_ARCH COMMON_DEBUG DEBUG_$(HAIKU_DEBUG_LEVELS) {
        HOST_$(var)_LOCATE_TARGET
            = [ FDirName $(HOST_$(var)_LOCATE_TARGET) $(feature) ] ;
        TARGET_$(var)_LOCATE_TARGET
            = [ FDirName $(TARGET_$(var)_LOCATE_TARGET) $(feature) ] ;
    }
}
```

**Feature Integration**:
- Modular build feature support
- Per-feature object directory isolation
- Hierarchical feature organization

## Summary

The Haiku build system represents one of the most sophisticated Jam-based build environments ever created. It provides:

1. **Complete OS Build Support**: From bootloader to applications
2. **Multi-Architecture**: Simultaneous builds for different CPU architectures  
3. **Package Management**: Full .hpkg creation and management
4. **Localization**: Comprehensive i18n/l10n support
5. **Resource Management**: Integrated .rdef compilation and attribute handling
6. **Container System**: Flexible system for creating different image types
7. **Mathematical Operations**: Pure Jam implementation of arithmetic
8. **Advanced Linking Control**: Platform-specific glue code and library management
9. **Kernel Build Support**: Specialized kernel compilation and linking
10. **Testing Framework**: Integrated unit testing with CppUnit
11. **Feature System**: Modular build feature support
12. **Path Utilities**: Comprehensive path manipulation and directory management

### Architectural Innovations

1. **Grist-Based Isolation**: Uses Jam's grist mechanism for target isolation across architectures and configurations
2. **Conditional Platform Logic**: Extensive platform detection and conditional behavior
3. **Hierarchical Object Directories**: Multi-dimensional organization by platform, architecture, debug level, and features
4. **Shared Object Glue Code**: Sophisticated linking control for Haiku's unique runtime requirements
5. **Multi-Architecture Simultaneous Builds**: Support for building multiple CPU architectures in parallel
6. **Resource Integration**: Seamless .rdef compilation and integration into executables
7. **Package Container System**: Abstract container system supporting multiple output formats
8. **Localization Pipeline**: Complete toolchain from source string extraction to binary catalog generation

The system's design allows for highly modular and maintainable builds while supporting the complex requirements of operating system development, including cross-compilation, package management, and internationalization. The advanced rule architecture demonstrates sophisticated build system engineering with extensive use of Jam's metaprogramming capabilities.