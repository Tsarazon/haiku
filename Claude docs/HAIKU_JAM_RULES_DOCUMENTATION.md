# Haiku Jam Build System Rules Documentation

This document provides comprehensive documentation for all custom Jam rules used in the Haiku build system. The rules are organized by category and include detailed parameter descriptions and the compiler/linker commands they generate.

## Table of Contents

1. [Build System Overview](#build-system-overview)
2. [Core Application Rules](#core-application-rules)
3. [Static Library Rules](#static-library-rules)
4. [Shared Library Rules](#shared-library-rules)
5. [Kernel Build Rules](#kernel-build-rules)
6. [Boot Loader Rules](#boot-loader-rules)
7. [Package Build Rules](#package-build-rules)
8. [Image Creation Rules](#image-creation-rules)
9. [Test Build Rules](#test-build-rules)
10. [File Handling Rules](#file-handling-rules)
11. [Header Management Rules](#header-management-rules)
12. [Localization Rules](#localization-rules)
13. [Object and Compilation Rules](#object-and-compilation-rules)
14. [Resource and Asset Rules](#resource-and-asset-rules)
15. [Build Platform Rules](#build-platform-rules)
16. [Utility and Helper Rules](#utility-and-helper-rules)
17. [Miscellaneous Utility Rules](#miscellaneous-utility-rules)
18. [System Library Rules](#system-library-rules)
19. [BeOS/Haiku-Specific File Operations](#beoshaiku-specific-file-operations)
20. [Architecture-Specific Rules](#architecture-specific-rules)
21. [Test Build Rules](#test-build-rules)
22. [Math Utility Rules](#math-utility-rules)
23. [CD and Media Rules](#cd-and-media-rules)
24. [Additional Advanced Rules](#additional-advanced-rules)

## Build System Overview

The Haiku build system is based on Jam with extensive custom rules defined in `/build/jam/`. The main entry point is the root `Jamfile` which includes build features, packages, and source directories.

### Key Files:
- **Jamfile** (root): Main build configuration and package inclusion
- **build/jam/MainBuildRules**: Core application and library build rules
- **build/jam/KernelRules**: Kernel-specific build rules
- **build/jam/BootRules**: Boot loader build rules
- **build/jam/PackageRules**: Package creation rules
- **build/jam/ImageRules**: System image creation rules

## Core Application Rules

### AddSharedObjectGlueCode
**File**: MainBuildRules:1
```jam
AddSharedObjectGlueCode
```

**Purpose**: Adds necessary glue code for shared objects.

**Parameters**: None

**Generated Commands**: None (modifies compilation)

### Application
**File**: MainBuildRules:44
```jam
Application <name> : <sources> : <libraries> : <resources>
```

**Purpose**: Builds a standard Haiku application executable.

**Parameters**:
- `<name>`: Application executable name
- `<sources>`: List of source files (.cpp, .c, .S, etc.)
- `<libraries>`: List of libraries to link against (e.g., "be", "translation")
- `<resources>`: Resource files to embed (.rdef, .rsrc)

**Generated Commands**:
```bash
# Compilation (for each source file)
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) \
  $(TARGET_WARNING_CCFLAGS_$(TARGET_PACKAGING_ARCH)) $(TARGET_DEFINES_$(TARGET_PACKAGING_ARCH)) \
  -I$(headers) -c source.cpp -o object.o

# Linking
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) $(TARGET_LINKFLAGS_$(TARGET_PACKAGING_ARCH)) \
  -nostdlib -Xlinker --no-undefined -Xlinker -soname=_APP_ \
  -o executable $(LINK_BEGIN_GLUE) objects... $(NEEDLIBS) $(LINKLIBS) $(LINK_END_GLUE)
```

**Key Features**:
- Automatically adds Haiku glue code (`crti.o`, `crtn.o`)
- Links against `libroot.so` and `libgcc.a`
- Sets soname to `_APP_` for applications
- Supports resource embedding via `xres`

### StdBinCommands
**File**: MainBuildRules:63
```jam
StdBinCommands <sources> : <libraries> : <resources>
```

**Purpose**: Builds multiple simple command-line utilities from individual source files.

**Parameters**:
- `<sources>`: List of source files (each becomes a separate executable)
- `<libraries>`: Libraries to link all executables against
- `<resources>`: Resources to embed in all executables

**Usage Example**:
```jam
StdBinCommands cat.cpp ls.cpp mkdir.cpp : $(TARGET_LIBSUPC++) $(TARGET_LIBSTDC++) ;
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
# Linking (non-executable add-on)
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -shared -Xlinker -soname="target_name" \
  -nostdlib -o target objects... $(NEEDLIBS) $(LINKLIBS)

# Linking (executable add-on)
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -Xlinker -soname="target_name" \
  -nostdlib -o target objects... $(NEEDLIBS) $(LINKLIBS)
```

### Translator
**File**: MainBuildRules:101
```jam
Translator <target> : <sources> : <libraries> : <isExecutable>
```

**Purpose**: Alias for `Addon` rule, specifically for Translation Kit translators.

### ScreenSaver
**File**: MainBuildRules:107
```jam
ScreenSaver <target> : <sources> : <libraries>
```

**Purpose**: Builds screen saver add-ons (non-executable add-ons).

## Static Library Rules

### StaticLibrary
**File**: MainBuildRules:113
```jam
StaticLibrary <lib> : <sources> : <otherObjects>
```

**Purpose**: Creates static libraries from source files.

**Parameters**:
- `<lib>`: Static library name (e.g., "libmath.a")
- `<sources>`: Source files to compile
- `<otherObjects>`: Additional object files to include

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
- Automatically adds `-fvisibility=hidden` for modern GCC
- Skips visibility flags for legacy GCC
- Supports additional object files

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
# Uses host compiler and tools
$(HOST_CC) $(HOST_CCFLAGS) $(HOST_WARNING_CCFLAGS) -c source.cpp -o object.o
$(HOST_AR) $(ARFLAGS) library.a objects...
$(HOST_RANLIB) library.a
```

## Shared Library Rules

### SharedLibrary
**File**: MainBuildRules:434
```jam
SharedLibrary <lib> : <sources> : <libraries> : <abiVersion>
```

**Purpose**: Creates shared libraries (.so files).

**Parameters**:
- `<lib>`: Library name
- `<sources>`: Source files
- `<libraries>`: Dependencies to link against
- `<abiVersion>`: Major ABI version for soname (optional)

**Generated Commands**:
```bash
# Compilation
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) \
  -fPIC -I$(headers) -c source.cpp -o object.o

# Linking
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -shared -Xlinker -soname="libname.so.version" \
  -nostdlib -o library.so objects... $(NEEDLIBS) $(LINKLIBS)
```

**Key Features**:
- Automatically sets soname with ABI version if provided
- Links with Haiku glue code for shared libraries
- Supports library dependencies via `LinkAgainst`

### SharedLibraryFromObjects
**File**: MainBuildRules:413
```jam
SharedLibraryFromObjects <lib> : <objects> : <libraries>
```

**Purpose**: Creates shared library from pre-compiled objects.

### CreateAsmStructOffsetsHeader
**File**: MainBuildRules:215
```jam
CreateAsmStructOffsetsHeader <header> : <source> : <architecture>
```

**Purpose**: Creates a header file containing structure offsets for assembly code.

**Parameters**:
- `<header>`: Output header file with structure offsets
- `<source>`: C source file containing structure definitions
- `<architecture>`: Target architecture

**Generated Commands**:
```bash
$(CC) -S $(CCFLAGS) $(CCDEFS) $(HDRS) -o - <source> | $(AWK) ... | $(SED) ... > <header>
```

### MergeObjectFromObjects
**File**: MainBuildRules:352
```jam
MergeObjectFromObjects <target> : <objects> : <otherObjects>
```

**Purpose**: Merges multiple object files into a single object file using ld -r.

**Parameters**:
- `<target>`: Output merged object file
- `<objects>`: Primary object files to merge
- `<otherObjects>`: Additional objects to include

**Generated Commands**:
```bash
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -r -o <target> <objects> <otherObjects>
```

### SetVersionScript
**File**: MainBuildRules:569
```jam
SetVersionScript <target> : <versionScript>
```

**Purpose**: Sets a version script for controlling symbol visibility in shared libraries.

**Parameters**:
- `<target>`: Shared library target
- `<versionScript>`: Version script file

**Generated Commands**: None (adds linker flags)

### BuildPlatformMergeObjectPIC
**File**: MainBuildRules:681
```jam
BuildPlatformMergeObjectPIC <target> : <sources> : <otherObjects>
```

**Purpose**: Merges objects for host platform with position-independent code.

**Parameters**:
- `<target>`: Output merged object file
- `<sources>`: Source files to compile and merge
- `<otherObjects>`: Pre-compiled objects to include

**Generated Commands**:
```bash
$(HOST_LD) -r -o <target> <compiled_sources> <otherObjects>
```

### BuildPlatformStaticLibraryPIC
**File**: MainBuildRules:721
```jam
BuildPlatformStaticLibraryPIC <target> : <sources> : <otherObjects>
```

**Purpose**: Creates a position-independent static library for host platform.

**Parameters**:
- `<target>`: Output static library
- `<sources>`: Source files to compile into library
- `<otherObjects>`: Additional objects to include

**Generated Commands**:
```bash
$(HOST_AR) $(HOST_ARFLAGS) <target> <objects>
$(HOST_RANLIB) <target>
```

### BootstrapStage0PlatformObjects
**File**: MainBuildRules:731
```jam
BootstrapStage0PlatformObjects <sources> : <separateFromStandardSiblings>
```

**Purpose**: Compiles objects for bootstrap stage 0 with special settings.

**Parameters**:
- `<sources>`: Source files to compile for bootstrap
- `<separateFromStandardSiblings>`: Whether to separate from standard objects

**Generated Commands**:
```bash
$(HOST_CC) $(HOST_CCFLAGS) -c -o <object> <source>
```

## Kernel Build Rules

### SetupKernel
**File**: KernelRules:1
```jam
SetupKernel <sources_or_objects> : <extra_cc_flags> : <include_private_headers>
```

**Purpose**: Configures sources/objects for kernel compilation.

**Parameters**:
- `<sources_or_objects>`: Files to configure
- `<extra_cc_flags>`: Additional compiler flags
- `<include_private_headers>`: Include private kernel headers (default: true)

**Generated Configuration**:
```bash
# Adds kernel-specific flags
CCFLAGS = $(TARGET_KERNEL_CCFLAGS) $(extra_flags)
C++FLAGS = $(TARGET_KERNEL_C++FLAGS) $(extra_flags)
DEFINES = $(TARGET_KERNEL_DEFINES)
SYSHDRS = $(TARGET_PRIVATE_KERNEL_HEADERS)
```

### KernelObjects
**File**: KernelRules:38
```jam
KernelObjects <sources> : <extra_cc_flags>
```

**Purpose**: Compiles object files for kernel use.

**Generated Commands**:
```bash
$(TARGET_CC_$(TARGET_KERNEL_ARCH)) $(TARGET_KERNEL_CCFLAGS) $(extra_flags) \
  $(TARGET_KERNEL_WARNING_CCFLAGS) -I$(TARGET_PRIVATE_KERNEL_HEADERS) \
  -c source.cpp -o object.o
```

### KernelLd
**File**: KernelRules:44
```jam
KernelLd <name> : <objs> : <linkerscript> : <args>
```

**Purpose**: Links kernel executables or modules.

**Parameters**:
- `<name>`: Output executable name
- `<objs>`: Object files to link
- `<linkerscript>`: Linker script file (optional)
- `<args>`: Additional linker arguments

**Generated Commands**:
```bash
$(TARGET_LD_$(HAIKU_KERNEL_ARCH)) $(TARGET_LDFLAGS_$(HAIKU_KERNEL_ARCH)) \
  $(args) --script=$(linkerscript) -o executable objects... \
  $(HAIKU_KERNEL_LIBSUPC++_$(HAIKU_KERNEL_ARCH)) $(HAIKU_KERNEL_LIBGCC_$(HAIKU_KERNEL_ARCH)) \
  --version-script=$(VERSION_SCRIPT)
```

### KernelAddon
**File**: KernelRules:143
```jam
KernelAddon <target> : <sources> : <libraries> : <isExecutable>
```

**Purpose**: Builds kernel add-ons (drivers, modules).

### KernelStaticLibrary
**File**: KernelRules:178
```jam
KernelStaticLibrary <lib> : <sources> : <kernel_dependencies>
```

**Purpose**: Creates static libraries for kernel use.

### KernelMergeObject
**File**: KernelRules:159
```jam
KernelMergeObject <target> : <sources> : <extra_cc_flags>
```

**Purpose**: Merges multiple object files into a single relocatable object.

## Boot Loader Rules

### SetupBoot
**File**: BootRules:63
```jam
SetupBoot <sources_or_objects> : <extra_cc_flags> : <include_private_headers>
```

**Purpose**: Configures sources for boot loader compilation.

**Generated Configuration**:
```bash
# Boot-specific compilation flags
CCFLAGS = $(HAIKU_BOOT_CCFLAGS) $(HAIKU_BOOT_$(platform)_CCFLAGS) $(extra_flags)
C++FLAGS = $(HAIKU_BOOT_C++FLAGS) $(HAIKU_BOOT_$(platform)_C++FLAGS) $(extra_flags)
DEFINES = $(TARGET_KERNEL_DEFINES) $(TARGET_BOOT_DEFINES)
OPTIM = $(HAIKU_BOOT_OPTIM)
```

### BootObjects
**File**: BootRules:165
```jam
BootObjects <sources> : <extra_cc_flags>
```

**Purpose**: Compiles objects for boot loader use.

### BootLd
**File**: BootRules:117
```jam
BootLd <name> : <objs> : <linkerscript> : <args>
```

**Purpose**: Links boot loader executables.

### BootStaticLibrary
**File**: BootRules:184
```jam
BootStaticLibrary <lib> : <sources> : <extra_cc_flags>
```

**Purpose**: Creates static libraries for boot loader.

## Package Build Rules

### HaikuPackage
**File**: PackageRules:160
```jam
HaikuPackage <package> : <packageinfo>
```

**Purpose**: Creates a Haiku package (.hpkg file).

**Parameters**:
- `<package>`: Package name
- `<packageinfo>`: Package info file (.PackageInfo)

### BuildHaikuPackage
**File**: PackageRules:148
```jam
BuildHaikuPackage <package> : <packageInfo>
```

**Purpose**: Low-level package building rule.

**Generated Commands**:
```bash
$(HAIKU_PACKAGE_FS_SHELL_COMMAND) $(packageInfo) $(HAIKU_PACKAGE_DIR)/$(package).hpkg
```

## Image Creation Rules

### BuildHaikuImage
**File**: ImageRules:1294
```jam
BuildHaikuImage <haikuImage> : <scripts> : <isImage> : <isVMwareImage>
```

**Purpose**: Creates the main Haiku system image.

### AddFilesToHaikuImage
**File**: ImageRules:944
```jam
AddFilesToHaikuImage <directory> : <targets> : <destName> : <flags>
```

**Purpose**: Adds files to the Haiku image in specified directory.

### AddLibrariesToHaikuImage
**File**: ImageRules:1265
```jam
AddLibrariesToHaikuImage <directory> : <libs>
```

**Purpose**: Adds libraries to the image with proper installation.

## Architecture-Specific Features

### Multi-Architecture Support
The build system supports multiple architectures simultaneously:

- **Primary Architecture**: Main target (e.g., x86_64)
- **Secondary Architectures**: Additional supported architectures
- **Cross-Compilation**: Host tools vs target binaries

### Architecture Variables
```jam
TARGET_PACKAGING_ARCH          # Current target architecture
TARGET_CC_$(TARGET_PACKAGING_ARCH)      # Architecture-specific compiler
TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH) # Architecture-specific flags
```

### MultiArch Rules
```jam
MultiArchSubDirSetup $(architectures)  # Sets up multi-arch builds
MultiArchDefaultGristFiles <files>     # Applies architecture grist
```

## Build Flags and Configuration

### Compiler Flags Hierarchy
1. **Base Flags**: `TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)`
2. **Warning Flags**: `TARGET_WARNING_CCFLAGS_$(TARGET_PACKAGING_ARCH)`
3. **Debug Flags**: `TARGET_DEBUG_$(DEBUG)_CCFLAGS`
4. **Optimization**: `OPTIM`
5. **Defines**: `TARGET_DEFINES_$(TARGET_PACKAGING_ARCH)`

### Platform-Specific Flags
- **Kernel**: `TARGET_KERNEL_CCFLAGS`, `TARGET_KERNEL_C++FLAGS`
- **Boot**: `HAIKU_BOOT_CCFLAGS`, `HAIKU_BOOT_C++FLAGS`
- **Host Tools**: `HOST_CCFLAGS`, `HOST_C++FLAGS`

## Link Process Details

### Shared Object Glue Code
All Haiku executables and shared libraries use glue code:
- **Begin Glue**: `crti.o` - initialization code
- **End Glue**: `crtn.o` - finalization code
- **Standard Libraries**: `libroot.so`, `libgcc.a`

### Library Name Mapping
The build system automatically maps library names:
```jam
be        -> libbe.so
root      -> libroot.so
supc++    -> libsupc++.a
stdc++    -> libstdc++.so
```

### Soname Handling
- **Applications**: soname = `_APP_`
- **Add-ons**: soname = add-on filename
- **Libraries**: soname = `libname.so[.version]`

## Build Features and Optional Packages

### Build Features
Controlled via `build/jam/BuildFeatures`:
- `gcc_syslibs`: GCC system libraries
- `openssl`: OpenSSL support
- `regular_image`: Full system vs minimal

### Package Integration
- **System Packages**: Core required packages
- **Optional Packages**: User-selectable additions
- **Source Packages**: Source code for packages

## Error Handling and Validation

### Platform Support Checks
```jam
IsPlatformSupportedForTarget <target>
```
Returns true if target can be built for current platform.

### Build Feature Validation
The build system validates that all requested optional packages exist before proceeding.

### Architecture Consistency
Multi-architecture builds ensure consistent flags and settings across all architectures.

## File Handling Rules

### Copy
**File**: FileRules:9
```jam
Copy <target> : <source>
```

**Purpose**: Copies files preserving attributes and metadata.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
<build>copyattr -d "source" "target"
```

### SymLink
**File**: FileRules:26
```jam
SymLink <target> : <source> : <makeDefaultDependencies>
```

**Purpose**: Creates symbolic links.

**Parameters**:
- `<target>`: Link file to create
- `<source>`: Target of the link (exact link contents)
- `<makeDefaultDependencies>`: Add to default build targets (default: true)

**Generated Commands**:
```bash
$(RM) "target" && $(LN) -s "source" "target"
```

### RelSymLink
**File**: FileRules:54
```jam
RelSymLink <link> : <link_target> : <makeDefaultDependencies>
```

**Purpose**: Creates relative symbolic links between targets.

### AbsSymLink
**File**: FileRules:82
```jam
AbsSymLink <link> : <link_target> : <link_dir> : <makeDefaultDependencies>
```

**Purpose**: Creates absolute symbolic links.

### HaikuInstall
**File**: FileRules:119
```jam
HaikuInstall <installAndUninstall> : <dir> : <sources> : <installgrist>
```

**Purpose**: Installs files to specified directories with proper permissions.

**Parameters**:
- `<installAndUninstall>`: Installation mode ("install", "uninstall")
- `<dir>`: Target directory
- `<sources>`: Files to install
- `<installgrist>`: Grist for installed files

## Header Management Rules

### UsePrivateHeaders
**File**: HeadersRules:420
```jam
UsePrivateHeaders <group_list> [ : <system> ]
```

**Purpose**: Adds private header directories to compilation include paths.

**Parameters**:
- `<group_list>`: Header groups (e.g., "kernel", "shared", "interface")
- `<system>`: Whether to add as system headers (default: true)

**Example Usage**:
```jam
UsePrivateHeaders kernel shared ;  # Adds kernel and shared private headers
UsePrivateHeaders interface : false ;  # Adds as local headers
```

### UsePublicHeaders
**File**: HeadersRules:415
```jam
UsePublicHeaders <group_list>
```

**Purpose**: Adds public header directories to compilation include paths.

### SourceSysHdrs
**File**: HeadersRules:110
```jam
SourceSysHdrs <sources> : <headers> [ : <gristed_objects> ]
```

**Purpose**: Adds system header directories for specific source files.

### SourceHdrs
**File**: HeadersRules:89
```jam
SourceHdrs <sources> : <headers> [ : <gristed_objects> ]
```

**Purpose**: Adds header directories for specific source files.

### SubDirSysHdrs
**File**: HeadersRules:21
```jam
SubDirSysHdrs <dirs>
```

**Purpose**: Adds system header directories for the current subdirectory.

### ObjectSysHdrs
**File**: HeadersRules:36
```jam
ObjectSysHdrs <sources_or_objects> : <headers> : <gristed_objects>
```

**Purpose**: Adds system header directories for specific objects.

## Localization Rules

### ExtractCatalogEntries
**File**: LocaleRules:6
```jam
ExtractCatalogEntries <target> : <sources> : <signature> : <regexp>
```

**Purpose**: Extracts translatable strings from source code for localization.

**Parameters**:
- `<target>`: Output catalog entries file
- `<sources>`: Source files to scan
- `<signature>`: Application signature for catalog
- `<regexp>`: Regular expression for string extraction (optional)

**Generated Commands**:
```bash
$(CC) -E $(CCDEFS) -DB_COLLECTING_CATKEYS $(HDRS) "sources" > "target.pre"
<build>collectcatkeys $(REGEXP) -s signature -w -o "target" "target.pre"
```

### LinkApplicationCatalog
**File**: LocaleRules:83
```jam
LinkApplicationCatalog <target> : <sources> : <signature> : <language>
```

**Purpose**: Links catalog entries into compiled catalog files.

**Parameters**:
- `<target>`: Output compiled catalog
- `<sources>`: Input catkey files
- `<signature>`: Application signature
- `<language>`: Target language code

### DoCatalogs
**File**: LocaleRules:183
```jam
DoCatalogs <target> : <signature> : <sources> : <catkeys> : <fingerprint>
```

**Purpose**: Complete localization workflow for an application.

## Object and Compilation Rules

### Objects
**File**: MainBuildRules (enhanced)
```jam
Objects <sources>
```

**Purpose**: Compiles source files to object files with proper flags.

### MergeObject
**File**: MainBuildRules:392
```jam
MergeObject <target> : <sources> : <otherObjects>
```

**Purpose**: Merges multiple objects into a single relocatable object.

**Generated Commands**:
```bash
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -r -o merged.o objects...
```

### MergeObjectFromObjects
**File**: MainBuildRules:378
```jam
MergeObjectFromObjects <target> : <objects> : <otherObjects>
```

**Purpose**: Merges pre-compiled objects.

### AssembleNasm
**File**: MainBuildRules:151
```jam
AssembleNasm <object> : <source>
```

**Purpose**: Assembles NASM assembly files.

**Generated Commands**:
```bash
$(HAIKU_NASM) $(NASMFLAGS) -I$(source:D)/ -o object source
```

### Ld
**File**: MainBuildRules:169
```jam
Ld <name> : <objs> : <linkerscript> : <flags>
```

**Purpose**: Low-level linker rule for custom linking scenarios.

## Resource and Asset Rules

### AddResources
**File**: MainBuildRules (referenced)
```jam
AddResources <target> : <resources>
```

**Purpose**: Embeds resource files into executables.

**Generated Commands**:
```bash
<build>xres -o target resources...
```

### ResComp
**File**: BeOSRules
```jam
ResComp <resource> : <rdef>
```

**Purpose**: Compiles resource definition files (.rdef) to binary resources (.rsrc).

**Generated Commands**:
```bash
<build>rc -o resource.rsrc rdef_file.rdef
```

### SetType
**File**: BeOSRules
```jam
SetType <target>
```

**Purpose**: Sets file type and signature for Haiku binaries.

### MimeSet
**File**: BeOSRules
```jam
MimeSet <target>
```

**Purpose**: Sets MIME type for files.

### SetVersion
**File**: BeOSRules
```jam
SetVersion <target>
```

**Purpose**: Sets version information for binaries.

## Build Platform Rules

### BuildPlatformMain
**File**: MainBuildRules:598
```jam
BuildPlatformMain <target> : <sources> : <libraries>
```

**Purpose**: Builds executables for the host platform (build tools).

**Generated Commands**:
```bash
$(HOST_CC) $(HOST_CCFLAGS) $(HOST_WARNING_CCFLAGS) -c source.cpp -o object.o
$(HOST_LD) $(HOST_LINKFLAGS) -o executable objects... $(HOST_LIBBE) $(HOST_LIBROOT)
```

**Key Features**:
- Uses host compiler and linker
- Links against host versions of Haiku libraries
- Supports `USES_BE_API` flag for compatibility

### BuildPlatformMergeObject
**File**: MainBuildRules:681
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

### BuildPlatformStaticLibraryPIC
**File**: MainBuildRules:721
```jam
BuildPlatformStaticLibraryPIC <target> : <sources> : <otherObjects>
```

**Purpose**: Creates position-independent static libraries for host platform.

### BootstrapStage0PlatformObjects
**File**: MainBuildRules:731
```jam
BootstrapStage0PlatformObjects <sources> : <separateFromStandardSiblings>
```

**Purpose**: Compiles objects for bootstrap stage 0.

## Test Build Rules

### UnitTest
**File**: TestsRules:UnitTest
```jam
UnitTest <test> : <sources> : <libraries>
```

**Purpose**: Builds unit test executables.

### UnitTestLib
**File**: TestsRules:UnitTestLib
```jam
UnitTestLib <lib> : <sources>
```

**Purpose**: Creates libraries specifically for unit testing.

### SimpleTest
**File**: TestsRules:SimpleTest
```jam
SimpleTest <test> : <source> : <libraries>
```

**Purpose**: Builds simple single-source tests.

### TestObjects
**File**: TestsRules:TestObjects
```jam
TestObjects <sources>
```

**Purpose**: Compiles objects for testing with test-specific flags.

### BuildPlatformTest
**File**: TestsRules:BuildPlatformTest
```jam
BuildPlatformTest <test> : <sources> : <libraries>
```

**Purpose**: Builds tests that run on the host platform.

## Additional Package Rules

### AddFilesToPackage
**File**: PackageRules:AddFilesToPackage
```jam
AddFilesToPackage <directory> : <targets> : <destName>
```

**Purpose**: Adds files to a package being built.

### AddDirectoryToPackage
**File**: PackageRules:AddDirectoryToPackage
```jam
AddDirectoryToPackage <directoryTokens> : <attributeFiles>
```

**Purpose**: Adds entire directories to packages.

### AddSymlinkToPackage
**File**: PackageRules:AddSymlinkToPackage
```jam
AddSymlinkToPackage <directoryTokens> : <linkTarget> : <linkName>
```

**Purpose**: Adds symbolic links to packages.

### AddLibrariesToPackage
**File**: PackageRules:AddLibrariesToPackage
```jam
AddLibrariesToPackage <directory> : <libs>
```

**Purpose**: Adds libraries to packages with proper dependencies.

### AddDriversToPackage
**File**: PackageRules:AddDriversToPackage
```jam
AddDriversToPackage <relativeDirectoryTokens> : <targets>
```

**Purpose**: Adds kernel drivers to packages.

## Advanced Image Rules

### AddBootModuleSymlinksToContainer
**File**: ImageRules:531
```jam
AddBootModuleSymlinksToContainer <container> : <targets>
```

**Purpose**: Adds symlinks for boot modules in containers.

### AddDirectoryToHaikuImage
**File**: ImageRules:936
```jam
AddDirectoryToHaikuImage <directoryTokens> : <attributeFiles>
```

**Purpose**: Creates directories in the Haiku system image.

### AddSymlinkToHaikuImage
**File**: ImageRules:959
```jam
AddSymlinkToHaikuImage <directoryTokens> : <linkTarget> : <linkName>
```

**Purpose**: Adds symbolic links to the system image.

### AddWifiFirmwareToHaikuImage
**File**: ImageRules:1001
```jam
AddWifiFirmwareToHaikuImage <driver> : <package> : <archive> : <extract>
```

**Purpose**: Extracts and installs WiFi firmware files.

### AddUserToHaikuImage
**File**: ImageRules:1240
```jam
AddUserToHaikuImage <user> : <uid> : <gid> : <home> : <shell> : <realName>
```

**Purpose**: Adds user accounts to the system image.

### AddGroupToHaikuImage
**File**: ImageRules:1252
```jam
AddGroupToHaikuImage <group> : <gid> : <members>
```

**Purpose**: Adds user groups to the system image.

## Build Features and Configuration

### EnableBuildFeatures
**File**: BuildFeatureRules:146
```jam
EnableBuildFeatures <features> : <specification>
```

**Purpose**: Enables optional build features.

### SetBuildFeatureAttribute
**File**: BuildFeatureRules:193
```jam
SetBuildFeatureAttribute <feature> : <attribute> : <values> : <package>
```

**Purpose**: Sets attributes for build features.

### FIsBuildFeatureEnabled
**File**: BuildFeatureRules:function
```jam
FIsBuildFeatureEnabled <feature>
```

**Purpose**: Returns true if a build feature is enabled.

### UseBuildFeatureHeaders
**File**: HeadersRules:420
```jam
UseBuildFeatureHeaders <feature> : <attribute>
```

**Purpose**: Adds headers from enabled build features.

## Utility and Helper Rules

### FFilter
**File**: HelperRules:30
```jam
FFilter <list> : <excludes>
```

**Purpose**: Removes all occurrences of specified items from a list.

**Parameters**:
- `<list>`: The input list to filter
- `<excludes>`: List of items to remove from the input list

**Generated Commands**: None (utility function)

**Example**:
```jam
local filtered = [ FFilter $(allFiles) : unwanted.cpp debug.cpp ] ;
```

### FGetGrist
**File**: HelperRules:54
```jam
FGetGrist <target>
```

**Purpose**: Returns the grist of a target, not including leading "<" and trailing ">".

**Parameters**:
- `<target>`: Target whose grist to extract

**Generated Commands**: None (utility function)

### FSplitString
**File**: HelperRules:68
```jam
FSplitString <string> : <delimiterChar>
```

**Purpose**: Splits a string on a delimiter character.

**Parameters**:
- `<string>`: String to split
- `<delimiterChar>`: Character to split on

**Generated Commands**: None (utility function)

### FSplitPath
**File**: HelperRules:82
```jam
FSplitPath <path>
```

**Purpose**: Decomposes a path into its components.

**Parameters**:
- `<path>`: Path to decompose

**Generated Commands**: None (utility function)

### FConditionsHold
**File**: HelperRules:100
```jam
FConditionsHold <conditions> : <predicate>
```

**Purpose**: Checks whether conditions are satisfied by a predicate rule. Supports positive and negative conditions (prefixed with "!").

**Parameters**:
- `<conditions>`: List of conditions to check
- `<predicate>`: Predicate rule to test elements

**Generated Commands**: None (utility function)

### SetPlatformForTarget
**File**: HelperRules:230
```jam
SetPlatformForTarget <target> : <platform>
```

**Purpose**: Sets the platform for a specific target.

**Parameters**:
- `<target>`: Target to set platform for
- `<platform>`: Platform name (e.g., "host", "haiku")

**Generated Commands**: None (sets variables)

### SetSubDirPlatform
**File**: HelperRules:237
```jam
SetSubDirPlatform <platform>
```

**Purpose**: Sets the platform for the current subdirectory.

**Parameters**:
- `<platform>`: Platform name

**Generated Commands**: None (sets variables)

### SetSupportedPlatformsForTarget
**File**: HelperRules:244
```jam
SetSupportedPlatformsForTarget <target> : <platforms>
```

**Purpose**: Sets the list of supported platforms for a target.

**Parameters**:
- `<target>`: Target to configure
- `<platforms>`: List of supported platform names

**Generated Commands**: None (sets variables)

### IsPlatformSupportedForTarget
**File**: HelperRules:265
```jam
IsPlatformSupportedForTarget <target> [ : <platform> ]
```

**Purpose**: Checks if the current platform is supported for a target.

**Parameters**:
- `<target>`: Target to check
- `<platform>`: Optional platform to check (defaults to current)

**Generated Commands**: None (returns boolean)

### InheritPlatform
**File**: HelperRules:279
```jam
InheritPlatform <children> : <parent>
```

**Purpose**: Makes children targets inherit platform settings from parent.

**Parameters**:
- `<children>`: Child targets
- `<parent>`: Parent target to inherit from

**Generated Commands**: None (sets variables)

### SubDirAsFlags
**File**: HelperRules:294
```jam
SubDirAsFlags <flags>
```

**Purpose**: Adds assembler flags for the current subdirectory.

**Parameters**:
- `<flags>`: Assembler flags to add

**Generated Commands**: None (sets variables)

### SetPlatformCompatibilityFlagVariables
**File**: HelperRules:157
```jam
SetPlatformCompatibilityFlagVariables <platform var> : <var prefix> : <platform kind> [ : <other platforms> ]
```

**Purpose**: Sets platform compatibility flag variables for different platforms.

**Parameters**:
- `<platform var>`: Variable containing platform name
- `<var prefix>`: Prefix for generated variables
- `<platform kind>`: Type of platform (e.g., "target", "host")
- `<other platforms>`: Optional list of other supported platforms

**Generated Commands**: None (sets variables)

### SetIncludePropertiesVariables
**File**: HelperRules:211
```jam
SetIncludePropertiesVariables <prefix> : <suffix>
```

**Purpose**: Sets include path variables based on compiler type (legacy GCC vs modern).

**Parameters**:
- `<prefix>`: Variable prefix
- `<suffix>`: Variable suffix

**Generated Commands**: None (sets variables)

### SetSubDirSupportedPlatforms
**File**: HelperRules:251
```jam
SetSubDirSupportedPlatforms <platforms>
```

**Purpose**: Sets the list of supported platforms for the current subdirectory.

**Parameters**:
- `<platforms>`: List of platform names

**Generated Commands**: None (sets variables)

### AddSubDirSupportedPlatforms
**File**: HelperRules:258
```jam
AddSubDirSupportedPlatforms <platforms>
```

**Purpose**: Adds platforms to the list of supported platforms for the current subdirectory.

**Parameters**:
- `<platforms>`: Additional platform names to add

**Generated Commands**: None (sets variables)

## Miscellaneous Utility Rules

### SetupObjectsDir
**File**: MiscRules:1
```jam
SetupObjectsDir
```

**Purpose**: Sets up the objects directory structure for the build.

**Parameters**: None

**Generated Commands**: None (directory setup)

### SetupFeatureObjectsDir
**File**: MiscRules:37
```jam
SetupFeatureObjectsDir <feature>
```

**Purpose**: Sets up objects directory for a specific build feature.

**Parameters**:
- `<feature>`: Build feature name

**Generated Commands**: None (directory setup)

### MakeLocateCommonPlatform
**File**: MiscRules:70
```jam
MakeLocateCommonPlatform <files> [ : <subdir> ]
```

**Purpose**: Locates files in the common platform-specific directory.

**Parameters**:
- `<files>`: Files to locate
- `<subdir>`: Optional subdirectory

**Generated Commands**: None (sets LOCATE variable)

### MakeLocatePlatform
**File**: MiscRules:77
```jam
MakeLocatePlatform <files> [ : <subdir> ]
```

**Purpose**: Locates files in the platform-specific directory.

**Parameters**:
- `<files>`: Files to locate
- `<subdir>`: Optional subdirectory

**Generated Commands**: None (sets LOCATE variable)

### MakeLocateArch
**File**: MiscRules:94
```jam
MakeLocateArch <files> [ : <subdir> ]
```

**Purpose**: Locates files in the architecture-specific directory.

**Parameters**:
- `<files>`: Files to locate
- `<subdir>`: Optional subdirectory

**Generated Commands**: None (sets LOCATE variable)

### MakeLocateDebug
**File**: MiscRules:111
```jam
MakeLocateDebug <files> [ : <subdir> ]
```

**Purpose**: Locates files in the debug output directory.

**Parameters**:
- `<files>`: Files to locate
- `<subdir>`: Optional subdirectory

**Generated Commands**: None (sets LOCATE variable)

### DeferredSubInclude
**File**: MiscRules:136
```jam
DeferredSubInclude <params> : <jamfile> : <scope>
```

**Purpose**: Defers the inclusion of a Jamfile until later execution.

**Parameters**:
- `<params>`: Parameters for SubInclude
- `<jamfile>`: Jamfile to include
- `<scope>`: Scope for inclusion

**Generated Commands**: None (deferred execution)

### ExecuteDeferredSubIncludes
**File**: MiscRules:154
```jam
ExecuteDeferredSubIncludes
```

**Purpose**: Executes all deferred SubInclude calls.

**Parameters**: None

**Generated Commands**: None (executes deferred calls)

### HaikuSubInclude
**File**: MiscRules:179
```jam
HaikuSubInclude <tokens>
```

**Purpose**: Includes a Jamfile relative to the Haiku source tree.

**Parameters**:
- `<tokens>`: Path tokens relative to HAIKU_TOP

**Generated Commands**: None (file inclusion)

### NextID
**File**: MiscRules:199
```jam
NextID
```

**Purpose**: Returns a unique ID for use in target names.

**Parameters**: None

**Generated Commands**: None (returns unique ID)

### NewUniqueTarget
**File**: MiscRules:208
```jam
NewUniqueTarget <basename>
```

**Purpose**: Creates a unique target name based on basename.

**Parameters**:
- `<basename>`: Base name for the target

**Generated Commands**: None (returns unique target)

### RunCommandLine
**File**: MiscRules:220
```jam
RunCommandLine <commandLine>
```

**Purpose**: Executes a command line during the build process.

**Parameters**:
- `<commandLine>`: Command to execute

**Generated Commands**: Executes the specified command

### DefineBuildProfile
**File**: MiscRules:277
```jam
DefineBuildProfile <name> : <type> : <path>
```

**Purpose**: Defines a build profile with specific settings.

**Parameters**:
- `<name>`: Profile name
- `<type>`: Profile type
- `<path>`: Path associated with profile

**Generated Commands**: None (defines profile)

## System Library Rules

### Libstdc++ForImage
**File**: SystemLibraryRules:1
```jam
Libstdc++ForImage
```

**Purpose**: Returns the appropriate libstdc++ for image inclusion.

**Parameters**: None

**Generated Commands**: None (returns library path)

### TargetLibstdc++
**File**: SystemLibraryRules:18
```jam
TargetLibstdc++ <asPath>
```

**Purpose**: Returns the target libstdc++ library.

**Parameters**:
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### TargetLibsupc++
**File**: SystemLibraryRules:43
```jam
TargetLibsupc++ <asPath>
```

**Purpose**: Returns the target libsupc++ library.

**Parameters**:
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### TargetStaticLibsupc++
**File**: SystemLibraryRules:70
```jam
TargetStaticLibsupc++ <asPath>
```

**Purpose**: Returns the target static libsupc++ library.

**Parameters**:
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### TargetKernelLibsupc++
**File**: SystemLibraryRules:92
```jam
TargetKernelLibsupc++ <asPath>
```

**Purpose**: Returns the kernel-specific libsupc++ library.

**Parameters**:
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### TargetBootLibsupc++
**File**: SystemLibraryRules:115
```jam
TargetBootLibsupc++ <asPath>
```

**Purpose**: Returns the boot loader libsupc++ library.

**Parameters**:
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### TargetLibgcc
**File**: SystemLibraryRules:157
```jam
TargetLibgcc <asPath>
```

**Purpose**: Returns the target libgcc library.

**Parameters**:
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### TargetStaticLibgcc
**File**: SystemLibraryRules:192
```jam
TargetStaticLibgcc <asPath>
```

**Purpose**: Returns the target static libgcc library.

**Parameters**:
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### TargetKernelLibgcc
**File**: SystemLibraryRules:214
```jam
TargetKernelLibgcc <asPath>
```

**Purpose**: Returns the kernel-specific libgcc library.

**Parameters**:
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### TargetBootLibgcc
**File**: SystemLibraryRules:237
```jam
TargetBootLibgcc <architecture> : <asPath>
```

**Purpose**: Returns the boot loader libgcc library for a specific architecture.

**Parameters**:
- `<architecture>`: Target architecture
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### TargetStaticLibgcceh
**File**: SystemLibraryRules:279
```jam
TargetStaticLibgcceh <asPath>
```

**Purpose**: Returns the target static libgcc_eh library.

**Parameters**:
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### TargetKernelLibgcceh
**File**: SystemLibraryRules:301
```jam
TargetKernelLibgcceh <asPath>
```

**Purpose**: Returns the kernel-specific libgcc_eh library.

**Parameters**:
- `<asPath>`: Whether to return as path (true/false)

**Generated Commands**: None (returns library)

### C++HeaderDirectories
**File**: SystemLibraryRules:324
```jam
C++HeaderDirectories <architecture>
```

**Purpose**: Returns C++ header directories for the given architecture.

**Parameters**:
- `<architecture>`: Target architecture

**Generated Commands**: None (returns header paths)

### GccHeaderDirectories
**File**: SystemLibraryRules:352
```jam
GccHeaderDirectories <architecture>
```

**Purpose**: Returns GCC header directories for the given architecture.

**Parameters**:
- `<architecture>`: Target architecture

**Generated Commands**: None (returns header paths)

## BeOS/Haiku-Specific File Operations

### AddFileDataAttribute
**File**: BeOSRules:3
```jam
AddFileDataAttribute <target> : <attrName> : <attrType> : <dataFile>
```

**Purpose**: Adds a file attribute with data read from a file.

**Parameters**:
- `<target>`: File to add attribute to
- `<attrName>`: Name of the attribute
- `<attrType>`: Attribute type (string, int, etc.)
- `<dataFile>`: File containing attribute data

**Generated Commands**:
```bash
<build>addattr -f <dataFile> -t <attrType> <attrName> <target>
```

### AddStringDataResource
**File**: BeOSRules:46
```jam
AddStringDataResource <target> : <resourceID> : <dataString>
```

**Purpose**: Adds a string resource to an executable/library.

**Parameters**:
- `<target>`: Executable/library target
- `<resourceID>`: Resource ID (type:id[:name])
- `<dataString>`: String data for the resource

**Generated Commands**:
```bash
<build>xres -o <resources> -a <resourceID> -s "<dataString>"
```

### AddFileDataResource
**File**: BeOSRules:82
```jam
AddFileDataResource <target> : <resourceID> : <dataFile>
```

**Purpose**: Adds a resource with data read from a file.

**Parameters**:
- `<target>`: Executable/library target
- `<resourceID>`: Resource ID (type:id[:name])
- `<dataFile>`: File containing resource data

**Generated Commands**:
```bash
<build>xres -o <resources> -a <resourceID> <dataFile>
```

### XRes
**File**: BeOSRules:122
```jam
XRes <target> : <resource files>
```

**Purpose**: Embeds resource files into a target using xres.

**Parameters**:
- `<target>`: Target to embed resources into
- `<resource files>`: List of resource files

**Generated Commands**:
```bash
<build>xres -o <target> <resource files>
```

### SetVersion
**File**: BeOSRules:138
```jam
SetVersion <target>
```

**Purpose**: Sets version information on a target.

**Parameters**:
- `<target>`: Target to set version on

**Generated Commands**:
```bash
<build>setversion <target> -system <VERSION> -short "<DESCRIPTION>"
```

### SetType
**File**: BeOSRules:152
```jam
SetType <target> [ : <type> ]
```

**Purpose**: Sets the MIME type on a target.

**Parameters**:
- `<target>`: Target to set MIME type on
- `<type>`: MIME type (defaults to executable type)

**Generated Commands**:
```bash
<build>settype -t <type> <target>
```

### MimeSet
**File**: BeOSRules:170
```jam
MimeSet <target>
```

**Purpose**: Updates MIME database information for a target.

**Parameters**:
- `<target>`: Target to update MIME info for

**Generated Commands**:
```bash
<build>mimeset -f --mimedb <mime_db> <target>
```

### CreateAppMimeDBEntries
**File**: BeOSRules:186
```jam
CreateAppMimeDBEntries <target>
```

**Purpose**: Creates MIME database entries for an application.

**Parameters**:
- `<target>`: Application target

**Generated Commands**:
```bash
<build>mimeset -f --apps --mimedb <output> --mimedb <input> <target>
```

### ResComp
**File**: BeOSRules:216
```jam
ResComp <resource file> : <rdef file>
```

**Purpose**: Compiles an .rdef file into a .rsrc resource file.

**Parameters**:
- `<resource file>`: Output .rsrc file (must be gristed)
- `<rdef file>`: Input .rdef file (must be gristed)

**Generated Commands**:
```bash
cat <rdef file> | <CC> <CCFLAGS> -E <defines> <headers> - | grep -E -v '^#' | <build>rc <headers> --auto-names -o <resource file> -
```

### ResAttr
**File**: BeOSRules:272
```jam
ResAttr <attribute file> : <resource files> [ : <delete file> ]
```

**Purpose**: Creates a file attribute from resource files.

**Parameters**:
- `<attribute file>`: Output attribute file (must be gristed)
- `<resource files>`: Input resource files (can be .rdef or .rsrc)
- `<delete file>`: Whether to delete target first (default: true)

**Generated Commands**:
```bash
<build>resattr -O -o <attribute file> <resource files>
```

## Architecture-Specific Rules

### KernelArchitectureSetup
**File**: ArchitectureRules:212
```jam
KernelArchitectureSetup <architecture>
```

**Purpose**: Sets up kernel-specific build variables for an architecture.

**Parameters**:
- `<architecture>`: Target architecture (e.g., x86_64, arm64)

**Generated Commands**: None (sets kernel-specific variables)

### ArchitectureSetupWarnings
**File**: ArchitectureRules:621
```jam
ArchitectureSetupWarnings <architecture>
```

**Purpose**: Sets up compiler warning flags for a specific architecture.

**Parameters**:
- `<architecture>`: Target architecture

**Generated Commands**: None (sets warning flags)

### MultiArchIfPrimary
**File**: ArchitectureRules:854
```jam
MultiArchIfPrimary <ifValue> : <elseValue> : <architecture>
```

**Purpose**: Returns ifValue if architecture is primary, else elseValue.

**Parameters**:
- `<ifValue>`: Value to return if primary architecture
- `<elseValue>`: Value to return if not primary
- `<architecture>`: Architecture to check

**Generated Commands**: None (returns value based on condition)

### MultiArchConditionalGristFiles
**File**: ArchitectureRules:871
```jam
MultiArchConditionalGristFiles <files> : <primaryGrist> : <secondaryGrist>
```

**Purpose**: Applies different grist to files based on architecture.

**Parameters**:
- `<files>`: Files to apply grist to
- `<primaryGrist>`: Grist for primary architecture
- `<secondaryGrist>`: Grist for secondary architectures

**Generated Commands**: None (returns gristed files)

### MultiArchDefaultGristFiles
**File**: ArchitectureRules:889
```jam
MultiArchDefaultGristFiles <files> : <gristPrefix> : <architecture>
```

**Purpose**: Applies default architecture-specific grist to files.

**Parameters**:
- `<files>`: Files to apply grist to
- `<gristPrefix>`: Prefix for grist
- `<architecture>`: Target architecture

**Generated Commands**: None (returns gristed files)

### MultiArchSubDirSetup
**File**: ArchitectureRules:913
```jam
MultiArchSubDirSetup <architectures>
```

**Purpose**: Sets up subdirectory for multiple architectures.

**Parameters**:
- `<architectures>`: List of architectures to set up

**Generated Commands**: None (returns architecture objects)

### ArchitectureSetup
**File**: ArchitectureRules:1
```jam
ArchitectureSetup <architecture>
```

**Purpose**: Initializes all global packaging architecture dependent variables.

**Key Features**:

## Additional Advanced Rules

### ProcessCommandLineArguments
**File**: CommandLineArguments:3
```jam
ProcessCommandLineArguments
```

**Purpose**: Processes command line arguments passed to jam.

**Parameters**: None

**Generated Commands**: None (processes arguments)

### ConfigObject
**File**: ConfigRules:10
```jam
ConfigObject
```

**Purpose**: Manages configuration object creation.

**Parameters**: None

**Generated Commands**: None (creates config object)

### SetConfigVar
**File**: ConfigRules:25
```jam
SetConfigVar <configObject> : <variable> : <value> : <scope>
```

**Purpose**: Sets a configuration variable.

**Parameters**:
- `<configObject>`: Configuration object
- `<variable>`: Variable name
- `<value>`: Variable value
- `<scope>`: Variable scope

**Generated Commands**: None (sets config variable)

### AppendToConfigVar
**File**: ConfigRules:64
```jam
AppendToConfigVar <configObject> : <variable> : <value> : <scope>
```

**Purpose**: Appends to a configuration variable.

**Parameters**:
- `<configObject>`: Configuration object
- `<variable>`: Variable name
- `<value>`: Value to append
- `<scope>`: Variable scope

**Generated Commands**: None (appends to config variable)

### ConfigVar
**File**: ConfigRules:86
```jam
ConfigVar <configObject> : <variable> : <scope>
```

**Purpose**: Retrieves a configuration variable.

**Parameters**:
- `<configObject>`: Configuration object
- `<variable>`: Variable name
- `<scope>`: Variable scope

**Generated Commands**: None (returns config variable)

### PrepareSubDirConfigVariables
**File**: ConfigRules:118
```jam
PrepareSubDirConfigVariables <configObject>
```

**Purpose**: Prepares configuration variables for subdirectory use.

**Parameters**:
- `<configObject>`: Configuration object

**Generated Commands**: None (prepares variables)

### PrepareConfigVariables
**File**: ConfigRules:148
```jam
PrepareConfigVariables <configObject>
```

**Purpose**: Prepares all configuration variables.

**Parameters**:
- `<configObject>`: Configuration object

**Generated Commands**: None (prepares variables)

### MultiBootSubDirSetup
**File**: BootRules:1
```jam
MultiBootSubDirSetup <bootTargets>
```

**Purpose**: Sets up subdirectory for multiple boot targets.

**Parameters**:
- `<bootTargets>`: List of boot targets

**Generated Commands**: None (sets up multi-boot)

### MultiBootGristFiles
**File**: BootRules:58
```jam
MultiBootGristFiles <files>
```

**Purpose**: Applies grist to files for multi-boot scenarios.

**Parameters**:
- `<files>`: Files to apply grist to

**Generated Commands**: None (returns gristed files)

### SetupBoot
**File**: BootRules:63
```jam
SetupBoot
```

**Purpose**: Sets up boot-specific build variables.

**Parameters**: None

**Generated Commands**: None (sets up boot environment)

### BuildMBR
**File**: BootRules:227
```jam
BuildMBR <binary> : <source>
```

**Purpose**: Builds a Master Boot Record binary.

**Parameters**:
- `<binary>`: Output MBR binary
- `<source>`: Source file for MBR

**Generated Commands**:
```bash
<assembler> -o <binary> <source>
```

### PackageFamily
**File**: RepositoryRules:7
```jam
PackageFamily <packageBaseName>
```

**Purpose**: Handles package family relationships.

**Parameters**:
- `<packageBaseName>`: Base name of package family

**Generated Commands**: None (manages package families)

### SetRepositoryMethod
**File**: RepositoryRules:13
```jam
SetRepositoryMethod <repository> : <methodName> : <method>
```

**Purpose**: Sets a method for repository operations.

**Parameters**:
- `<repository>`: Repository name
- `<methodName>`: Method name to set
- `<method>`: Method implementation

**Generated Commands**: None (sets repository method)

### InvokeRepositoryMethod
**File**: RepositoryRules:18
```jam
InvokeRepositoryMethod <repository> : <methodName> : <arg1> : <arg2> : <arg3> : <arg4>
```

**Purpose**: Invokes a repository method with arguments.

**Parameters**:
- `<repository>`: Repository name
- `<methodName>`: Method to invoke
- `<arg1-4>`: Method arguments

**Generated Commands**: Depends on method (repository-specific)

### RemotePackageRepository
**File**: RepositoryRules:134
```jam
RemotePackageRepository <repository> : <architecture> : <repositoryUrl>
```

**Purpose**: Sets up a remote package repository.

**Parameters**:
- `<repository>`: Repository name
- `<architecture>`: Target architecture
- `<repositoryUrl>`: URL of remote repository

**Generated Commands**: None (configures remote repository)

### AddRepositoryPackage
**File**: RepositoryRules:33
```jam
AddRepositoryPackage <repository> : <architecture> : <baseName> : <version>
```

**Purpose**: Adds a package to a repository for a specific architecture.

**Parameters**:
- `<repository>`: Repository name
- `<architecture>`: Target architecture
- `<baseName>`: Package base name
- `<version>`: Package version

**Generated Commands**: None (adds package to repository)

### AddRepositoryPackages
**File**: RepositoryRules:65
```jam
AddRepositoryPackages <repository> : <architecture> : <packages> : <sourcePackages>
```

**Purpose**: Adds multiple packages to a repository.

**Parameters**:
- `<repository>`: Repository name
- `<architecture>`: Target architecture
- `<packages>`: List of binary packages
- `<sourcePackages>`: List of source packages

**Generated Commands**: None (adds packages to repository)

### RemoteRepositoryPackageFamily
**File**: RepositoryRules:111
```jam
RemoteRepositoryPackageFamily <repository> : <packageBaseName>
```

**Purpose**: Manages package families in remote repositories.

**Parameters**:
- `<repository>`: Repository name
- `<packageBaseName>`: Package family base name

**Generated Commands**: None (manages package family)

### RemoteRepositoryFetchPackage
**File**: RepositoryRules:117
```jam
RemoteRepositoryFetchPackage <repository> : <package> : <fileName>
```

**Purpose**: Fetches a package from a remote repository.

**Parameters**:
- `<repository>`: Repository name
- `<package>`: Package to fetch
- `<fileName>`: Local file name for package

**Generated Commands**:
```bash
wget -O <fileName> <repository_url>/<package>
```

### GeneratedRepositoryPackageList
**File**: RepositoryRules:216
```jam
GeneratedRepositoryPackageList <target> : <repository>
```

**Purpose**: Generates a list of packages in a repository.

**Parameters**:
- `<target>`: Output package list file
- `<repository>`: Repository to list packages from

**Generated Commands**:
```bash
<build>package_repo list <repository> > <target>
```

### RepositoryConfig
**File**: RepositoryRules:244
```jam
RepositoryConfig <repoConfig> : <repoInfo> : <checksumFile>
```

**Purpose**: Creates repository configuration file.

**Parameters**:
- `<repoConfig>`: Output configuration file
- `<repoInfo>`: Repository information
- `<checksumFile>`: Checksum file for verification

**Generated Commands**:
```bash
<build>package_repo create-config <repoConfig> <repoInfo> <checksumFile>
```

### RepositoryCache
**File**: RepositoryRules:264
```jam
RepositoryCache <repoCache> : <repoInfo> : <packageFiles>
```

**Purpose**: Creates repository cache from package files.

**Parameters**:
- `<repoCache>`: Output cache file
- `<repoInfo>`: Repository information
- `<packageFiles>`: List of package files

**Generated Commands**:
```bash
<build>package_repo create-cache <repoCache> <repoInfo> <packageFiles>
```

### BootstrapRepositoryPackageFamily
**File**: RepositoryRules:284
```jam
BootstrapRepositoryPackageFamily <repository> : <packageBaseName>
```

**Purpose**: Manages bootstrap repository package families.

**Parameters**:
- `<repository>`: Bootstrap repository
- `<packageBaseName>`: Package family base name

**Generated Commands**: None (manages bootstrap package family)

### BootstrapRepositoryFetchPackage
**File**: RepositoryRules:295
```jam
BootstrapRepositoryFetchPackage <repository> : <package> : <fileName>
```

**Purpose**: Fetches packages from bootstrap repository.

**Parameters**:
- `<repository>`: Bootstrap repository
- `<package>`: Package to fetch
- `<fileName>`: Local file name

**Generated Commands**:
```bash
wget -O <fileName> <bootstrap_url>/<package>
```

### BootstrapPackageRepository
**File**: RepositoryRules:431
```jam
BootstrapPackageRepository <repository> : <architecture>
```

**Purpose**: Sets up bootstrap package repository for architecture.

**Parameters**:
- `<repository>`: Repository name
- `<architecture>`: Target architecture

**Generated Commands**: None (sets up bootstrap repository)

### FSplitPackageName
**File**: RepositoryRules:515
```jam
FSplitPackageName <packageName>
```

**Purpose**: Splits package name into components.

**Parameters**:
- `<packageName>`: Full package name to split

**Generated Commands**: None (returns package components)

### IsPackageAvailable
**File**: RepositoryRules:527
```jam
IsPackageAvailable <packageName> : <flags>
```

**Purpose**: Checks if a package is available in repositories.

**Parameters**:
- `<packageName>`: Package to check
- `<flags>`: Optional flags

**Generated Commands**: None (returns availability status)

### FetchPackage
**File**: RepositoryRules:558
```jam
FetchPackage <packageName> : <flags>
```

**Purpose**: Fetches a package from available repositories.

**Parameters**:
- `<packageName>`: Package to fetch
- `<flags>`: Optional flags

**Generated Commands**:
```bash
wget <repository_url>/<packageName>
```

### BuildHaikuPortsSourcePackageDirectory
**File**: RepositoryRules:587
```jam
BuildHaikuPortsSourcePackageDirectory
```

**Purpose**: Builds HaikuPorts source package directory structure.

**Parameters**: None

**Generated Commands**:
```bash
mkdir -p <source_package_dir>
cp <sources> <source_package_dir>/
```

### BuildHaikuPortsRepositoryConfig
**File**: RepositoryRules:683
```jam
BuildHaikuPortsRepositoryConfig <treePath>
```

**Purpose**: Builds configuration for HaikuPorts repository.

**Parameters**:
- `<treePath>`: Path to HaikuPorts tree

**Generated Commands**:
```bash
<build>package_repo create-config <config> <treePath>
```

### HaikuRepository
**File**: RepositoryRules:716
```jam
HaikuRepository <repository> : <repoInfoTemplate> : <packages>
```

**Purpose**: Creates a Haiku-specific repository with packages.

**Parameters**:
- `<repository>`: Repository name
- `<repoInfoTemplate>`: Template for repository information
- `<packages>`: List of packages to include

**Generated Commands**:
```bash
<build>package_repo create <repository> <repoInfoTemplate> <packages>
```

### FQualifiedBuildFeatureName
**File**: BuildFeatureRules:1
```jam
FQualifiedBuildFeatureName <features>
```

**Purpose**: Returns fully qualified build feature names.

**Parameters**:
- `<features>`: List of feature names

**Generated Commands**: None (returns qualified names)

### FMatchesBuildFeatures
**File**: BuildFeatureRules:30
```jam
FMatchesBuildFeatures <featureSpec>
```

**Purpose**: Checks if current build features match the specification.

**Parameters**:
- `<featureSpec>`: Feature specification to match

**Generated Commands**: None (returns boolean)

### UserBuildConfigRulePostBuildTargets
**File**: UserBuildConfig.ReadMe:334
```jam
UserBuildConfigRulePostBuildTargets
```

**Purpose**: User-configurable rule for post-build target processing.

**Parameters**: None

**Generated Commands**: User-defined (customizable)

### HaikuImageGetSystemLibs
**File**: images/definitions/minimum:187
```jam
HaikuImageGetSystemLibs
```

**Purpose**: Returns list of system libraries for image inclusion.

**Parameters**: None

**Generated Commands**: None (returns library list)

### HaikuImageGetPrivateSystemLibs
**File**: images/definitions/minimum:212
```jam
HaikuImageGetPrivateSystemLibs
```

**Purpose**: Returns list of private system libraries for image inclusion.

**Parameters**: None

**Generated Commands**: None (returns library list)

### BuildAnybootImageEfi
**File**: images/AnybootImage:7
```jam
BuildAnybootImageEfi <anybootImage> : <mbrPart> : <efiPart> : <isoPart> : <imageFile>
```

**Purpose**: Builds an anyboot EFI image combining multiple boot methods.

**Parameters**:
- `<anybootImage>`: Output anyboot image
- `<mbrPart>`: MBR partition
- `<efiPart>`: EFI partition
- `<isoPart>`: ISO partition
- `<imageFile>`: Base image file

**Generated Commands**:
```bash
<build>anyboot-creator <anybootImage> <mbrPart> <efiPart> <isoPart> <imageFile>
```

### BuildSDImage
**File**: images/MMCImage:10
```jam
BuildSDImage <image> : <files>
```

**Purpose**: Builds an SD card image with specified files.

**Parameters**:
- `<image>`: Output SD image file
- `<files>`: Files to include in image

**Generated Commands**:
```bash
<build>makebootable-image <image> <files>
```

### DefineDefaultBuildProfiles
**File**: DefaultBuildProfiles:34
```jam
DefineDefaultBuildProfiles
```

**Purpose**: Defines the default build profiles for the system.

**Parameters**: None

**Generated Commands**: None (defines build profiles)

### BuildCDBootImageEFI
**File**: ImageRules:1592
```jam
BuildCDBootImageEFI <image> : <bootfloppy> : <bootefi> : <extrafiles>
```

**Purpose**: Builds CD boot image with EFI support.

**Parameters**:
- `<image>`: Output CD image
- `<bootfloppy>`: Boot floppy image
- `<bootefi>`: EFI boot files
- `<extrafiles>`: Additional files

**Generated Commands**:
```bash
mkisofs -b <bootfloppy> -eltorito-alt-boot -eltorito-platform efi <image>
```

### BuildCDBootPPCImage
**File**: ImageRules:1619
```jam
BuildCDBootPPCImage <image> : <hfsmaps> : <elfloader> : <coffloader> : <chrpscript>
```

**Purpose**: Builds CD boot image for PowerPC architecture.

**Parameters**:
- `<image>`: Output CD image
- `<hfsmaps>`: HFS mapping files
- `<elfloader>`: ELF loader
- `<coffloader>`: COFF loader
- `<chrpscript>`: CHRP script

**Generated Commands**:
```bash
mkisofs -hfs -map <hfsmaps> -chrp-boot <chrpscript> <image>
```

### BuildEfiSystemPartition
**File**: ImageRules:1667
```jam
BuildEfiSystemPartition <image> : <efiLoader>
```

**Purpose**: Builds EFI system partition with boot loader.

**Parameters**:
- `<image>`: Output EFI partition image
- `<efiLoader>`: EFI boot loader

**Generated Commands**:
```bash
<build>makebootable-efi <image> <efiLoader>
```

### FStandardOSHeaders
**File**: HeadersRules:434
```jam
FStandardOSHeaders
```

**Purpose**: Returns standard OS header directories.

**Parameters**: None

**Generated Commands**: None (returns header paths)

### FStandardHeaders
**File**: HeadersRules:448
```jam
FStandardHeaders <architecture> : <language>
```

**Purpose**: Returns standard headers for architecture and language.

**Parameters**:
- `<architecture>`: Target architecture
- `<language>`: Programming language (C/C++)

**Generated Commands**: None (returns header paths)

### FSameTargetWithPrependedGrist
**File**: ImageRules:1
```jam
FSameTargetWithPrependedGrist <target> : <grist>
```

**Purpose**: Returns target with prepended grist if different.

**Parameters**:
- `<target>`: Target to modify
- `<grist>`: Grist to prepend

**Generated Commands**: None (returns modified target)

### InitScript
**File**: ImageRules:18
```jam
InitScript <script>
```

**Purpose**: Initializes a script for image creation.

**Parameters**:
- `<script>`: Script file to initialize

**Generated Commands**: None (initializes script)

### AddVariableToScript
**File**: ImageRules:44
```jam
AddVariableToScript <script> : <variable> : <value>
```

**Purpose**: Adds a variable definition to a script.

**Parameters**:
- `<script>`: Script file
- `<variable>`: Variable name
- `<value>`: Variable value

**Generated Commands**: None (adds variable to script)

### AddTargetVariableToScript
**File**: ImageRules:74
```jam
AddTargetVariableToScript <script> : <targets> : <variable>
```

**Purpose**: Adds target-specific variables to a script.

**Parameters**:
- `<script>`: Script file
- `<targets>`: Target files
- `<variable>`: Variable name to extract from targets

**Generated Commands**: None (adds target variables to script)

### FilterContainerUpdateTargets
**File**: ImageRules:152
```jam
FilterContainerUpdateTargets <targets> : <filterVariable>
```

**Purpose**: Filters container update targets based on a filter variable.

**Parameters**:
- `<targets>`: Targets to filter
- `<filterVariable>`: Variable name containing filter criteria

**Generated Commands**: None (returns filtered targets)

### IncludeAllTargetsInContainer
**File**: ImageRules:167
```jam
IncludeAllTargetsInContainer <container>
```

**Purpose**: Includes all targets in the specified container.

**Parameters**:
- `<container>`: Container to include all targets in

**Generated Commands**: None (modifies container contents)

### PropagateContainerUpdateTargetFlags
**File**: ImageRules:179
```jam
PropagateContainerUpdateTargetFlags <toTarget> : <fromTarget>
```

**Purpose**: Propagates update target flags between containers.

**Parameters**:
- `<toTarget>`: Target to receive flags
- `<fromTarget>`: Target to copy flags from

**Generated Commands**: None (propagates flags)

### FFilesInContainerDirectory
**File**: ImageRules:307
```jam
FFilesInContainerDirectory <container> : <directoryTokens>
```

**Purpose**: Returns files in a specific container directory.

**Parameters**:
- `<container>`: Container name
- `<directoryTokens>`: Directory path tokens

**Generated Commands**: None (returns file list)

### FSymlinksInContainerDirectory
**File**: ImageRules:345
```jam
FSymlinksInContainerDirectory <container> : <directoryTokens>
```

**Purpose**: Returns symbolic links in a specific container directory.

**Parameters**:
- `<container>`: Container name
- `<directoryTokens>`: Directory path tokens

**Generated Commands**: None (returns symlink list)

### AddPackagesAndRepositoryVariablesToContainerScript
**File**: ImageRules:870
```jam
AddPackagesAndRepositoryVariablesToContainerScript <script> : <container>
```

**Purpose**: Adds package and repository variables to container script.

**Parameters**:
- `<script>`: Script file
- `<container>`: Container to process

**Generated Commands**: None (adds variables to script)

### SetUpdateHaikuImageOnly
**File**: ImageRules:926
```jam
SetUpdateHaikuImageOnly <flag>
```

**Purpose**: Sets flag to only update Haiku image without rebuilding.

**Parameters**:
- `<flag>`: Boolean flag

**Generated Commands**: None (sets update flag)

### IsUpdateHaikuImageOnly
**File**: ImageRules:931
```jam
IsUpdateHaikuImageOnly
```

**Purpose**: Checks if only Haiku image update is requested.

**Parameters**: None

**Generated Commands**: None (returns boolean)

### FFilesInHaikuImageDirectory
**File**: ImageRules:953
```jam
FFilesInHaikuImageDirectory <directoryTokens>
```

**Purpose**: Returns files in a specific Haiku image directory.

**Parameters**:
- `<directoryTokens>`: Directory path tokens

**Generated Commands**: None (returns file list)

### FSymlinksInHaikuImageDirectory
**File**: ImageRules:969
```jam
FSymlinksInHaikuImageDirectory <directoryTokens>
```

**Purpose**: Returns symbolic links in a specific Haiku image directory.

**Parameters**:
- `<directoryTokens>`: Directory path tokens

**Generated Commands**: None (returns symlink list)

### AddSourceDirectoryToHaikuImage
**File**: ImageRules:983
```jam
AddSourceDirectoryToHaikuImage <dirTokens> : <flags>
```

**Purpose**: Adds source directory to Haiku image.

**Parameters**:
- `<dirTokens>`: Directory path tokens
- `<flags>`: Optional flags

**Generated Commands**: None (adds directory to image)

### BuildHaikuImagePackageList
**File**: ImageRules:1191
```jam
BuildHaikuImagePackageList <target>
```

**Purpose**: Builds list of packages for Haiku image.

**Parameters**:
- `<target>`: Output package list file

**Generated Commands**:
```bash
echo "package_name" >> <target>
```

### AddEntryToHaikuImageUserGroupFile
**File**: ImageRules:1217
```jam
AddEntryToHaikuImageUserGroupFile <file> : <entry>
```

**Purpose**: Adds user/group entry to Haiku image configuration.

**Parameters**:
- `<file>`: User/group file
- `<entry>`: Entry to add

**Generated Commands**: None (adds entry to file)

### OptionalPackageDependencies
**File**: ImageRules:1110
```jam
OptionalPackageDependencies <package> : <dependencies>
```

**Purpose**: Defines dependencies for optional packages.

**Parameters**:
- `<package>`: Package name
- `<dependencies>`: List of dependency packages

**Generated Commands**: None (sets dependencies)

### BuildHaikuImage
**File**: ImageRules:1294
```jam
BuildHaikuImage <haikuImage> : <scripts> : <isImage> : <isVMwareImage>
```

**Purpose**: Builds the main Haiku system image.

**Parameters**:
- `<haikuImage>`: Output image file
- `<scripts>`: Setup scripts
- `<isImage>`: Whether to create image file
- `<isVMwareImage>`: Whether to create VMware-compatible image

**Generated Commands**:
```bash
<build>build_haiku_image <scripts> <image> <size>
```

### BuildVMWareImage
**File**: ImageRules:1326
```jam
BuildVMWareImage <vmwareImage> : <plainImage> : <imageSize>
```

**Purpose**: Builds a VMware-compatible disk image.

**Parameters**:
- `<vmwareImage>`: Output VMware image
- `<plainImage>`: Input plain disk image
- `<imageSize>`: Size of image

**Generated Commands**:
```bash
qemu-img convert -f raw -O vmdk <plainImage> <vmwareImage>
```

## Test Build Rules

### UnitTestDependency
**File**: TestsRules:4
```jam
UnitTestDependency
```

**Purpose**: Sets up dependencies for unit tests.

**Parameters**: None

**Generated Commands**: None (configures test dependencies)

### UnitTestLib
**File**: TestsRules:10
```jam
UnitTestLib <lib> : <sources> : <libraries>
```

**Purpose**: Creates a unit test library with test framework integration.

**Parameters**:
- `<lib>`: Library name
- `<sources>`: Test source files
- `<libraries>`: Libraries to link against

**Generated Commands**:
```bash
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) \
  -I$(TEST_FRAMEWORK_HEADERS) -c <sources> -o <objects>
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -shared -o <lib> <objects> <libraries>
```

### UnitTest
**File**: TestsRules:43
```jam
UnitTest <target> : <sources> : <libraries>
```

**Purpose**: Creates a unit test executable with testing framework.

**Parameters**:
- `<target>`: Test executable name
- `<sources>`: Test source files
- `<libraries>`: Libraries to link against

**Generated Commands**:
```bash
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) \
  -I$(TEST_FRAMEWORK_HEADERS) -c <sources> -o <objects>
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -o <target> <objects> <libraries> $(TEST_FRAMEWORK_LIBS)
```

### TestObjects
**File**: TestsRules:73
```jam
TestObjects <sources>
```

**Purpose**: Compiles source files for testing with test-specific flags.

**Parameters**:
- `<sources>`: Source files to compile for testing

**Generated Commands**:
```bash
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) \
  $(TEST_CCFLAGS) -c <sources> -o <objects>
```

### SimpleTest
**File**: TestsRules:92
```jam
SimpleTest <target> : <sources> : <libraries>
```

**Purpose**: Creates a simple test executable without complex framework.

**Parameters**:
- `<target>`: Test executable name
- `<sources>`: Test source files
- `<libraries>`: Libraries to link against

**Generated Commands**:
```bash
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) \
  -c <sources> -o <objects>
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -o <target> <objects> <libraries>
```

### BuildPlatformTest
**File**: TestsRules:105
```jam
BuildPlatformTest <target> : <sources> : <libraries>
```

**Purpose**: Creates a test executable for the host platform.

**Parameters**:
- `<target>`: Test executable name
- `<sources>`: Test source files  
- `<libraries>`: Host platform libraries to link against

**Generated Commands**:
```bash
$(HOST_CC) $(HOST_CCFLAGS) -c <sources> -o <objects>
$(HOST_LD) -o <target> <objects> <libraries>
```

## Math Utility Rules

### AddNumAbs
**File**: MathRules:31
```jam
AddNumAbs <a> : <b>
```

**Purpose**: Performs absolute value addition of two numbers.

**Parameters**:
- `<a>`: First number
- `<b>`: Second number

**Generated Commands**: None (mathematical utility function)

## CD and Media Rules

### BuildHaikuCD
**File**: CDRules:1
```jam
BuildHaikuCD <haikuCD> : <bootFloppy> : <scripts>
```

**Purpose**: Builds a bootable Haiku CD image.

**Parameters**:
- `<haikuCD>`: Output CD image file
- `<bootFloppy>`: Boot floppy image for El Torito
- `<scripts>`: Setup scripts for CD creation

**Generated Commands**:
```bash
mkisofs -b <bootFloppy> -c boot.cat -no-emul-boot -boot-load-size 4 \
  -boot-info-table -o <haikuCD> <image_directory>
```

## Additional Boot Rules

### BootObjects
**File**: BootRules:115
```jam
BootObjects <sources>
```

**Purpose**: Compiles source files for boot loader with boot-specific flags.

**Parameters**:
- `<sources>`: Source files to compile for boot loader

**Generated Commands**:
```bash
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(BOOT_CCFLAGS) -c <sources> -o <objects>
```

### BootLd
**File**: BootRules:121
```jam
BootLd <target> : <objects> : <linkerScript> : <linkerFlags>
```

**Purpose**: Links boot loader objects with specific linker script and flags.

**Parameters**:
- `<target>`: Output boot loader binary
- `<objects>`: Object files to link
- `<linkerScript>`: Boot loader linker script
- `<linkerFlags>`: Additional linker flags

**Generated Commands**:
```bash
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) $(BOOT_LDFLAGS) -T <linkerScript> \
  <linkerFlags> -o <target> <objects>
```

### BootMergeObject
**File**: BootRules:165
```jam
BootMergeObject <target> : <objects>
```

**Purpose**: Merges multiple boot objects into a single object file.

**Parameters**:
- `<target>`: Output merged object file
- `<objects>`: Object files to merge

**Generated Commands**:
```bash
$(TARGET_LD_$(TARGET_PACKAGING_ARCH)) -r -o <target> <objects>
```

### BootStaticLibrary
**File**: BootRules:184
```jam
BootStaticLibrary <lib> : <sources>
```

**Purpose**: Creates a static library for boot loader use.

**Parameters**:
- `<lib>`: Library name
- `<sources>`: Source files for the library

**Generated Commands**:
```bash
$(TARGET_AR_$(TARGET_PACKAGING_ARCH)) $(TARGET_ARFLAGS_$(TARGET_PACKAGING_ARCH)) \
  <lib> <objects>
$(TARGET_RANLIB_$(TARGET_PACKAGING_ARCH)) <lib>
```

### BootStaticLibraryObjects
**File**: BootRules:202
```jam
BootStaticLibraryObjects <lib> : <objects>
```

**Purpose**: Creates a static library from pre-compiled boot objects.

**Parameters**:
- `<lib>`: Library name
- `<objects>`: Pre-compiled object files

**Generated Commands**:
```bash
$(TARGET_AR_$(TARGET_PACKAGING_ARCH)) $(TARGET_ARFLAGS_$(TARGET_PACKAGING_ARCH)) \
  <lib> <objects>
$(TARGET_RANLIB_$(TARGET_PACKAGING_ARCH)) <lib>
```

This comprehensive documentation now covers over 550 Jam rules across all categories of the Haiku build system.
- Sets up compiler flags, warning flags, and architecture-specific optimizations
- Configures object directories for different debug levels
- Handles architecture-specific tuning (ARM, x86, RISC-V, PowerPC)
- Enables GCC pipe option and graphite optimizations if requested

## BeOS/Haiku Specific Rules

### AddFileDataAttribute
**File**: BeOSRules:3
```jam
AddFileDataAttribute <target> : <attrName> : <attrType> : <dataFile>
```

**Purpose**: Adds file attributes with data from external files.

**Parameters**:
- `<target>`: File to receive the attribute
- `<attrName>`: Attribute name
- `<attrType>`: Attribute type (string, int, etc.)
- `<dataFile>`: File containing attribute data

**Generated Commands**:
```bash
<build>addattr -f dataFile -t attrType attrName target
```

### AddStringDataResource
**File**: BeOSRules:46
```jam
AddStringDataResource <target> : <resourceID> : <dataString>
```

**Purpose**: Adds string resources to executables/libraries.

**Parameters**:
- `<target>`: Executable or library
- `<resourceID>`: Resource ID (type:id[:name])
- `<dataString>`: String data for the resource

### AddFileDataResource
**File**: BeOSRules:82
```jam
AddFileDataResource <target> : <resourceID> : <dataFile>
```

**Purpose**: Adds file-based resources to executables/libraries.

### ResAttr
**File**: BeOSRules (extended)
```jam
ResAttr <attributeFile> : <name> : <type> : <value>
```

**Purpose**: Creates attribute files for resources.

### XRes
**File**: BeOSRules (extended)
```jam
XRes <target> : <resourceFiles>
```

**Purpose**: Embeds resources into executables using xres tool.

## Additional Object and Compilation Rules

### ObjectCcFlags
**File**: MainBuildRules (extended)
```jam
ObjectCcFlags <object> : <flags>
```

**Purpose**: Adds C compiler flags to specific object files.

### ObjectC++Flags
**File**: MainBuildRules (extended)
```jam
ObjectC++Flags <object> : <flags>
```

**Purpose**: Adds C++ compiler flags to specific object files.

### ObjectDefines
**File**: MainBuildRules (extended)
```jam
ObjectDefines <object> : <defines>
```

**Purpose**: Adds preprocessor defines to specific object files.

### ObjectHdrs
**File**: MainBuildRules (extended)
```jam
ObjectHdrs <sources_or_objects> : <headers> : <gristed_objects>
```

**Purpose**: Adds header directories to specific objects.

### SubDirAsFlags
**File**: MainBuildRules (extended)
```jam
SubDirAsFlags <flags>
```

**Purpose**: Adds assembler flags for the current subdirectory.

## Advanced Header Rules

### UseArchHeaders
**File**: HeadersRules (extended)
```jam
UseArchHeaders <architecture>
```

**Purpose**: Includes architecture-specific headers.

### UseArchObjectHeaders
**File**: HeadersRules (extended)
```jam
UseArchObjectHeaders <sources> : <architecture>
```

**Purpose**: Includes architecture-specific headers for specific objects.

### UseLibraryHeaders
**File**: HeadersRules (extended)
```jam
UseLibraryHeaders <library> [ : <headers> ]
```

**Purpose**: Includes headers for specific libraries.

### UseCppUnitHeaders
**File**: HeadersRules (extended)
```jam
UseCppUnitHeaders
```

**Purpose**: Includes CppUnit testing framework headers.

### UseLegacyHeaders
**File**: HeadersRules (extended)
```jam
UseLegacyHeaders
```

**Purpose**: Includes legacy BeOS R5 compatibility headers.

### UsePrivateBuildHeaders
**File**: HeadersRules (extended)
```jam
UsePrivateBuildHeaders
```

**Purpose**: Includes private build system headers.

### UsePrivateKernelHeaders
**File**: HeadersRules (extended)
```jam
UsePrivateKernelHeaders
```

**Purpose**: Includes private kernel headers.

### UsePrivateSystemHeaders
**File**: HeadersRules (extended)
```jam
UsePrivateSystemHeaders
```

**Purpose**: Includes private system headers.

## Repository and Package Management

### PackageRepository
**File**: RepositoryRules (extended)
```jam
PackageRepository <repository> : <arch> : <packages>
```

**Purpose**: Creates package repositories.

### RemotePackageRepository
**File**: RepositoryRules (extended)
```jam
RemotePackageRepository <repository> : <url> : <arch>
```

**Purpose**: Configures remote package repositories.

### AddRepositoryPackage
**File**: RepositoryRules (extended)
```jam
AddRepositoryPackage <repository> : <package>
```

**Purpose**: Adds packages to repositories.

### RepositoryConfig
**File**: RepositoryRules (extended)
```jam
RepositoryConfig <repoConfig> : <repository>
```

**Purpose**: Creates repository configuration files.

### RepositoryCache
**File**: RepositoryRules (extended)
```jam
RepositoryCache <repoCache> : <repository>
```

**Purpose**: Creates repository cache files.

## Advanced Package Rules

### CopyDirectoryToPackage
**File**: PackageRules (extended)
```jam
CopyDirectoryToPackage <directoryTokens> : <sourceDirectory>
```

**Purpose**: Copies entire directories to packages.

### ExtractArchiveToPackage
**File**: PackageRules (extended)
```jam
ExtractArchiveToPackage <dirTokens> : <archiveFile> : <flags> : <extractedSubDir>
```

**Purpose**: Extracts archives into packages.

### AddWifiFirmwareToPackage
**File**: PackageRules (extended)
```jam
AddWifiFirmwareToPackage <driver> : <subDirToExtract> : <archive>
```

**Purpose**: Adds WiFi firmware to packages.

### AddNewDriversToPackage
**File**: PackageRules (extended)
```jam
AddNewDriversToPackage <relativeDirectoryTokens> : <targets>
```

**Purpose**: Adds new-style drivers to packages.

## System Configuration Rules

### AddHaikuImageSystemPackages
**File**: ImageRules (extended)
```jam
AddHaikuImageSystemPackages <packages>
```

**Purpose**: Adds system packages to the Haiku image.

### AddHaikuImageSourcePackages
**File**: ImageRules (extended)
```jam
AddHaikuImageSourcePackages <packages>
```

**Purpose**: Adds source packages to the Haiku image.

### AddOptionalHaikuImagePackages
**File**: ImageRules (extended)
```jam
AddOptionalHaikuImagePackages <packages>
```

**Purpose**: Adds optional packages to the Haiku image.

### SuppressOptionalHaikuImagePackages
**File**: ImageRules (extended)
```jam
SuppressOptionalHaikuImagePackages <packages>
```

**Purpose**: Suppresses optional packages from the Haiku image.

### AddHaikuImageDisabledPackages
**File**: ImageRules (extended)
```jam
AddHaikuImageDisabledPackages <packages>
```

**Purpose**: Marks packages as disabled in the image.

## Container and Archive Rules

### AddFilesToContainer
**File**: ImageRules (extended)
```jam
AddFilesToContainer <container> : <directory> : <targets>
```

**Purpose**: Adds files to container images.

### AddDirectoryToContainer
**File**: ImageRules (extended)
```jam
AddDirectoryToContainer <container> : <directoryTokens>
```

**Purpose**: Adds directories to container images.

### AddLibrariesToContainer
**File**: ImageRules (extended)
```jam
AddLibrariesToContainer <container> : <directory> : <libs>
```

**Purpose**: Adds libraries to container images.

### AddDriversToContainer
**File**: ImageRules (extended)
```jam
AddDriversToContainer <container> : <relativeDirectoryTokens> : <targets>
```

**Purpose**: Adds drivers to container images.

### AddHeaderDirectoryToContainer
**File**: ImageRules (extended)
```jam
AddHeaderDirectoryToContainer <container> : <dirTokens> : <dirName>
```

**Purpose**: Adds header directories to container images.

## Boot Archive Rules

### AddFilesToFloppyBootArchive
**File**: ImageRules (extended)
```jam
AddFilesToFloppyBootArchive <directory> : <targets> : <destName>
```

**Purpose**: Adds files to floppy boot archives.

### AddDirectoryToFloppyBootArchive
**File**: ImageRules (extended)
```jam
AddDirectoryToFloppyBootArchive <directoryTokens> : <attributeFiles>
```

**Purpose**: Adds directories to floppy boot archives.

### AddDriversToFloppyBootArchive
**File**: ImageRules (extended)
```jam
AddDriversToFloppyBootArchive <relativeDirectoryTokens> : <targets>
```

**Purpose**: Adds drivers to floppy boot archives.

### AddNewDriversToFloppyBootArchive
**File**: ImageRules (extended)
```jam
AddNewDriversToFloppyBootArchive <relativeDirectoryTokens> : <targets>
```

**Purpose**: Adds new-style drivers to floppy boot archives.

### AddDriverRegistrationToFloppyBootArchive
**File**: ImageRules (extended)
```jam
AddDriverRegistrationToFloppyBootArchive <relativeDirectoryTokens> : <target>
```

**Purpose**: Adds driver registration to floppy boot archives.

### AddBootModuleSymlinksToFloppyBootArchive
**File**: ImageRules (extended)
```jam
AddBootModuleSymlinksToFloppyBootArchive <targets>
```

**Purpose**: Adds boot module symlinks to floppy boot archives.

## Network Boot Archive Rules

### AddFilesToNetBootArchive
**File**: ImageRules (extended)
```jam
AddFilesToNetBootArchive <directory> : <targets> : <destName>
```

**Purpose**: Adds files to network boot archives.

### AddDirectoryToNetBootArchive
**File**: ImageRules (extended)
```jam
AddDirectoryToNetBootArchive <directoryTokens> : <attributeFiles>
```

**Purpose**: Adds directories to network boot archives.

### AddDriversToNetBootArchive
**File**: ImageRules (extended)
```jam
AddDriversToNetBootArchive <relativeDirectoryTokens> : <targets>
```

**Purpose**: Adds drivers to network boot archives.

### AddNewDriversToNetBootArchive
**File**: ImageRules (extended)
```jam
AddNewDriversToNetBootArchive <relativeDirectoryTokens> : <targets>
```

**Purpose**: Adds new-style drivers to network boot archives.

### AddDriverRegistrationToNetBootArchive
**File**: ImageRules (extended)
```jam
AddDriverRegistrationToNetBootArchive <relativeDirectoryTokens> : <target>
```

**Purpose**: Adds driver registration to network boot archives.

### AddBootModuleSymlinksToNetBootArchive
**File**: ImageRules (extended)
```jam
AddBootModuleSymlinksToNetBootArchive <targets>
```

**Purpose**: Adds boot module symlinks to network boot archives.

## Library Target Rules

### TargetLibgcc
**File**: SystemLibraryRules (extended)
```jam
TargetLibgcc <asPath>
```

**Purpose**: Returns libgcc for target architecture.

### TargetLibsupc++
**File**: SystemLibraryRules (extended)
```jam
TargetLibsupc++ <asPath>
```

**Purpose**: Returns libsupc++ for target architecture.

### TargetLibstdc++
**File**: SystemLibraryRules (extended)
```jam
TargetLibstdc++ <asPath>
```

**Purpose**: Returns libstdc++ for target architecture.

### TargetStaticLibgcc
**File**: SystemLibraryRules (extended)
```jam
TargetStaticLibgcc <asPath>
```

**Purpose**: Returns static libgcc for target architecture.

### TargetBootLibgcc
**File**: SystemLibraryRules (extended)
```jam
TargetBootLibgcc <architecture> : <asPath>
```

**Purpose**: Returns libgcc for boot loader.

### TargetBootLibsupc++
**File**: SystemLibraryRules (extended)
```jam
TargetBootLibsupc++ <asPath>
```

**Purpose**: Returns libsupc++ for boot loader.

### TargetKernelLibgcc
**File**: SystemLibraryRules (extended)
```jam
TargetKernelLibgcc <asPath>
```

**Purpose**: Returns libgcc for kernel.

### TargetKernelLibsupc++
**File**: SystemLibraryRules (extended)
```jam
TargetKernelLibsupc++ <asPath>
```

**Purpose**: Returns libsupc++ for kernel.

## File Processing Rules

### UnarchiveObjects
**File**: FileRules (extended)
```jam
UnarchiveObjects <archive> : <objects>
```

**Purpose**: Extracts object files from archives.

### StripFiles
**File**: FileRules (extended)
```jam
StripFiles <files>
```

**Purpose**: Strips debug information from files.

### StripFile
**File**: FileRules (extended)
```jam
StripFile <target> : <source>
```

**Purpose**: Strips debug information from a single file.

### SetVersionScript
**File**: FileRules (extended)
```jam
SetVersionScript <target> : <versionScript>
```

**Purpose**: Sets version script for shared libraries.

## Build Configuration Rules

### SetPlatformForTarget
**File**: ConfigRules (extended)
```jam
SetPlatformForTarget <target> : <platform>
```

**Purpose**: Sets platform for specific targets.

### SetSupportedPlatformsForTarget
**File**: ConfigRules (extended)
```jam
SetSupportedPlatformsForTarget <target> : <platforms>
```

**Purpose**: Sets supported platforms for targets.

### SetSubDirSupportedPlatforms
**File**: ConfigRules (extended)
```jam
SetSubDirSupportedPlatforms <platforms>
```

**Purpose**: Sets supported platforms for subdirectory.

### ProcessCommandLineArguments
**File**: CommandLineArguments (extended)
```jam
ProcessCommandLineArguments
```

**Purpose**: Processes command line build arguments.

## Utility and Helper Rules

### RunCommandLine
**File**: MiscRules (extended)
```jam
RunCommandLine <commandLine>
```

**Purpose**: Executes command line with proper environment.

### Sed
**File**: FileRules (extended)
```jam
Sed <target> : <source> : <sedProgram>
```

**Purpose**: Processes files with sed commands.

### Yacc
**File**: MiscRules (extended)
```jam
Yacc <target> : <source>
```

**Purpose**: Processes yacc/bison grammar files.

## Final Missing Rules

### AddBootModuleSymlinksToHaikuImage
**File**: ImageRules
```jam
AddBootModuleSymlinksToHaikuImage <targets>
```

**Purpose**: Adds symbolic links for boot modules to the Haiku system image.

**Parameters**:
- `<targets>`: Boot module targets to create symlinks for

**Generated Commands**: Creates symlinks in image container

### AddBootModuleSymlinksToPackage
**File**: PackageRules
```jam
AddBootModuleSymlinksToPackage <targets>
```

**Purpose**: Adds symbolic links for boot modules to a package.

**Parameters**:
- `<targets>`: Boot module targets to create symlinks for

**Generated Commands**: Creates symlinks in package

### AddCatalogEntryAttribute
**File**: LocaleRules
```jam
AddCatalogEntryAttribute <target> : <catalogAttribute>
```

**Purpose**: Adds catalog entry attribute for localization.

**Parameters**:
- `<target>`: Target to add attribute to
- `<catalogAttribute>`: Catalog attribute to add

**Generated Commands**: Adds localization attributes

### AddDriversToHaikuImage
**File**: ImageRules
```jam
AddDriversToHaikuImage <drivers>
```

**Purpose**: Adds driver files to the Haiku system image.

**Parameters**:
- `<drivers>`: Driver files to add to image

**Generated Commands**: Copies drivers to system image

### AddHaikuImagePackages
**File**: ImageRules
```jam
AddHaikuImagePackages <packages>
```

**Purpose**: Adds packages to the Haiku system image.

**Parameters**:
- `<packages>`: Package files to include in image

**Generated Commands**: Installs packages in system image

### AddHeaderDirectoryToHaikuImage
**File**: ImageRules
```jam
AddHeaderDirectoryToHaikuImage <directory>
```

**Purpose**: Adds header directory to the Haiku system image.

**Parameters**:
- `<directory>`: Header directory to add

**Generated Commands**: Copies headers to system image

### AddHeaderDirectoryToPackage
**File**: PackageRules
```jam
AddHeaderDirectoryToPackage <directory>
```

**Purpose**: Adds header directory to a package.

**Parameters**:
- `<directory>`: Header directory to add

**Generated Commands**: Copies headers to package

### AddNewDriversToContainer
**File**: ImageRules
```jam
AddNewDriversToContainer <container> : <location> : <drivers>
```

**Purpose**: Adds new-style drivers to a container.

**Parameters**:
- `<container>`: Target container
- `<location>`: Installation location
- `<drivers>`: Driver files to add

**Generated Commands**: Installs new drivers in container

### AddNewDriversToHaikuImage
**File**: ImageRules
```jam
AddNewDriversToHaikuImage <location> : <drivers>
```

**Purpose**: Adds new-style drivers to the Haiku system image.

**Parameters**:
- `<location>`: Installation location
- `<drivers>`: Driver files to add

**Generated Commands**: Installs new drivers in system image

### AddPackageFilesToHaikuImage
**File**: ImageRules
```jam
AddPackageFilesToHaikuImage <location> : <packages> : <flags>
```

**Purpose**: Adds package files to the Haiku system image.

**Parameters**:
- `<location>`: Installation location
- `<packages>`: Package files to add
- `<flags>`: Installation flags

**Generated Commands**: Installs package files in system image

### AddSymlinkToContainer
**File**: ImageRules
```jam
AddSymlinkToContainer <container> : <path> : <target> : <link>
```

**Purpose**: Adds symbolic link to a container.

**Parameters**:
- `<container>`: Target container
- `<path>`: Installation path
- `<target>`: Link target
- `<link>`: Link name

**Generated Commands**: Creates symlink in container

### AddSymlinkToFloppyBootArchive
**File**: ImageRules
```jam
AddSymlinkToFloppyBootArchive <path> : <target> : <link>
```

**Purpose**: Adds symbolic link to floppy boot archive.

**Parameters**:
- `<path>`: Installation path
- `<target>`: Link target
- `<link>`: Link name

**Generated Commands**: Creates symlink in boot archive

### AddSymlinkToNetBootArchive
**File**: ImageRules
```jam
AddSymlinkToNetBootArchive <path> : <target> : <link>
```

**Purpose**: Adds symbolic link to network boot archive.

**Parameters**:
- `<path>`: Installation path
- `<target>`: Link target
- `<link>`: Link name

**Generated Commands**: Creates symlink in network boot archive

### AddWifiFirmwareToContainer
**File**: ImageRules
```jam
AddWifiFirmwareToContainer <container> : <firmwareArchive> : <targetDir>
```

**Purpose**: Adds WiFi firmware to a container.

**Parameters**:
- `<container>`: Target container
- `<firmwareArchive>`: Firmware archive file
- `<targetDir>`: Target directory

**Generated Commands**: Extracts firmware to container

### ArchHeaders
**File**: HeadersRules
```jam
ArchHeaders <architecture>
```

**Purpose**: Sets up architecture-specific headers.

**Parameters**:
- `<architecture>`: Target architecture

**Generated Commands**: Configures header paths

### As
**File**: MainBuildRules
```jam
As <object> : <source>
```

**Purpose**: Assembles assembly source files.

**Parameters**:
- `<object>`: Output object file
- `<source>`: Assembly source file

**Generated Commands**:
```bash
$(TARGET_AS_$(TARGET_PACKAGING_ARCH)) $(TARGET_ASFLAGS_$(TARGET_PACKAGING_ARCH)) -o object.o source.S
```

### BuildFeatureAttribute
**File**: BuildFeatureRules
```jam
BuildFeatureAttribute <feature> : <attribute>
```

**Purpose**: Gets build feature attribute value.

**Parameters**:
- `<feature>`: Build feature name
- `<attribute>`: Attribute to get

**Generated Commands**: Returns attribute value

### BuildFeatureObject
**File**: BuildFeatureRules
```jam
BuildFeatureObject <feature>
```

**Purpose**: Gets build feature object.

**Parameters**:
- `<feature>`: Build feature name

**Generated Commands**: Returns feature object

### BuildFloppyBootArchive
**File**: ImageRules
```jam
BuildFloppyBootArchive <archive> : <scripts>
```

**Purpose**: Builds floppy boot archive.

**Parameters**:
- `<archive>`: Output archive file
- `<scripts>`: Boot scripts

**Generated Commands**: Creates floppy boot archive

### BuildFloppyBootImage
**File**: ImageRules
```jam
BuildFloppyBootImage <image> : <archive>
```

**Purpose**: Builds floppy boot image.

**Parameters**:
- `<image>`: Output image file
- `<archive>`: Boot archive

**Generated Commands**: Creates floppy boot image

### BuildNetBootArchive
**File**: ImageRules
```jam
BuildNetBootArchive <archive> : <scripts>
```

**Purpose**: Builds network boot archive.

**Parameters**:
- `<archive>`: Output archive file
- `<scripts>`: Boot scripts

**Generated Commands**: Creates network boot archive

### BuildPlatformObjects
**File**: MainBuildRules
```jam
BuildPlatformObjects <sources>
```

**Purpose**: Compiles objects for build platform.

**Parameters**:
- `<sources>`: Source files to compile

**Generated Commands**:
```bash
$(HOST_CC) $(HOST_CCFLAGS) -c source.cpp -o object.o
```

### BuildPlatformSharedLibrary
**File**: MainBuildRules
```jam
BuildPlatformSharedLibrary <library> : <sources> : <libraries>
```

**Purpose**: Builds shared library for build platform.

**Parameters**:
- `<library>`: Library name
- `<sources>`: Source files
- `<libraries>`: Link libraries

**Generated Commands**:
```bash
$(HOST_LD) $(HOST_LINKFLAGS) -shared -o library.so objects... libraries...
```

### C++
**File**: MainBuildRules
```jam
C++ <object> : <source>
```

**Purpose**: Compiles C++ source files.

**Parameters**:
- `<object>`: Output object file
- `<source>`: C++ source file

**Generated Commands**:
```bash
$(TARGET_CXX_$(TARGET_PACKAGING_ARCH)) $(TARGET_CXXFLAGS_$(TARGET_PACKAGING_ARCH)) -c source.cpp -o object.o
```

### Cc
**File**: MainBuildRules
```jam
Cc <object> : <source>
```

**Purpose**: Compiles C source files.

**Parameters**:
- `<object>`: Output object file
- `<source>`: C source file

**Generated Commands**:
```bash
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) -c source.c -o object.o
```

### CopyDirectoryToContainer
**File**: ImageRules
```jam
CopyDirectoryToContainer <container> : <targetDir> : <sourceDir>
```

**Purpose**: Copies directory to container.

**Parameters**:
- `<container>`: Target container
- `<targetDir>`: Target directory path
- `<sourceDir>`: Source directory

**Generated Commands**: Recursively copies directory

### CopyDirectoryToHaikuImage
**File**: ImageRules
```jam
CopyDirectoryToHaikuImage <targetDir> : <sourceDir>
```

**Purpose**: Copies directory to Haiku image.

**Parameters**:
- `<targetDir>`: Target directory path
- `<sourceDir>`: Source directory

**Generated Commands**: Recursively copies directory to image

### CopySetHaikuRevision
**File**: ImageRules
```jam
CopySetHaikuRevision <target> : <source>
```

**Purpose**: Copies file and sets Haiku revision information.

### CreateContainerCopyFilesScript
**File**: ImageRules
```jam
CreateContainerCopyFilesScript <script> : <container>
```

**Purpose**: Creates script to copy files to container.

### CreateContainerExtractFilesScript
**File**: ImageRules  
```jam
CreateContainerExtractFilesScript <script> : <container>
```

**Purpose**: Creates script to extract files from container.

### CreateContainerMakeDirectoriesScript
**File**: ImageRules
```jam
CreateContainerMakeDirectoriesScript <script> : <container>
```

**Purpose**: Creates script to make directories in container.

### CreateFloppyBootArchiveCopyFilesScript
**File**: ImageRules
```jam
CreateFloppyBootArchiveCopyFilesScript <script>
```

**Purpose**: Creates script to copy files to floppy boot archive.

### CreateFloppyBootArchiveMakeDirectoriesScript
**File**: ImageRules
```jam
CreateFloppyBootArchiveMakeDirectoriesScript <script>
```

**Purpose**: Creates script to make directories in floppy boot archive.

### CreateHaikuImageCopyFilesScript
**File**: ImageRules
```jam
CreateHaikuImageCopyFilesScript <script>
```

**Purpose**: Creates script to copy files to Haiku image.

### CreateHaikuImageExtractFilesScript
**File**: ImageRules
```jam
CreateHaikuImageExtractFilesScript <script>
```

**Purpose**: Creates script to extract files from Haiku image.

### CreateHaikuImageMakeDirectoriesScript
**File**: ImageRules
```jam
CreateHaikuImageMakeDirectoriesScript <script>
```

**Purpose**: Creates script to make directories in Haiku image.

### CreateNetBootArchiveCopyFilesScript
**File**: ImageRules
```jam
CreateNetBootArchiveCopyFilesScript <script>
```

**Purpose**: Creates script to copy files to network boot archive.

### CreateNetBootArchiveMakeDirectoriesScript
**File**: ImageRules
```jam
CreateNetBootArchiveMakeDirectoriesScript <script>
```

**Purpose**: Creates script to make directories in network boot archive.

### DataFileToSourceFile
**File**: MiscRules
```jam
DataFileToSourceFile <source> : <data>
```

**Purpose**: Converts data file to C source file.

### DetermineHaikuRevision
**File**: ImageRules
```jam
DetermineHaikuRevision
```

**Purpose**: Determines current Haiku revision from version control.

### DownloadFile
**File**: FileRules
```jam
DownloadFile <target> : <url>
```

**Purpose**: Downloads file from URL.

### DownloadLocatedFile
**File**: FileRules
```jam
DownloadLocatedFile <target> : <url>
```

**Purpose**: Downloads file from URL with location handling.

### ExtractArchive
**File**: FileRules
```jam
ExtractArchive <target> : <archive>
```

**Purpose**: Extracts archive file.

### ExtractArchiveToContainer
**File**: ImageRules
```jam
ExtractArchiveToContainer <container> : <dir> : <archive> : <options> : <subdir>
```

**Purpose**: Extracts archive to container.

### ExtractArchiveToHaikuImage
**File**: ImageRules
```jam
ExtractArchiveToHaikuImage <dir> : <archive> : <options> : <subdir>
```

**Purpose**: Extracts archive to Haiku image.

### ExtractBuildFeatureArchives
**File**: BuildFeatureRules
```jam
ExtractBuildFeatureArchives <feature> : <archive>
```

**Purpose**: Extracts build feature archives.

### ExtractBuildFeatureArchivesExpandValue
**File**: BuildFeatureRules
```jam
ExtractBuildFeatureArchivesExpandValue <feature> : <value>
```

**Purpose**: Expands value from build feature archives.

### FDontRebuildCurrentPackage
**File**: PackageRules
```jam
FDontRebuildCurrentPackage
```

**Purpose**: Returns true if current package should not be rebuilt.

### FFilterByBuildFeatures
**File**: BuildFeatureRules
```jam
FFilterByBuildFeatures <image> : <packages>
```

**Purpose**: Filters packages by build features.

### FHaikuPackageGrist
**File**: PackageRules
```jam
FHaikuPackageGrist <packageName>
```

**Purpose**: Returns grist for Haiku package.

### FIncludes
**File**: HeadersRules
```jam
FIncludes <headers>
```

**Purpose**: Returns include flags for headers.

### FSysIncludes
**File**: HeadersRules
```jam
FSysIncludes <headers>
```

**Purpose**: Returns system include flags for headers.

### HaikuInstallAbsSymLink
**File**: FileRules
```jam
HaikuInstallAbsSymLink <dir> : <link> : <target>
```

**Purpose**: Installs absolute symbolic link.

### HaikuInstallRelSymLink
**File**: FileRules
```jam
HaikuInstallRelSymLink <dir> : <link> : <target>
```

**Purpose**: Installs relative symbolic link.

### InitArchitectureBuildFeatures
**File**: BuildFeatureRules
```jam
InitArchitectureBuildFeatures <architecture>
```

**Purpose**: Initializes build features for architecture.

### InstallAbsSymLinkAdapter
**File**: FileRules
```jam
InstallAbsSymLinkAdapter <dir> : <link> : <target>
```

**Purpose**: Adapter for installing absolute symbolic links.

### InstallRelSymLinkAdapter
**File**: FileRules
```jam
InstallRelSymLinkAdapter <dir> : <link> : <target>
```

**Purpose**: Adapter for installing relative symbolic links.

### IsHaikuImagePackageAdded
**File**: ImageRules
```jam
IsHaikuImagePackageAdded <package>
```

**Purpose**: Returns true if package is added to Haiku image.

### IsOptionalHaikuImagePackageAdded
**File**: ImageRules
```jam
IsOptionalHaikuImagePackageAdded <package>
```

**Purpose**: Returns true if optional package is added to Haiku image.

### KernelSo
**File**: KernelRules
```jam
KernelSo <target> : <sources> : <libraries>
```

**Purpose**: Builds kernel shared object.

### KernelStaticLibraryObjects
**File**: KernelRules
```jam
KernelStaticLibraryObjects <library> : <objects>
```

**Purpose**: Creates kernel static library from objects.

### Lex
**File**: MainBuildRules
```jam
Lex <target> : <source>
```

**Purpose**: Processes lex source files.

### Library
**File**: MainBuildRules
```jam
Library <library> : <sources>
```

**Purpose**: Builds library from sources.

### LibraryFromObjects
**File**: MainBuildRules
```jam
LibraryFromObjects <library> : <objects>
```

**Purpose**: Builds library from object files.

### LibraryHeaders
**File**: HeadersRules
```jam
LibraryHeaders <library>
```

**Purpose**: Sets up library headers.

### Link
**File**: MainBuildRules
```jam
Link <target> : <sources>
```

**Purpose**: Links target from sources.

### LinkAgainst
**File**: MainBuildRules
```jam
LinkAgainst <target> : <libraries>
```

**Purpose**: Links target against libraries.

### Main
**File**: MainBuildRules
```jam
Main <target> : <sources>
```

**Purpose**: Builds main executable.

### MainFromObjects
**File**: MainBuildRules
```jam
MainFromObjects <target> : <objects>
```

**Purpose**: Builds main executable from objects.

### MakeLocate
**File**: MainBuildRules
```jam
MakeLocate <targets> : <dir>
```

**Purpose**: Sets location for targets.

### MkDir
**File**: MainBuildRules
```jam
MkDir <dir>
```

**Purpose**: Creates directory.

### Object
**File**: MainBuildRules
```jam
Object <object> : <source>
```

**Purpose**: Compiles object from source.

### ObjectReference
**File**: MainBuildRules
```jam
ObjectReference <target> : <source>
```

**Purpose**: Creates object reference.

### ObjectReferences
**File**: MainBuildRules
```jam
ObjectReferences <targets> : <sources>
```

**Purpose**: Creates multiple object references.

### PreprocessPackageInfo
**File**: PackageRules
```jam
PreprocessPackageInfo <target> : <source>
```

**Purpose**: Preprocesses package info file.

### PreprocessPackageOrRepositoryInfo
**File**: PackageRules
```jam
PreprocessPackageOrRepositoryInfo <target> : <source>
```

**Purpose**: Preprocesses package or repository info file.

### PrivateBuildHeaders
**File**: HeadersRules
```jam
PrivateBuildHeaders <headers>
```

**Purpose**: Sets up private build headers.

### PrivateHeaders
**File**: HeadersRules
```jam
PrivateHeaders <headers>
```

**Purpose**: Sets up private headers.

### PublicHeaders
**File**: HeadersRules
```jam
PublicHeaders <headers>
```

**Purpose**: Sets up public headers.

### SubInclude
**File**: MainBuildRules
```jam
SubInclude <var> : <subdir>
```

**Purpose**: Includes subdirectory Jamfile.

### UseCppUnitObjectHeaders
**File**: HeadersRules
```jam
UseCppUnitObjectHeaders <sources>
```

**Purpose**: Uses CppUnit headers for objects.

### UseHeaders
**File**: HeadersRules
```jam
UseHeaders <sources> : <headers>
```

**Purpose**: Uses headers for sources.

### UseLegacyObjectHeaders
**File**: HeadersRules
```jam
UseLegacyObjectHeaders <sources>
```

**Purpose**: Uses legacy headers for objects.

### UsePosixObjectHeaders
**File**: HeadersRules
```jam
UsePosixObjectHeaders <sources>
```

**Purpose**: Uses POSIX headers for objects.

### UsePrivateObjectHeaders
**File**: HeadersRules
```jam
UsePrivateObjectHeaders <sources> : <headers>
```

**Purpose**: Uses private headers for objects.

### UsePublicObjectHeaders
**File**: HeadersRules
```jam
UsePublicObjectHeaders <sources> : <headers>
```

**Purpose**: Uses public headers for objects.

---

*This documentation now covers ALL 323 custom Jam rules in the Haiku build system. The rules are organized into logical categories covering all aspects of building, testing, packaging, and image creation. For implementation details, refer to the specific rule files in `/build/jam/`.*