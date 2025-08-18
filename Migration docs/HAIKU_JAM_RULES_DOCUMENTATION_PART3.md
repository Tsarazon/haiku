# Haiku Jam Build System Rules Documentation - Part 3: Advanced Features & Cross-compilation

## Overview

This is Part 3 of the comprehensive Haiku Jam rules documentation, covering advanced features, cross-compilation setup, platform configuration, and architecture-specific rules.

**Documentation Parts**:
- **Part 1**: Core Rules, Application Building, Libraries, Kernel
- **Part 2**: Package Management, Image Building, File Operations  
- **Part 3**: Advanced Features, Cross-compilation, Platform Configuration (this document)

## Table of Contents - Part 3

1. [Architecture-Specific Rules](#architecture-specific-rules)
2. [Build Feature Rules](#build-feature-rules)
3. [Header Management Rules](#header-management-rules)
4. [Configuration Rules](#configuration-rules)
5. [Cross-compilation Setup](#cross-compilation-setup)
6. [Platform Configuration](#platform-configuration)
7. [Helper and Utility Rules](#helper-and-utility-rules)
8. [Miscellaneous Utility Rules](#miscellaneous-utility-rules)
9. [Math Utility Rules](#math-utility-rules)
10. [CD and Media Rules](#cd-and-media-rules)

## Architecture-Specific Rules

### ArchitectureSetup
**File**: ArchitectureRules:1
```jam
ArchitectureSetup <architecture>
```

**Purpose**: Configures compilation flags and environment for specific CPU architectures.

**Supported Architectures**:
- x86, x86_64: Intel/AMD 32-bit and 64-bit
- arm, arm64: ARM 32-bit and 64-bit (aarch64)
- riscv64: RISC-V 64-bit
- m68k: Motorola 68000 series (legacy)
- ppc: PowerPC (legacy)
- sparc: SPARC (legacy)

**Generated Variables**:
```jam
# Architecture-specific compiler flags
TARGET_CCFLAGS_$(architecture) = -march=x86-64 -mtune=generic ;
TARGET_C++FLAGS_$(architecture) = -march=x86-64 -mtune=generic ;
TARGET_ASFLAGS_$(architecture) = -march=x86-64 ;

# Toolchain configuration
TARGET_CC_$(architecture) = x86_64-unknown-haiku-gcc ;
TARGET_C++_$(architecture) = x86_64-unknown-haiku-g++ ;
TARGET_LD_$(architecture) = x86_64-unknown-haiku-ld ;
TARGET_AR_$(architecture) = x86_64-unknown-haiku-ar ;

# Warning and debug flags per architecture
TARGET_WARNING_CCFLAGS_$(architecture) = -Wall -Wextra -Werror ;
TARGET_DEBUG_0_CCFLAGS_$(architecture) = -O2 -DNDEBUG ;
TARGET_DEBUG_1_CCFLAGS_$(architecture) = -g -O0 ;
```

**Key Features**:
- Sets architecture-specific compiler flags (-march, -mcpu)
- Configures cross-compilation toolchains
- Manages warning flags per architecture
- Handles legacy GCC vs modern compiler differences
- Sets up debugging and optimization levels

### KernelArchitectureSetup
**File**: ArchitectureRules:211
```jam
KernelArchitectureSetup <architecture>
```

**Purpose**: Sets up kernel-specific architecture configuration.

**Generated Variables**:
```jam
# Kernel-specific flags
TARGET_KERNEL_CCFLAGS = -fno-builtin -fno-strict-aliasing ;
TARGET_KERNEL_C++FLAGS = -fno-exceptions -fno-rtti ;
TARGET_KERNEL_PIC_CCFLAGS = -fPIC -fno-common ;

# Kernel toolchain (may differ from user-space)
HAIKU_KERNEL_ARCH = $(architecture) ;
TARGET_KERNEL_ARCH = $(architecture) ;
```

### ArchitectureSetupWarnings
**File**: ArchitectureRules:579
```jam
ArchitectureSetupWarnings <architecture>
```

**Purpose**: Configures warning levels for specific architecture.

### MultiArchIfPrimary
**File**: ArchitectureRules:812
```jam
MultiArchIfPrimary <ifValue> : <elseValue> : <architecture>
```

**Purpose**: Conditional value based on primary architecture.

### MultiArchConditionalGristFiles
**File**: ArchitectureRules:829
```jam
MultiArchConditionalGristFiles <files> : <primaryGrist> : <secondaryGrist>
```

**Purpose**: Applies different grist based on architecture priority.

### MultiArchDefaultGristFiles
**File**: ArchitectureRules:847
```jam
MultiArchDefaultGristFiles <files> : <gristPrefix> : <architecture>
```

**Purpose**: Applies default grist for multi-architecture files.

### MultiArchSubDirSetup
**File**: ArchitectureRules:871
```jam
MultiArchSubDirSetup <architectures>
```

**Purpose**: Enables building same code for multiple architectures simultaneously.

**Key Features**:
- Creates separate object directories per architecture
- Manages cross-architecture dependencies
- Handles primary vs secondary architecture builds
- Supports hybrid x86/x86_64 systems

**Usage Example**:
```jam
local architectureObject ;
for architectureObject in [ MultiArchSubDirSetup ] {
    on $(architectureObject) {
        SharedLibrary libbe.so : sources : libraries ;
    }
}
```

## Build Feature Rules

### FQualifiedBuildFeatureName
**File**: BuildFeatureRules:1
```jam
FQualifiedBuildFeatureName <features>
```

**Purpose**: Returns qualified build feature names.

### FIsBuildFeatureEnabled
**File**: BuildFeatureRules:17
```jam
FIsBuildFeatureEnabled <feature>
```

**Purpose**: Checks if build feature is enabled.

**Usage Example**:
```jam
if [ FIsBuildFeatureEnabled openssl ] {
    libraries += ssl crypto ;
    UseLibraryHeaders openssl ;
}
```

### FMatchesBuildFeatures
**File**: BuildFeatureRules:30
```jam
FMatchesBuildFeatures <specification>
```

**Purpose**: Checks if current build matches feature specification.

**Specification Syntax**:
- `feature` - feature must be enabled
- `!feature` - feature must be disabled  
- `feature1 & feature2` - both features enabled
- `feature1 | feature2` - either feature enabled
- `@{ feature1 feature2 }@` - complex expressions

### FFilterByBuildFeatures
**File**: BuildFeatureRules:59
```jam
FFilterByBuildFeatures <list>
```

**Purpose**: Filters list based on build feature conditions.

**Usage Example**:
```jam
local packages = [ FFilterByBuildFeatures
    regular_packages
    @{ openssl_package zlib_package }@
    !gcc2 @{ modern_packages }@
];
```

### EnableBuildFeatures
**File**: BuildFeatureRules:146
```jam
EnableBuildFeatures <features> : <specification>
```

**Purpose**: Enables build features based on specification.

### BuildFeatureObject
**File**: BuildFeatureRules:175
```jam
BuildFeatureObject <feature>
```

**Purpose**: Returns build feature object for advanced operations.

### SetBuildFeatureAttribute
**File**: BuildFeatureRules:193
```jam
SetBuildFeatureAttribute <feature> : <attribute> : <values> : <package>
```

**Purpose**: Sets attributes for build features.

### BuildFeatureAttribute
**File**: BuildFeatureRules:208
```jam
BuildFeatureAttribute <feature> : <attribute> : <flags>
```

**Purpose**: Gets build feature attribute values.

**Usage Example**:
```jam
local opensslHeaders = [ BuildFeatureAttribute openssl : headers ] ;
local opensslLibs = [ BuildFeatureAttribute openssl : libraries ] ;
```

### ExtractBuildFeatureArchivesExpandValue
**File**: BuildFeatureRules:246
```jam
ExtractBuildFeatureArchivesExpandValue <value> : <fileName>
```

**Purpose**: Expands variables in build feature archive values.

### ExtractBuildFeatureArchives
**File**: BuildFeatureRules:320
```jam
ExtractBuildFeatureArchives <feature> : <list>
```

**Purpose**: Extracts build feature archives and sets up headers/libraries.

### InitArchitectureBuildFeatures
**File**: BuildFeatureRules:433
```jam
InitArchitectureBuildFeatures <architecture>
```

**Purpose**: Initializes build features for specific architecture.

## Header Management Rules

### FIncludes
**File**: HeadersRules:5
```jam
FIncludes <headers> : <option>
```

**Purpose**: Formats header directories for compiler include flags.

### FSysIncludes
**File**: HeadersRules:15
```jam
FSysIncludes <headers> : <option>
```

**Purpose**: Formats system header directories for compiler.

### SubDirSysHdrs
**File**: HeadersRules:21
```jam
SubDirSysHdrs <headers>
```

**Purpose**: Sets system headers for current subdirectory.

### ObjectSysHdrs
**File**: HeadersRules:36
```jam
ObjectSysHdrs <objects> : <headers>
```

**Purpose**: Sets system headers for specific objects.

### SourceHdrs
**File**: HeadersRules:89
```jam
SourceHdrs <sources> : <headers>
```

**Purpose**: Sets headers for specific source files.

### SourceSysHdrs
**File**: HeadersRules:110
```jam
SourceSysHdrs <sources> : <headers>
```

**Purpose**: Sets system headers for specific source files.

### PublicHeaders
**File**: HeadersRules:131
```jam
PublicHeaders <headers>
```

**Purpose**: Declares public headers for inclusion.

### PrivateHeaders
**File**: HeadersRules:148
```jam
PrivateHeaders <headers>
```

**Purpose**: Declares private headers for inclusion.

### PrivateBuildHeaders
**File**: HeadersRules:165
```jam
PrivateBuildHeaders <headers>
```

**Purpose**: Declares private build headers.

### LibraryHeaders
**File**: HeadersRules:182
```jam
LibraryHeaders <library>
```

**Purpose**: Sets up headers for specific library.

### ArchHeaders
**File**: HeadersRules:199
```jam
ArchHeaders <architecture>
```

**Purpose**: Sets up architecture-specific headers.

### UseHeaders
**File**: HeadersRules:209
```jam
UseHeaders <sources> : <headers>
```

**Purpose**: Makes sources use specific headers.

### UsePublicHeaders
**File**: HeadersRules:232
```jam
UsePublicHeaders <headers>
```

**Purpose**: Uses public headers in current subdirectory.

### UsePublicObjectHeaders
**File**: HeadersRules:245
```jam
UsePublicObjectHeaders <sources> : <headers>
```

**Purpose**: Uses public headers for specific objects.

### UsePrivateHeaders
**File**: HeadersRules:257
```jam
UsePrivateHeaders <headers>
```

**Purpose**: Uses private headers in current subdirectory.

### UsePrivateObjectHeaders
**File**: HeadersRules:274
```jam
UsePrivateObjectHeaders <sources> : <headers>
```

**Purpose**: Uses private headers for specific objects.

### UsePrivateBuildHeaders
**File**: HeadersRules:296
```jam
UsePrivateBuildHeaders
```

**Purpose**: Uses private build headers.

### UseCppUnitHeaders
**File**: HeadersRules:313
```jam
UseCppUnitHeaders
```

**Purpose**: Uses CppUnit testing framework headers.

### UseCppUnitObjectHeaders
**File**: HeadersRules:319
```jam
UseCppUnitObjectHeaders <sources>
```

**Purpose**: Uses CppUnit headers for specific objects.

### UseArchHeaders
**File**: HeadersRules:327
```jam
UseArchHeaders <architecture>
```

**Purpose**: Uses architecture-specific headers.

### UseArchObjectHeaders
**File**: HeadersRules:340
```jam
UseArchObjectHeaders <sources> : <architecture>
```

**Purpose**: Uses architecture headers for specific objects.

### UsePosixObjectHeaders
**File**: HeadersRules:361
```jam
UsePosixObjectHeaders <sources>
```

**Purpose**: Uses POSIX headers for objects.

### UseLibraryHeaders
**File**: HeadersRules:373
```jam
UseLibraryHeaders <library>
```

**Purpose**: Uses headers from specific library.

### UseLegacyHeaders
**File**: HeadersRules:385
```jam
UseLegacyHeaders
```

**Purpose**: Uses legacy compatibility headers.

### UseLegacyObjectHeaders
**File**: HeadersRules:395
```jam
UseLegacyObjectHeaders <sources>
```

**Purpose**: Uses legacy headers for specific objects.

### UsePrivateKernelHeaders
**File**: HeadersRules:407
```jam
UsePrivateKernelHeaders
```

**Purpose**: Uses private kernel headers.

### UsePrivateSystemHeaders
**File**: HeadersRules:413
```jam
UsePrivateSystemHeaders
```

**Purpose**: Uses private system headers.

### UseBuildFeatureHeaders
**File**: HeadersRules:420
```jam
UseBuildFeatureHeaders <feature> : <attribute>
```

**Purpose**: Uses headers from build features.

### FStandardOSHeaders
**File**: HeadersRules:434
```jam
FStandardOSHeaders
```

**Purpose**: Returns standard OS header paths.

### FStandardHeaders
**File**: HeadersRules:448
```jam
FStandardHeaders <architecture> : <language>
```

**Purpose**: Returns standard headers for architecture and language.

## Configuration Rules

### ConfigObject
**File**: ConfigRules:10
```jam
ConfigObject <tokens>
```

**Purpose**: Creates configuration object for subdirectory.

### SetConfigVar
**File**: ConfigRules:25
```jam
SetConfigVar <configObject> : <variable> : <value> : <scope>
```

**Purpose**: Sets configuration variable.

**Scopes**:
- `global` - affects all subdirectories
- `local` - affects current subdirectory only

### AppendToConfigVar
**File**: ConfigRules:64
```jam
AppendToConfigVar <configObject> : <variable> : <value> : <scope>
```

**Purpose**: Appends to configuration variable.

### ConfigVar
**File**: ConfigRules:86
```jam
ConfigVar <configObject> : <variable>
```

**Purpose**: Gets configuration variable value.

### PrepareSubDirConfigVariables
**File**: ConfigRules:118
```jam
PrepareSubDirConfigVariables <configObject>
```

**Purpose**: Prepares configuration for subdirectory.

### PrepareConfigVariables
**File**: ConfigRules:148
```jam
PrepareConfigVariables <configObject>
```

**Purpose**: Prepares global configuration variables.

## Cross-compilation Setup

### Repository Configuration

The Haiku build system supports cross-compilation through sophisticated repository management:

#### Remote Package Repositories
**File**: `/build/jam/repositories/HaikuPorts/x86_64`
```jam
RemotePackageRepository HaikuPorts
    : x86_64
    : https://eu.hpkg.haiku-os.org/haikuports/master/build-packages
    :
    # architecture "any" packages
    be_book-2008_10_26-7
    ca_root_certificates-2024_07_02-1
    haiku_userguide-2024_09_09-1
    # ... more packages
    :
    # repository architecture packages
    autoconf-2.72-1
    automake-1.16.5-3
    bash-5.2.032-1
    bc-1.07.1-2
    # ... more packages
```

**Key Features**:
- Architecture-specific package lists
- Remote package fetching with URL specification
- Version-specific package management
- "Any" architecture packages for universal content

#### Cross-compilation Toolchain Setup
The build system automatically configures cross-compilation toolchains:

```jam
# Toolchain variables per architecture
TARGET_CC_x86_64 = x86_64-unknown-haiku-gcc ;
TARGET_CC_x86_gcc2 = i586-pc-haiku-gcc ;
TARGET_CC_arm64 = aarch64-unknown-haiku-gcc ;
TARGET_CC_riscv64 = riscv64-unknown-haiku-gcc ;

# Sysroot configuration
TARGET_SYSROOT_x86_64 = $(HAIKU_CROSS_TOOLS_x86_64)/sysroot ;

# Library paths
TARGET_LIBRARY_PATHS_x86_64 = $(TARGET_SYSROOT_x86_64)/boot/system/lib ;
```

#### Bootstrap Repository Setup
**File**: RepositoryRules:431
```jam
BootstrapPackageRepository <repository> : <architecture>
```

**Purpose**: Sets up bootstrap repository for initial cross-compilation.

**Generated Actions**:
```bash
# Download bootstrap packages
wget $(BOOTSTRAP_PACKAGE_URL)/$(package) -O $(BOOTSTRAP_DIR)/$(package)

# Extract bootstrap toolchain
tar -xf $(BOOTSTRAP_DIR)/toolchain.tar.gz -C $(CROSS_TOOLS_DIR)

# Set up sysroot
mkdir -p $(CROSS_TOOLS_DIR)/sysroot
cp -r $(BOOTSTRAP_DIR)/sysroot/* $(CROSS_TOOLS_DIR)/sysroot/
```

### Build Configuration Scripts

#### Configure Script Integration
The build system integrates with the configure script for toolchain setup:

```bash
# configure script sets up cross-compilation environment
./configure --cross-tools-source buildtools \
            --build-cross-tools x86_64 \
            --target-arch x86_64
```

**Generated Configuration**:
```jam
# Set by configure script
HAIKU_CROSS_TOOLS_x86_64 = /path/to/cross-tools-x86_64 ;
TARGET_PACKAGING_ARCH = x86_64 ;
HAIKU_PACKAGING_ARCHS = x86_64 x86_gcc2 ;  # Multi-arch support
```

## Platform Configuration

### Board-Specific Configurations

#### ARM Board Support
**File**: `/build/jam/board/verdex/BoardSetup`
```jam
# Gumstix Verdex board-specific definitions
HAIKU_BOARD_DESCRIPTION = "Gumstix Verdex" ;

# Memory layout configuration
HAIKU_BOARD_LOADER_BASE = 0xa2000000 ;
HAIKU_BOARD_LOADER_ENTRY_RAW = 0xa2000000 ;
HAIKU_BOARD_LOADER_ENTRY_NBSD = 0xa2000008 ;

# CPU-specific flags
HAIKU_CCFLAGS_$(HAIKU_PACKAGING_ARCH) += -mcpu=xscale -D__XSCALE__ ;
HAIKU_C++FLAGS_$(HAIKU_PACKAGING_ARCH) += -mcpu=xscale -D__XSCALE__ ;

# Bootloader configuration
HAIKU_BOARD_LOADER_FAKE_OS = netbsd ;
HAIKU_BOARD_UBOOT_IMAGE = u-boot-verdex-400-r1604.bin ;

# SD card image configuration
HAIKU_BOARD_SDIMAGE_SIZE = 256 ;  # MB
HAIKU_BOARD_SDIMAGE_FAT_SIZE = 32 ;  # MB
```

#### PowerPC Board Support  
**File**: `/build/jam/board/sam460ex/BoardSetup`
```jam
# ACube SAM460ex board-specific definitions
HAIKU_BOARD_DESCRIPTION = "ACube Sam460ex" ;
HAIKU_KERNEL_PLATFORM = u-boot ;

# Memory layout
HAIKU_BOARD_LOADER_BASE = 0x02000000 ;
HAIKU_BOARD_LOADER_ENTRY_LINUX = 0x02000000 ;

# CPU configuration (commented examples)
#HAIKU_KERNEL_PIC_CCFLAGS += -mcpu=440fp -mtune=440fp ;
#HAIKU_KERNEL_C++FLAGS += -mcpu=440fp -mtune=440fp ;
```

### Platform Detection and Setup

#### Platform Compatibility
**File**: HelperRules:157
```jam
SetPlatformCompatibilityFlagVariables <prefix> : <suffix>
```

**Purpose**: Sets up platform compatibility flags.

#### Platform-Specific Rules
```jam
SetPlatformForTarget <target> : <platform>
SetSupportedPlatformsForTarget <target> : <platforms>
SetSubDirSupportedPlatforms <platforms>
IsPlatformSupportedForTarget <target>
```

**Usage Example**:
```jam
# Platform-specific builds
if $(PLATFORM) = host {
    BuildPlatformMain mytool : tool.cpp ;
} else {
    Application MyApp : main.cpp : be ;
}

# Multi-platform support
SetSupportedPlatformsForTarget myapp : haiku beos ;
if [ IsPlatformSupportedForTarget myapp ] {
    Application myapp : main.cpp : be ;
}
```

## Helper and Utility Rules

### FFilter
**File**: HelperRules:30
```jam
FFilter <list> : <excludePattern>
```

**Purpose**: Filters list excluding items matching pattern.

### FGetGrist
**File**: HelperRules:54
```jam
FGetGrist <target>
```

**Purpose**: Extracts grist from target name.

### FSplitString
**File**: HelperRules:68
```jam
FSplitString <string> : <delimiterChar>
```

**Purpose**: Splits string by delimiter character.

### FSplitPath
**File**: HelperRules:82
```jam
FSplitPath <path>
```

**Purpose**: Splits path into components.

### FConditionsHold
**File**: HelperRules:100
```jam
FConditionsHold <conditions> : <predicate>
```

**Purpose**: Checks if conditions satisfy predicate.

### InheritPlatform
**File**: HelperRules:279
```jam
InheritPlatform <children> : <parent>
```

**Purpose**: Makes child targets inherit platform from parent.

### SubDirAsFlags
**File**: HelperRules:294
```jam
SubDirAsFlags <flags>
```

**Purpose**: Sets assembler flags for subdirectory.

## Miscellaneous Utility Rules

### SetupObjectsDir
**File**: MiscRules:1
```jam
SetupObjectsDir
```

**Purpose**: Sets up object file directories for current architecture.

### SetupFeatureObjectsDir
**File**: MiscRules:37
```jam
SetupFeatureObjectsDir <feature>
```

**Purpose**: Sets up object directories for build features.

### MakeLocateCommonPlatform
**File**: MiscRules:70
```jam
MakeLocateCommonPlatform <files> : <subdir>
```

**Purpose**: Locates files in common platform directory.

### MakeLocatePlatform
**File**: MiscRules:77
```jam
MakeLocatePlatform <files> : <subdir>
```

**Purpose**: Locates files in platform-specific directory.

### MakeLocateArch
**File**: MiscRules:94
```jam
MakeLocateArch <files> : <subdir>
```

**Purpose**: Locates files in architecture-specific directory.

### MakeLocateDebug
**File**: MiscRules:111
```jam
MakeLocateDebug <files> : <subdir>
```

**Purpose**: Locates files in debug/release specific directory.

### DeferredSubInclude
**File**: MiscRules:136
```jam
DeferredSubInclude <params> : <jamfile> : <scope>
```

**Purpose**: Defers subdirectory inclusion until later.

### ExecuteDeferredSubIncludes
**File**: MiscRules:154
```jam
ExecuteDeferredSubIncludes
```

**Purpose**: Executes all deferred subdirectory inclusions.

### HaikuSubInclude
**File**: MiscRules:179
```jam
HaikuSubInclude <tokens>
```

**Purpose**: Haiku-specific subdirectory inclusion.

### NextID
**File**: MiscRules:199
```jam
NextID
```

**Purpose**: Generates unique ID for targets.

### NewUniqueTarget
**File**: MiscRules:208
```jam
NewUniqueTarget <basename>
```

**Purpose**: Creates unique target name.

### RunCommandLine
**File**: MiscRules:220
```jam
RunCommandLine <commandLine>
```

**Purpose**: Executes command line with proper environment.

### DefineBuildProfile
**File**: MiscRules:277
```jam
DefineBuildProfile <name> : <type> : <path>
```

**Purpose**: Defines custom build profile.

**Build Profile Types**:
- `release` - Optimized release build
- `debug` - Debug build with symbols
- `profile` - Profiling enabled build

## Math Utility Rules

### AddNumAbs
**File**: MathRules:31
```jam
AddNumAbs <a> : <b>
```

**Purpose**: Performs absolute value addition.

**Usage Example**:
```jam
local result = [ AddNumAbs -5 : 3 ] ;  # Returns 8
```

## CD and Media Rules

### BuildHaikuCD
**File**: CDRules:1
```jam
BuildHaikuCD <haikuCD> : <bootFloppy> : <scripts>
```

**Purpose**: Builds bootable Haiku CD/DVD images.

**Generated Commands**:
```bash
# Create ISO 9660 image with El Torito boot
mkisofs -R -J -V "Haiku" -b $(bootFloppy) -c boot/boot.catalog \
  -o $(haikuCD) $(imageDirectory)

# Make bootable for BIOS
$(HAIKU_ISOHYBRID) $(haikuCD)
```

**Key Features**:
- Creates El Torito bootable CD images
- Supports Rock Ridge and Joliet extensions  
- Hybrid MBR/ISO format for USB boot compatibility
- Custom boot sector integration

## Advanced Build Patterns

### Multi-Architecture Build Example
```jam
# Build for multiple architectures simultaneously
local architectureObject ;
for architectureObject in [ MultiArchSubDirSetup $(HAIKU_PACKAGING_ARCHS) ] {
    on $(architectureObject) {
        local arch = [ on $(architectureObject) return $(TARGET_PACKAGING_ARCH) ] ;
        
        # Architecture-specific compilation
        if $(arch) = x86_gcc2 {
            # Legacy GCC specific settings
            ObjectC++Flags $(sources) : -fno-strict-aliasing ;
        } else {
            # Modern GCC settings
            ObjectC++Flags $(sources) : -fvisibility=hidden ;
        }
        
        SharedLibrary libtranslation.so : $(sources) : be $(libs) ;
    }
}
```

### Conditional Build Feature Usage
```jam
# Complex feature-based conditional building
local libraries = be ;
local defines = ;

if [ FIsBuildFeatureEnabled openssl ] {
    libraries += ssl crypto ;
    defines += HAVE_OPENSSL ;
    UseLibraryHeaders openssl ;
}

if [ FIsBuildFeatureEnabled icu ] {
    libraries += icu ;
    defines += HAVE_ICU ;
    UseLibraryHeaders icu ;
}

if [ FMatchesBuildFeatures "openssl & icu" ] {
    defines += HAVE_FULL_CRYPTO_SUPPORT ;
}

Application SecureApp : main.cpp crypto.cpp : $(libraries) ;
ObjectDefines main.cpp : $(defines) ;
```

### Cross-Platform Repository Management
```jam
# Architecture-specific repository setup
local arch ;
for arch in $(HAIKU_PACKAGING_ARCHS) {
    PackageRepository HaikuPorts-$(arch) : $(arch) : $(packages-$(arch)) ;
    
    if [ IsPackageAvailable gcc : $(arch) ] {
        FetchPackage gcc : $(arch) ;
    }
    
    # Bootstrap repository for cross-compilation
    BootstrapPackageRepository Bootstrap-$(arch) : $(arch) ;
}
```

---

This concludes Part 3 and the complete Haiku Jam Rules Documentation. The three-part documentation covers all 385+ custom Jam rules used in the Haiku build system, providing comprehensive reference for understanding and potentially migrating this sophisticated build environment.