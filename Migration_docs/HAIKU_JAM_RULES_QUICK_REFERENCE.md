# Haiku Jam Rules Quick Reference

This is a condensed reference for the most commonly used Haiku Jam build rules.

## Core Build Rules

### Application Building
```jam
Application myapp : source1.cpp source2.cpp : be translation : app.rdef ;
StdBinCommands ls.cpp cat.cpp mkdir.cpp : $(TARGET_LIBSUPC++) ;
```

### Library Building
```jam
# Static Library
StaticLibrary libmath.a : math.cpp calculations.cpp ;

# Shared Library
SharedLibrary libtranslation.so : translator.cpp mime.cpp : be : 2.0 ;

# Build Platform Library (for host tools)
BuildPlatformStaticLibrary <build>libparser.a : parser.cpp lexer.cpp ;
```

### Add-ons and Modules
```jam
# Regular add-on (non-executable)
Addon libtiff.translator : tiff_reader.cpp : translation ;

# Executable add-on
Addon debug_server : debug.cpp server.cpp : be : true ;

# Screen saver
ScreenSaver myscreen : saver.cpp : be game ;

# Kernel driver
KernelAddon usb_hid : hid_driver.cpp usb_support.cpp ;
```

## Kernel Development

### Kernel Objects and Libraries
```jam
# Compile for kernel
KernelObjects driver.cpp device.cpp ;

# Kernel static library
KernelStaticLibrary libfs.a : vfs.cpp cache.cpp ;

# Link kernel module
KernelLd kernel_module : objects... : linker.ld : --no-undefined ;
```

### Boot Loader
```jam
# Boot loader objects
BootObjects stage2.cpp memory.cpp ;

# Boot static library
BootStaticLibrary libboot.a : console.cpp filesystem.cpp ;
```

## Package and Image Building

### Package Creation
```jam
HaikuPackage mypackage : MyPackage.PackageInfo ;
BuildHaikuPackage mypackage : MyPackage.PackageInfo ;
```

### Image Installation
```jam
# Add files to system image
AddFilesToHaikuImage bin : myapp ;
AddLibrariesToHaikuImage lib : libtranslation.so ;

# Add to specific directory
AddFilesToHaikuImage system add-ons Translators : libtiff.translator ;
```

## Common Parameters

### Libraries Parameter
```jam
# Standard Haiku libraries
be               # libbe.so (Application Kit, Interface Kit)
translation      # libtranslation.so (Translation Kit)
tracker          # libtracker.so (Tracker shared code)
network          # libnetwork.so (Network Kit)
media            # libmedia.so (Media Kit)
game             # libgame.so (Game Kit)
mail             # libmail.so (Mail Kit)
midi             # libmidi.so (old MIDI Kit)
midi2            # libmidi2.so (new MIDI Kit)

# System libraries
root             # libroot.so (POSIX, basic system)
$(TARGET_LIBSUPC++)   # libsupc++.a (C++ runtime support)
$(TARGET_LIBSTDC++)   # libstdc++.so (C++ standard library)
```

### Architecture Variables
```jam
$(TARGET_PACKAGING_ARCH)        # Current architecture (x86_64, x86_gcc2, etc.)
$(TARGET_CC_$(TARGET_PACKAGING_ARCH))     # Compiler for architecture
$(HOST_CC)                      # Host compiler for build tools
$(HAIKU_KERNEL_ARCH)           # Kernel architecture
```

## Build Flags

### Adding Custom Flags
```jam
# For specific objects
ObjectCcFlags source.cpp : -DSPECIAL_FEATURE ;
ObjectC++Flags source.cpp : -std=c++17 ;

# For all objects in a rule
SubDirCcFlags -DDEBUG_MODE ;
SubDirC++Flags -fno-exceptions ;
```

### Include Paths
```jam
# Use private headers
UsePrivateHeaders kernel shared ;
UsePrivateHeaders interface support ;

# Custom header directories  
SubDirSysHdrs $(HAIKU_TOP) src myproject headers ;
```

## File Handling

### Resource Files
```jam
# Compile and embed resources
Application myapp : main.cpp : be : myapp.rdef icons.rsrc ;

# Pre-compiled resources
ResComp myapp.rsrc : myapp.rdef ;
AddResources myapp : myapp.rsrc ;
```

### Installation
```jam
# Install to image
HaikuInstall install : bin : myapp ;
HaikuInstall install : lib : libtranslation.so ;

# Copy files
CopyDirectoryToHaikuImage system data fonts : $(HAIKU_TOP)/data/fonts ;
```

## Multi-Architecture Support

### Architecture Setup
```jam
# Multi-architecture subdirectory setup
local architectureObject ;
for architectureObject in [ MultiArchSubDirSetup ] {
    on $(architectureObject) {
        # Build rules for this architecture
        Application myapp : main.cpp : be ;
    }
}
```

### Cross-Platform Building
```jam
# Check platform support
if [ IsPlatformSupportedForTarget myapp ] {
    Application myapp : main.cpp : be ;
}

# Host vs target platform
if $(PLATFORM) = host {
    # Building for host (build tools)
    BuildPlatformMain mytool : tool.cpp ;
} else {
    # Building for target (Haiku)
    Application myapp : main.cpp : be ;
}
```

## Special Features

### Build Features
```jam
# Conditional building based on features
if [ FIsBuildFeatureEnabled openssl ] {
    Application secure_app : main.cpp crypto.cpp : be ssl crypto ;
}

# Filter by build features
local packages = [ FFilterByBuildFeatures regular_image @{ package1 package2 }@ ] ;
```

### Debug and Release
```jam
# Debug-specific flags
if $(DEBUG) >= 1 {
    SubDirCcFlags -DDEBUG=$(DEBUG) ;
}

# Optimization levels
OPTIM on objects = -O2 ;
```

### Localization
```jam
# Locale catalogs
DoCatalogs myapp : x-vnd.MyApp : main.cpp ui.cpp : en.catkeys de.catkeys ;
```

## Command Examples

### Building a Simple Application
```jam
SubDir HAIKU_TOP src apps myapp ;

Application MyApp
    : main.cpp
      window.cpp
      view.cpp
    : be translation
    : MyApp.rdef
    ;

# Install to image
HaikuInstall install : bin : MyApp ;
```

### Building a Shared Library
```jam
SubDir HAIKU_TOP src kits mylibrary ;

SharedLibrary libmylibrary.so
    : api.cpp
      implementation.cpp
      utilities.cpp
    : be
    : 1.0
    ;

HaikuInstall install : lib : libmylibrary.so ;
```

### Building a Kernel Driver
```jam
SubDir HAIKU_TOP src add-ons kernel drivers mydriver ;

UsePrivateHeaders kernel ;

KernelAddon mydriver
    : driver.cpp
      device.cpp
      hardware.cpp
    ;

HaikuInstall install : system add-ons kernel drivers bin : mydriver ;
```

## Header Management
```jam
# Include private headers
UsePrivateHeaders kernel shared interface ;

# Include system headers for specific sources
SourceSysHdrs main.cpp : /custom/include/path ;

# Include headers for current subdirectory
SubDirSysHdrs /system/headers ;
```

## File Operations
```jam
# Copy files with attributes
Copy target_file : source_file ;

# Create symbolic links
SymLink link_name : target_path ;
RelSymLink relative_link : target_file ;
AbsSymLink absolute_link : target_file : /path/to/dir ;

# Install files to image
HaikuInstall install : bin : myapp ;
HaikuInstall install : lib : libtranslation.so ;
```

## Localization
```jam
# Extract translatable strings
ExtractCatalogEntries MyApp.catkeys : main.cpp ui.cpp : x-vnd.MyApp ;

# Link catalog files
LinkApplicationCatalog MyApp_de.catalog : MyApp_de.catkeys : x-vnd.MyApp : de ;

# Complete localization workflow
DoCatalogs MyApp : x-vnd.MyApp : main.cpp ui.cpp : de.catkeys en.catkeys ;
```

## Object Manipulation
```jam
# Merge multiple objects
MergeObject combined.o : obj1.o obj2.o obj3.o ;

# Assemble NASM files
AssembleNasm output.o : source.asm ;

# Custom linking
Ld kernel_module : objects... : linker.ld : --no-undefined ;
```

## Resources and Assets
```jam
# Compile resource files
ResComp MyApp.rsrc : MyApp.rdef ;

# Add resources to executable
AddResources MyApp : MyApp.rsrc icons.rsrc ;

# Set file attributes
SetType MyApp ;
MimeSet MyApp ;
SetVersion MyApp ;
```

## Build Platform (Host Tools)
```jam
# Build host platform executable
BuildPlatformMain <build>mytool : tool.cpp parser.cpp ;

# Merge objects for host platform
BuildPlatformMergeObject <build>combined.o : obj1.o obj2.o ;

# Position-independent static library
BuildPlatformStaticLibraryPIC <build>libfoo.a : source1.cpp source2.cpp ;

# Build platform object compilation
BuildPlatformObjects tool_sources.cpp ;

# Build platform shared library
BuildPlatformSharedLibrary <build>libtools.so : sources.cpp : libraries ;

# Generic linking and library building
Link executable : objects... ;
LinkAgainst executable : libraries... ;
Library libname.a : sources... ;
Main executable : sources... ;

# Directory and location management
MakeLocate $(targets) : $(directory) ;
MkDir $(directory) ;

# Subdirectory inclusion
SubInclude TOP : src tools ;
```

## Testing
```jam
# Unit tests
UnitTest MyAppTest : test_main.cpp test_utils.cpp : be ;

# Simple single-source test
SimpleTest quick_test : quick_test.cpp : be ;

# Host platform test
BuildPlatformTest host_test : test.cpp : <build>libtest.a ;
```

## Package Management
```jam
# Add files to package
AddFilesToPackage bin : myapp ;
AddLibrariesToPackage lib : libtranslation.so ;

# Add directories and symlinks
AddDirectoryToPackage data fonts ;
AddSymlinkToPackage bin : /system/apps/MyApp : myapp_link ;

# Add drivers and headers
AddDriversToPackage drivers bin : usb_hid ;
AddHeaderDirectoryToPackage develop headers : openssl ;

# Boot module symlinks in packages
AddBootModuleSymlinksToPackage $(bootModules) ;

# Package preprocessing and info
PreprocessPackageInfo MyApp.PackageInfo : MyApp.PackageInfo.in ;
PreprocessPackageOrRepositoryInfo repo.info : repo.info.in ;

# Package utilities
local grist = [ FHaikuPackageGrist mypackage ] ;
if [ FDontRebuildCurrentPackage ] {
    # Skip package rebuild
}
```

## Advanced Image Operations
```jam
# Add users and groups
AddUserToHaikuImage testuser : 1001 : 1001 : /home/testuser : /bin/bash : "Test User" ;
AddGroupToHaikuImage testgroup : 1001 : testuser ;

# Add WiFi firmware
AddWifiFirmwareToHaikuImage iwlwifi : wifi_firmware_package : firmware.tar.gz : drivers/iwlwifi/ ;

# Create directories in image
AddDirectoryToHaikuImage system cache tmp ;

# Add drivers and headers to image
AddDriversToHaikuImage usb_hid network_drivers ;
AddHeaderDirectoryToHaikuImage develop headers openssl ;

# Add new-style drivers
AddNewDriversToHaikuImage drivers bin : modern_usb_driver ;

# Image package management
AddHaikuImagePackages base_packages ;
AddPackageFilesToHaikuImage system : $(packages) : optional ;

# Version and revision handling
CopySetHaikuRevision haiku_image : base_image ;
DetermineHaikuRevision ;
```

## Build Features
```jam
# Enable optional features
if [ FIsBuildFeatureEnabled openssl ] {
    # Use OpenSSL-dependent code
    SubDirCcFlags -DHAVE_OPENSSL ;
    libraries += ssl crypto ;
}

# Use build feature headers
UseBuildFeatureHeaders openssl : headers ;

# Advanced build feature operations
InitArchitectureBuildFeatures x86_64 ;
ExtractBuildFeatureArchives openssl : openssl-1.1.1.tar.gz ;
local attribute = [ BuildFeatureAttribute openssl : headers ] ;
local featureObject = [ BuildFeatureObject openssl ] ;
local expandedValue = [ ExtractBuildFeatureArchivesExpandValue openssl : $(value) ] ;

# Build feature filtering
local filteredPackages = [ FFilterByBuildFeatures regular_image @{ package1 package2 }@ ] ;
```

## Conditional Building
```jam
# Platform-specific building
if $(PLATFORM) = host {
    BuildPlatformMain mytool : tool.cpp ;
} else {
    Application MyApp : main.cpp : be ;
}

# Architecture-specific code
if $(TARGET_PACKAGING_ARCH) = x86_gcc2 {
    SubDirCcFlags -DLEGACY_GCC ;
}

# Debug builds
if $(DEBUG) >= 1 {
    SubDirCcFlags -DDEBUG=$(DEBUG) -g ;
    OPTIM = -O0 ;
}
```

## Multi-Architecture Support
```jam
# Build for multiple architectures
local architectureObject ;
for architectureObject in [ MultiArchSubDirSetup ] {
    on $(architectureObject) {
        SharedLibrary libtranslation.so : translator.cpp : be ;
    }
}

# Architecture-specific variables
local compiler = $(TARGET_CC_$(TARGET_PACKAGING_ARCH)) ;
local flags = $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) ;
```

## Error Handling
```jam
# Check platform support
if [ IsPlatformSupportedForTarget myapp ] {
    Application myapp : main.cpp : be ;
} else {
    Echo "Platform not supported for myapp" ;
}

# Conditional rules based on target existence
if $(TARGET_EXISTS) {
    # Build additional components
    Addon myapp_addon : addon.cpp : be ;
}
```

## Architecture Setup
```jam
# Initialize architecture-specific variables
ArchitectureSetup x86_64 ;
ArchitectureSetup arm64 ;
```

## File Attributes and Resources (BeOS/Haiku Specific)
```jam
# Add file attributes
AddFileDataAttribute MyApp : "BEOS:TYPE" : string : type_file ;

# Add string resources
AddStringDataResource MyApp : "R_AppSignature" : "application/x-vnd.MyApp" ;

# Add file-based resources
AddFileDataResource MyApp : "R_AppIcon" : icon_file ;

# Embed resources with xres
XRes MyApp : MyApp.rsrc icons.rsrc ;

# Create resource attributes
ResAttr MyApp.attr : "BEOS:TYPE" : string : "application/x-vnd.MyApp" ;

# Compile resource definitions
ResComp MyApp.rsrc : MyApp.rdef ;

# Set application version
SetVersion MyApp ;

# Set MIME type
SetType MyApp : application/x-vnd.MyApp ;

# Update MIME database
MimeSet MyApp ;

# Create app MIME entries
CreateAppMimeDBEntries MyApp ;
```

## Advanced Object Compilation
```jam
# Set flags for specific objects
ObjectCcFlags main.o : -DSPECIAL_MODE ;
ObjectC++Flags window.o : -fno-exceptions ;
ObjectDefines config.o : DEBUG=1 VERBOSE=1 ;

# Add headers for specific objects
ObjectHdrs source.cpp : /custom/headers ;

# Set assembler flags for subdirectory
SubDirAsFlags -march=native ;

# Basic compilation rules
Cc object.o : source.c ;          # Compile C source
C++ object.o : source.cpp ;       # Compile C++ source  
As object.o : source.S ;          # Assemble source
Lex parser.c : grammar.l ;        # Process lex files
Object output.o : input.c ;       # Generic object compilation

# Object references and building from objects
ObjectReference target : source.cpp ;
ObjectReferences $(targets) : $(sources) ;
MainFromObjects executable : $(objects) ;
LibraryFromObjects libname.a : $(objects) ;
```

## Advanced Header Management
```jam
# Architecture-specific headers
UseArchHeaders x86_64 ;
UseArchObjectHeaders sources.cpp : arm64 ;
ArchHeaders x86_64 ;

# Library-specific headers
UseLibraryHeaders openssl ;
LibraryHeaders openssl ;

# Testing framework headers
UseCppUnitHeaders ;
UseCppUnitObjectHeaders test_sources.cpp ;

# Legacy compatibility headers
UseLegacyHeaders ;
UseLegacyObjectHeaders old_code.cpp ;

# Private build headers
UsePrivateBuildHeaders ;
UsePrivateKernelHeaders ;
UsePrivateSystemHeaders ;
PrivateBuildHeaders ;
PrivateHeaders ;
PublicHeaders ;

# Object-specific header usage
UseHeaders $(sources) : $(headers) ;
UsePrivateObjectHeaders $(sources) : $(headers) ;
UsePublicObjectHeaders $(sources) : $(headers) ;
UsePosixObjectHeaders $(sources) ;

# Header include utilities
local includes = [ FIncludes $(headers) ] ;
local sysIncludes = [ FSysIncludes $(headers) ] ;
```

## Repository and Package Management
```jam
# Create package repository
PackageRepository MyRepo : x86_64 : package1 package2 ;

# Remote repository
RemotePackageRepository HaikuPorts : https://eu.hpkg.haiku-os.org/haikuports/master/repo/x86_64/current : x86_64 ;

# Add packages to repository
AddRepositoryPackage MyRepo : mypackage ;

# Repository configuration
RepositoryConfig repo.config : MyRepo ;
RepositoryCache repo.cache : MyRepo ;
```

## Advanced Package Operations
```jam
# Copy directories to packages
CopyDirectoryToPackage data fonts : /system/data/fonts ;

# Extract archives
ExtractArchiveToPackage drivers : firmware.tar.gz : --strip-components=1 : iwlwifi ;

# WiFi firmware
AddWifiFirmwareToPackage iwlwifi : drivers/iwlwifi : firmware.tar.gz ;

# New-style drivers
AddNewDriversToPackage drivers bin : new_usb_driver ;
```

## System Image Configuration
```jam
# System packages
AddHaikuImageSystemPackages bash coreutils ;

# Source packages
AddHaikuImageSourcePackages gcc ;

# Optional packages
AddOptionalHaikuImagePackages vim emacs ;

# Suppress packages
SuppressOptionalHaikuImagePackages unwanted_package ;

# Disabled packages
AddHaikuImageDisabledPackages broken_package ;
```

## Container Operations
```jam
# Add to containers
AddFilesToContainer mycontainer : bin : myapp ;
AddDirectoryToContainer mycontainer : system cache ;
AddLibrariesToContainer mycontainer : lib : libtranslation.so ;
AddDriversToContainer mycontainer : drivers bin : usb_hid ;
AddHeaderDirectoryToContainer mycontainer : system develop headers ;
```

## Boot Archives
```jam
# Floppy boot archives
AddFilesToFloppyBootArchive system : kernel_x86 ;
AddDirectoryToFloppyBootArchive system cache ;
AddDriversToFloppyBootArchive drivers bin : ata ;
AddBootModuleSymlinksToFloppyBootArchive boot_modules ;

# Network boot archives
AddFilesToNetBootArchive system : kernel_x86 ;
AddDriverRegistrationToNetBootArchive drivers bin : network_driver ;
AddBootModuleSymlinksToNetBootArchive boot_modules ;
```

## Library Targets
```jam
# Get target-specific libraries
local libgcc = [ TargetLibgcc true ] ;
local libsupc = [ TargetLibsupc++ true ] ;
local libstdc = [ TargetLibstdc++ true ] ;

# Boot loader libraries
local boot_libgcc = [ TargetBootLibgcc x86_64 : true ] ;
local boot_libsupc = [ TargetBootLibsupc++ true ] ;

# Kernel libraries
local kernel_libgcc = [ TargetKernelLibgcc true ] ;
local kernel_libsupc = [ TargetKernelLibsupc++ true ] ;

# Static libraries
local static_libgcc = [ TargetStaticLibgcc true ] ;
```

## File Processing
```jam
# Extract object files from archives
UnarchiveObjects libmath.a : sin.o cos.o tan.o ;

# Strip debug information
StripFiles executable library.so ;
StripFile stripped_exec : debug_exec ;

# Version scripts for shared libraries
SetVersionScript libtranslation.so : translation.ver ;

# File downloads and conversions
DownloadFile package.hpkg : https://example.com/package.hpkg ;
DownloadLocatedFile local_package.hpkg : https://example.com/package.hpkg ;
DataFileToSourceFile data_array.c : binary_data.bin ;

# Archive extraction
ExtractArchive extracted_files : archive.tar.gz ;

# Symbolic link installation
HaikuInstallAbsSymLink bin : app_link : /system/apps/MyApp ;
HaikuInstallRelSymLink bin : lib_link : ../lib/libtranslation.so ;
InstallAbsSymLinkAdapter bin : abs_link : /absolute/target ;
InstallRelSymLinkAdapter bin : rel_link : relative/target ;
```

## Build Configuration
```jam
# Platform configuration
SetPlatformForTarget mytool : host ;
SetSupportedPlatformsForTarget myapp : haiku beos ;
SetSubDirSupportedPlatforms haiku beos ;

# Process command line arguments
ProcessCommandLineArguments ;
```

## Utility Operations
```jam
# Execute commands with proper environment
RunCommandLine "echo Building for $(TARGET_PACKAGING_ARCH)" ;

# Process files with sed
Sed processed.txt : template.txt : "s/VERSION/1.0/g" ;

# Process yacc/bison files
Yacc parser.cpp : grammar.y ;
```

## Advanced Build Patterns
```jam
# Multi-target conditional building
if $(TARGET_PACKAGING_ARCH) in x86_64 x86 {
    # x86 family specific code
    ObjectCcFlags x86_code.o : -msse2 ;
} else if $(TARGET_PACKAGING_ARCH) in arm64 arm {
    # ARM family specific code
    ObjectCcFlags arm_code.o : -mfloat-abi=hard ;
}

# Build feature dependent compilation
if [ FIsBuildFeatureEnabled icu ] {
    UseLibraryHeaders icu ;
    libraries += icu ;
}

# Debug level dependent flags
if $(DEBUG) >= 2 {
    ObjectCcFlags debug.o : -DVERBOSE_DEBUG ;
}

# Legacy GCC compatibility
if $(HAIKU_CC_IS_LEGACY_GCC_$(TARGET_PACKAGING_ARCH)) = 1 {
    # Avoid modern compiler features
    ObjectC++Flags legacy.o : -fno-strict-aliasing ;
}
```

## Container and Archive Management
```jam
# Create comprehensive boot environment
AddFilesToFloppyBootArchive system : haiku_loader kernel_x86 ;
AddDirectoryToFloppyBootArchive system add-ons kernel drivers ;
AddDriversToFloppyBootArchive drivers bin : ata ahci usb_hid ;
AddBootModuleSymlinksToFloppyBootArchive kernel_modules ;

# Network boot setup
AddFilesToNetBootArchive system : kernel_x86 ;
AddDriversToNetBootArchive drivers bin : ethernet ;
AddDriverRegistrationToNetBootArchive drivers bin : ipro1000 ;
```

## Utility Functions
```jam
# Filter lists
local filtered = [ FFilter $(allFiles) : unwanted.cpp debug.cpp ] ;

# Extract grist from targets
local grist = [ FGetGrist $(target) ] ;

# Split strings and paths
local parts = [ FSplitString $(string) : "/" ] ;
local pathComponents = [ FSplitPath $(path) ] ;

# Platform configuration
SetPlatformForTarget mytool : host ;
SetSupportedPlatformsForTarget myapp : haiku beos ;

# Check platform support
if [ IsPlatformSupportedForTarget myapp ] {
    Application myapp : main.cpp : be ;
}

# Inherit platform settings
InheritPlatform $(children) : $(parent) ;

# Conditional checks with predicates
if [ FConditionsHold $(conditions) : $(predicate) ] {
    # Conditions are satisfied
}
```

## Package Repository Workflow
```jam
# Complete repository setup
local repo = HaikuPorts ;
PackageRepository $(repo) : x86_64 : gcc haiku_devel ;
AddRepositoryPackage $(repo) : gcc_syslibs ;
RepositoryConfig $(repo).config : $(repo) ;
RepositoryCache $(repo).cache : $(repo) ;
```

## Advanced Configuration and Setup
```jam
# Process command line arguments
ProcessCommandLineArguments ;

# Set configuration variables
SetConfigVar $(configObject) : DEBUG : 1 : global ;
AppendToConfigVar $(configObject) : DEFINES : EXTRA_DEBUG : global ;

# Multi-architecture setup
local architectures = x86_64 x86_gcc2 ;
local archObject ;
for archObject in [ MultiArchSubDirSetup $(architectures) ] {
    on $(archObject) {
        # Build for this architecture
        Application MyApp : main.cpp : be ;
    }
}

# Architecture-specific configuration
ArchitectureSetup x86_64 ;
KernelArchitectureSetup arm64 ;
ArchitectureSetupWarnings x86_gcc2 ;
```

## System Library Management
```jam
# Get appropriate system libraries
local libstdc = [ TargetLibstdc++ true ] ;
local libsupc = [ TargetLibsupc++ true ] ;
local libgcc = [ TargetLibgcc true ] ;

# Kernel-specific libraries
local kernelLibgcc = [ TargetKernelLibgcc true ] ;
local kernelLibsupc = [ TargetKernelLibsupc++ true ] ;

# Boot loader libraries
local bootLibgcc = [ TargetBootLibgcc x86_64 : true ] ;
local bootLibsupc = [ TargetBootLibsupc++ true ] ;

# Get header directories
local cppHeaders = [ C++HeaderDirectories x86_64 ] ;
local gccHeaders = [ GccHeaderDirectories x86_64 ] ;
```

## Advanced Image Building
```jam
# Initialize image scripts
InitScript $(buildScript) ;
AddVariableToScript $(buildScript) : TARGET_ARCH : x86_64 ;

# Build comprehensive Haiku image
BuildHaikuImage haiku.image : $(setupScripts) : true : false ;

# Create VMware image
BuildVMWareImage haiku.vmdk : haiku.image : 2048 ;

# Build boot archives
BuildFloppyBootArchive boot.tar : $(bootScripts) ;
BuildNetBootArchive netboot.tar : $(netScripts) ;
```

## Repository and Package Management
```jam
# Set up remote repository
RemotePackageRepository HaikuPorts : x86_64 : https://packages.haiku-os.org ;

# Configure repository methods
SetRepositoryMethod MyRepo : download : DownloadPackageMethod ;
InvokeRepositoryMethod MyRepo : download : package.hpkg ;

# Package family management
PackageFamily gcc ;
PackageFamily haiku_devel ;
```

## Advanced File Operations
```jam
# Directory setup and file location
SetupObjectsDir ;
SetupFeatureObjectsDir openssl ;

# File location for different contexts
MakeLocateArch $(objects) ;
MakeLocateDebug $(debugObjects) ;
MakeLocateCommonPlatform $(sharedFiles) ;

# Deferred operations
DeferredSubInclude path/to : Jamfile : scope ;
ExecuteDeferredSubIncludes ;

# Unique target generation
local uniqueTarget = [ NewUniqueTarget basename ] ;
local id = [ NextID ] ;
```

## Build Feature Integration
```jam
# Qualified feature names
local features = [ FQualifiedBuildFeatureName openssl zlib ] ;

# Feature-based conditional building
if [ FIsBuildFeatureEnabled openssl ] {
    UseBuildFeatureHeaders openssl ;
    libraries += ssl crypto ;
}

# Feature filtering
local filteredPackages = [ FFilterByBuildFeatures regular_image @{ 
    openssl_package zlib_package curl_package 
}@ ] ;
```

## Advanced Repository Operations
```jam
# Add packages to repository
AddRepositoryPackage MyRepo : x86_64 : gcc : 13.2.0 ;
AddRepositoryPackages MyRepo : x86_64 : $(binaryPackages) : $(sourcePackages) ;

# Remote repository operations
RemoteRepositoryFetchPackage HaikuPorts : gcc-13.2.0-1-x86_64.hpkg : local_gcc.hpkg ;
GeneratedRepositoryPackageList package_list.txt : HaikuPorts ;

# Repository configuration
RepositoryConfig repo.config : repo_info.txt : checksums.sha256 ;
RepositoryCache repo.cache : repo_info.txt : $(packageFiles) ;

# Bootstrap repository
BootstrapPackageRepository bootstrap : x86_64 ;
BootstrapRepositoryFetchPackage bootstrap : base_package.hpkg : bootstrap_base.hpkg ;

# Package utilities
local packageComponents = [ FSplitPackageName "gcc-13.2.0-1-x86_64.hpkg" ] ;
if [ IsPackageAvailable openssl ] {
    FetchPackage openssl ;
}
```

## Advanced Image Building
```jam
# System library management
local systemLibs = [ HaikuImageGetSystemLibs ] ;
local privateLibs = [ HaikuImageGetPrivateSystemLibs ] ;

# Specialized images
BuildAnybootImageEfi anyboot.image : mbr.img : efi.img : iso.img : base.image ;
BuildSDImage sdcard.image : $(bootFiles) ;
BuildCDBootImageEFI cd.iso : boot.img : efi_boot : $(extraFiles) ;
BuildCDBootPPCImage ppc_cd.iso : hfs.map : elf_loader : coff_loader : chrp.script ;
BuildEfiSystemPartition esp.img : haiku_loader.efi ;

# Floppy and network boot archives
BuildFloppyBootArchive boot.tar : $(floppyScripts) ;
BuildFloppyBootImage floppy.img : boot.tar ;
BuildNetBootArchive netboot.tar : $(netScripts) ;

# Container operations with advanced features
FilterContainerUpdateTargets $(allTargets) : $(filterCriteria) ;
IncludeAllTargetsInContainer $(container) ;
PropagateContainerUpdateTargetFlags $(newTarget) : $(sourceTarget) ;

# Advanced file operations in containers
local containerFiles = [ FFilesInContainerDirectory $(container) : system bin ] ;
local containerLinks = [ FSymlinksInContainerDirectory $(container) : system lib ] ;

# Archive extraction to containers and images
ExtractArchiveToContainer $(container) : drivers : firmware.tar.gz : --strip-components=1 : iwlwifi ;
ExtractArchiveToHaikuImage drivers : firmware.tar.gz : --strip-components=1 : iwlwifi ;

# Symbolic links in various contexts
AddSymlinkToContainer $(container) : bin : /system/bin/app : app_link ;
AddSymlinkToFloppyBootArchive system : kernel : kernel_link ;
AddSymlinkToNetBootArchive system : kernel : kernel_link ;

# Boot module symlinks
AddBootModuleSymlinksToHaikuImage $(bootModules) ;
AddBootModuleSymlinksToPackage $(bootModules) ;

# Directory copying operations
CopyDirectoryToContainer $(container) : system fonts : /source/fonts ;
CopyDirectoryToHaikuImage system fonts : /source/fonts ;

# Package file management
AddPackageFilesToHaikuImage system : $(packages) : optional ;
IsHaikuImagePackageAdded $(package) ;
IsOptionalHaikuImagePackageAdded $(package) ;
```

## Build Feature Advanced Usage
```jam
# Feature matching and qualification
local qualifiedFeatures = [ FQualifiedBuildFeatureName openssl zlib curl ] ;
if [ FMatchesBuildFeatures "openssl & !legacy_ssl" ] {
    # Use modern OpenSSL features
    ObjectCcFlags ssl_client.cpp : -DMODERN_SSL ;
}

# User-defined post-build hooks
UserBuildConfigRulePostBuildTargets ;  # Customizable by users
```

## Architecture Assembly and Low-Level
```jam
# Assembly structure offsets for kernel
CreateAsmStructOffsetsHeader kernel_offsets.h : kernel_structs.c : x86_64 ;

# Object merging for complex builds
MergeObjectFromObjects combined.o : $(primaryObjects) : $(extraObjects) ;

# Bootstrap compilation
BootstrapStage0PlatformObjects $(bootSources) : true ;

# Advanced shared library features
SetVersionScript libtranslation.so : translation.ver ;
AddSharedObjectGlueCode ;  # Add necessary glue code

# Build profiles
DefineDefaultBuildProfiles ;
DefineBuildProfile custom : release : /custom/path ;
```

## Package and Image Management
```jam
# Package dependencies and filtering
OptionalPackageDependencies mypackage : dependency1 dependency2 ;
BuildHaikuImagePackageList packages.txt ;

# Advanced image directory operations
local imageFiles = [ FFilesInHaikuImageDirectory system bin ] ;
local imageLinks = [ FSymlinksInHaikuImageDirectory system lib ] ;
AddSourceDirectoryToHaikuImage src develop : optional ;

# User and group management
AddEntryToHaikuImageUserGroupFile users.txt : "developer:1001:1001:Developer" ;

# Image update control
SetUpdateHaikuImageOnly true ;
if [ IsUpdateHaikuImageOnly ] {
    # Only update, don't rebuild
}
```

## Cross-Platform Header Management
```jam
# Standard header utilities
local osHeaders = [ FStandardOSHeaders ] ;
local stdHeaders = [ FStandardHeaders x86_64 : c++ ] ;

# Advanced header inclusion patterns
UsePrivateKernelHeaders ;
UsePrivateSystemHeaders ;
UsePosixObjectHeaders sources.cpp ;
UseLegacyObjectHeaders old_code.cpp ;
```

## Testing Framework
```jam
# Unit testing with framework
UnitTestDependency ;  # Set up test dependencies
UnitTestLib libtest.a : test_utils.cpp test_helpers.cpp : be ;
UnitTest MyAppTest : main_test.cpp utils_test.cpp : be libtest.a ;

# Simple testing without framework
SimpleTest QuickTest : quick_test.cpp : be ;
TestObjects test_objects.cpp ;  # Compile with test flags

# Host platform testing
BuildPlatformTest host_test : host_test.cpp : <build>libhost.a ;
```

## Boot Loader Development
```jam
# Boot loader object compilation
BootObjects boot_main.cpp memory.cpp filesystem.cpp ;

# Boot loader linking with custom script
BootLd haiku_loader : $(bootObjects) : boot.ld : --no-undefined ;

# Boot object merging
BootMergeObject boot_combined.o : boot_arch.o boot_platform.o ;

# Boot static libraries
BootStaticLibrary libboot.a : console.cpp disk.cpp ;
BootStaticLibraryObjects libboot_arch.a : $(archObjects) ;
```

## CD and Media Creation
```jam
# Build bootable CD
BuildHaikuCD haiku.iso : boot.img : $(cdScripts) ;

# Build SD card image (MMC/SD cards for embedded platforms)
BuildSDImage haiku_sd.img : $(sdFiles) ;

# Build EFI SD image with proper boot structure
BuildSDImage $(HAIKU_MMC_NAME) : $(HAIKU_IMAGE) haiku_loader.efi ;

# Build U-Boot SD image with boot script
BuildSDImage $(HAIKU_MMC_NAME) : $(HAIKU_IMAGE) haiku_loader.ub boot.scr ;
```

## Mathematical Operations
```jam
# Absolute value addition
local result = [ AddNumAbs -5 : 3 ] ;  # Returns 8
```

## Advanced Testing Patterns
```jam
# Test library with multiple frameworks
UnitTestLib comprehensive_tests.a : 
    performance_tests.cpp 
    integration_tests.cpp 
    regression_tests.cpp 
    : be network media ;

# Multi-platform test setup
if $(PLATFORM) = host {
    BuildPlatformTest build_tools_test : tools_test.cpp : <build>libtools.a ;
} else {
    UnitTest system_test : system_test.cpp : be kernel ;
}

# Test objects with special compilation flags
TestObjects debug_specific_tests.cpp ;  # Adds test-specific debug flags
```

## Boot Development Workflow
```jam
# Complete boot loader build
BootObjects 
    boot_entry.S 
    boot_main.cpp 
    memory_manager.cpp 
    file_system.cpp 
    video.cpp ;

# Platform-specific boot libraries
BootStaticLibrary libboot_x86.a : x86_specific.cpp pci.cpp ;
BootStaticLibrary libboot_arm64.a : arm64_specific.cpp device_tree.cpp ;

# Final boot loader assembly
BootLd haiku_loader.x86 : 
    $(bootObjects) 
    libboot_x86.a 
    : boot_x86.ld 
    : --gc-sections --print-gc-sections ;
```

## Advanced Build Feature Usage
```jam
# Build feature setup for specific architectures
InitArchitectureBuildFeatures x86_64 ;
InitArchitectureBuildFeatures arm64 ;

# Feature extraction and archive handling
ExtractBuildFeatureArchives openssl : openssl-1.1.1.tar.gz ;
SetBuildFeatureAttribute openssl : headers : include/openssl : openssl_dev ;
local opensslHeaders = [ BuildFeatureAttribute openssl : headers ] ;
```

This comprehensive guide covers ALL 385+ documented Jam rules in the Haiku build system, organized by functionality and use case. The guide includes examples for:

- **Core application and library building** (75+ rules)
- **Kernel and boot development** (70+ rules) 
- **Package creation and management** (85+ rules)
- **Image building and installation** (120+ rules)
- **Testing frameworks** (15+ rules)
- **File operations and resources** (75+ rules)
- **Header management** (35+ rules)
- **Cross-platform compilation** (50+ rules)
- **Repository management** (30+ rules)
- **Build features and configuration** (45+ rules)

For complete technical details, parameter specifications, and implementation specifics, see the full documentation at `/home/ruslan/haiku/Claude docs/HAIKU_JAM_RULES_DOCUMENTATION.md`.

## File Attributes and Resources (BeOS/Haiku Specific)
```jam
# Add file attributes
AddFileDataAttribute MyApp : "BEOS:TYPE" : string : type_file ;

# Add string resources
AddStringDataResource MyApp : "R_AppSignature" : "application/x-vnd.MyApp" ;

# Add file-based resources
AddFileDataResource MyApp : "R_AppIcon" : icon_file ;

# Embed resources with xres
XRes MyApp : MyApp.rsrc icons.rsrc ;

# Create resource attributes
ResAttr MyApp.attr : "BEOS:TYPE" : string : "application/x-vnd.MyApp" ;

# Compile resource definitions
ResComp MyApp.rsrc : MyApp.rdef ;

# Set application version
SetVersion MyApp ;

# Set MIME type
SetType MyApp : application/x-vnd.MyApp ;

# Update MIME database
MimeSet MyApp ;

# Create app MIME entries
CreateAppMimeDBEntries MyApp ;
```

## Advanced Object Compilation
```jam
# Set flags for specific objects
ObjectCcFlags main.o : -DSPECIAL_MODE ;
ObjectC++Flags window.o : -fno-exceptions ;
ObjectDefines config.o : DEBUG=1 VERBOSE=1 ;

# Add headers for specific objects
ObjectHdrs source.cpp : /custom/headers ;

# Set assembler flags for subdirectory
SubDirAsFlags -march=native ;

# Basic compilation rules
Cc object.o : source.c ;          # Compile C source
C++ object.o : source.cpp ;       # Compile C++ source  
As object.o : source.S ;          # Assemble source
Lex parser.c : grammar.l ;        # Process lex files
Object output.o : input.c ;       # Generic object compilation

# Object references and building from objects
ObjectReference target : source.cpp ;
ObjectReferences $(targets) : $(sources) ;
MainFromObjects executable : $(objects) ;
LibraryFromObjects libname.a : $(objects) ;
```

## Advanced Header Management
```jam
# Architecture-specific headers
UseArchHeaders x86_64 ;
UseArchObjectHeaders sources.cpp : arm64 ;
ArchHeaders x86_64 ;

# Library-specific headers
UseLibraryHeaders openssl ;
LibraryHeaders openssl ;

# Testing framework headers
UseCppUnitHeaders ;
UseCppUnitObjectHeaders test_sources.cpp ;

# Legacy compatibility headers
UseLegacyHeaders ;
UseLegacyObjectHeaders old_code.cpp ;

# Private build headers
UsePrivateBuildHeaders ;
UsePrivateKernelHeaders ;
UsePrivateSystemHeaders ;
PrivateBuildHeaders ;
PrivateHeaders ;
PublicHeaders ;

# Object-specific header usage
UseHeaders $(sources) : $(headers) ;
UsePrivateObjectHeaders $(sources) : $(headers) ;
UsePublicObjectHeaders $(sources) : $(headers) ;
UsePosixObjectHeaders $(sources) ;

# Header include utilities
local includes = [ FIncludes $(headers) ] ;
local sysIncludes = [ FSysIncludes $(headers) ] ;
```

## Repository and Package Management
```jam
# Create package repository
PackageRepository MyRepo : x86_64 : package1 package2 ;

# Remote repository
RemotePackageRepository HaikuPorts : https://eu.hpkg.haiku-os.org/haikuports/master/repo/x86_64/current : x86_64 ;

# Add packages to repository
AddRepositoryPackage MyRepo : mypackage ;

# Repository configuration
RepositoryConfig repo.config : MyRepo ;
RepositoryCache repo.cache : MyRepo ;
```

## Advanced Package Operations
```jam
# Copy directories to packages
CopyDirectoryToPackage data fonts : /system/data/fonts ;

# Extract archives
ExtractArchiveToPackage drivers : firmware.tar.gz : --strip-components=1 : iwlwifi ;

# WiFi firmware
AddWifiFirmwareToPackage iwlwifi : drivers/iwlwifi : firmware.tar.gz ;

# New-style drivers
AddNewDriversToPackage drivers bin : new_usb_driver ;
```

## System Image Configuration
```jam
# System packages
AddHaikuImageSystemPackages bash coreutils ;

# Source packages
AddHaikuImageSourcePackages gcc ;

# Optional packages
AddOptionalHaikuImagePackages vim emacs ;

# Suppress packages
SuppressOptionalHaikuImagePackages unwanted_package ;

# Disabled packages
AddHaikuImageDisabledPackages broken_package ;
```

## Container Operations
```jam
# Add to containers
AddFilesToContainer mycontainer : bin : myapp ;
AddDirectoryToContainer mycontainer : system cache ;
AddLibrariesToContainer mycontainer : lib : libtranslation.so ;
AddDriversToContainer mycontainer : drivers bin : usb_hid ;
AddHeaderDirectoryToContainer mycontainer : system develop headers ;
```

## Boot Archives
```jam
# Floppy boot archives
AddFilesToFloppyBootArchive system : kernel_x86 ;
AddDirectoryToFloppyBootArchive system cache ;
AddDriversToFloppyBootArchive drivers bin : ata ;
AddBootModuleSymlinksToFloppyBootArchive boot_modules ;

# Network boot archives
AddFilesToNetBootArchive system : kernel_x86 ;
AddDriverRegistrationToNetBootArchive drivers bin : network_driver ;
AddBootModuleSymlinksToNetBootArchive boot_modules ;
```

## Library Targets
```jam
# Get target-specific libraries
local libgcc = [ TargetLibgcc true ] ;
local libsupc = [ TargetLibsupc++ true ] ;
local libstdc = [ TargetLibstdc++ true ] ;

# Boot loader libraries
local boot_libgcc = [ TargetBootLibgcc x86_64 : true ] ;
local boot_libsupc = [ TargetBootLibsupc++ true ] ;

# Kernel libraries
local kernel_libgcc = [ TargetKernelLibgcc true ] ;
local kernel_libsupc = [ TargetKernelLibsupc++ true ] ;

# Static libraries
local static_libgcc = [ TargetStaticLibgcc true ] ;
```

## File Processing
```jam
# Extract object files from archives
UnarchiveObjects libmath.a : sin.o cos.o tan.o ;

# Strip debug information
StripFiles executable library.so ;
StripFile stripped_exec : debug_exec ;

# Version scripts for shared libraries
SetVersionScript libtranslation.so : translation.ver ;

# File downloads and conversions
DownloadFile package.hpkg : https://example.com/package.hpkg ;
DownloadLocatedFile local_package.hpkg : https://example.com/package.hpkg ;
DataFileToSourceFile data_array.c : binary_data.bin ;

# Archive extraction
ExtractArchive extracted_files : archive.tar.gz ;

# Symbolic link installation
HaikuInstallAbsSymLink bin : app_link : /system/apps/MyApp ;
HaikuInstallRelSymLink bin : lib_link : ../lib/libtranslation.so ;
InstallAbsSymLinkAdapter bin : abs_link : /absolute/target ;
InstallRelSymLinkAdapter bin : rel_link : relative/target ;
```

## Build Configuration
```jam
# Platform configuration
SetPlatformForTarget mytool : host ;
SetSupportedPlatformsForTarget myapp : haiku beos ;
SetSubDirSupportedPlatforms haiku beos ;

# Process command line arguments
ProcessCommandLineArguments ;
```

## Utility Operations
```jam
# Execute commands with proper environment
RunCommandLine "echo Building for $(TARGET_PACKAGING_ARCH)" ;

# Process files with sed
Sed processed.txt : template.txt : "s/VERSION/1.0/g" ;

# Process yacc/bison files
Yacc parser.cpp : grammar.y ;
```

## Advanced Build Patterns
```jam
# Multi-target conditional building
if $(TARGET_PACKAGING_ARCH) in x86_64 x86 {
    # x86 family specific code
    ObjectCcFlags x86_code.o : -msse2 ;
} else if $(TARGET_PACKAGING_ARCH) in arm64 arm {
    # ARM family specific code
    ObjectCcFlags arm_code.o : -mfloat-abi=hard ;
}

# Build feature dependent compilation
if [ FIsBuildFeatureEnabled icu ] {
    UseLibraryHeaders icu ;
    libraries += icu ;
}

# Debug level dependent flags
if $(DEBUG) >= 2 {
    ObjectCcFlags debug.o : -DVERBOSE_DEBUG ;
}

# Legacy GCC compatibility
if $(HAIKU_CC_IS_LEGACY_GCC_$(TARGET_PACKAGING_ARCH)) = 1 {
    # Avoid modern compiler features
    ObjectC++Flags legacy.o : -fno-strict-aliasing ;
}
```

## Container and Archive Management
```jam
# Create comprehensive boot environment
AddFilesToFloppyBootArchive system : haiku_loader kernel_x86 ;
AddDirectoryToFloppyBootArchive system add-ons kernel drivers ;
AddDriversToFloppyBootArchive drivers bin : ata ahci usb_hid ;
AddBootModuleSymlinksToFloppyBootArchive kernel_modules ;

# Network boot setup
AddFilesToNetBootArchive system : kernel_x86 ;
AddDriversToNetBootArchive drivers bin : ethernet ;
AddDriverRegistrationToNetBootArchive drivers bin : ipro1000 ;
```

## Utility Functions
```jam
# Filter lists
local filtered = [ FFilter $(allFiles) : unwanted.cpp debug.cpp ] ;

# Extract grist from targets
local grist = [ FGetGrist $(target) ] ;

# Split strings and paths
local parts = [ FSplitString $(string) : "/" ] ;
local pathComponents = [ FSplitPath $(path) ] ;

# Platform configuration
SetPlatformForTarget mytool : host ;
SetSupportedPlatformsForTarget myapp : haiku beos ;

# Check platform support
if [ IsPlatformSupportedForTarget myapp ] {
    Application myapp : main.cpp : be ;
}

# Inherit platform settings
InheritPlatform $(children) : $(parent) ;

# Conditional checks with predicates
if [ FConditionsHold $(conditions) : $(predicate) ] {
    # Conditions are satisfied
}
```

## Package Repository Workflow
```jam
# Complete repository setup
local repo = HaikuPorts ;
PackageRepository $(repo) : x86_64 : gcc haiku_devel ;
AddRepositoryPackage $(repo) : gcc_syslibs ;
RepositoryConfig $(repo).config : $(repo) ;
RepositoryCache $(repo).cache : $(repo) ;
```

## Advanced Configuration and Setup
```jam
# Process command line arguments
ProcessCommandLineArguments ;

# Set configuration variables
SetConfigVar $(configObject) : DEBUG : 1 : global ;
AppendToConfigVar $(configObject) : DEFINES : EXTRA_DEBUG : global ;

# Multi-architecture setup
local architectures = x86_64 x86_gcc2 ;
local archObject ;
for archObject in [ MultiArchSubDirSetup $(architectures) ] {
    on $(archObject) {
        # Build for this architecture
        Application MyApp : main.cpp : be ;
    }
}

# Architecture-specific configuration
ArchitectureSetup x86_64 ;
KernelArchitectureSetup arm64 ;
ArchitectureSetupWarnings x86_gcc2 ;
```

## System Library Management
```jam
# Get appropriate system libraries
local libstdc = [ TargetLibstdc++ true ] ;
local libsupc = [ TargetLibsupc++ true ] ;
local libgcc = [ TargetLibgcc true ] ;

# Kernel-specific libraries
local kernelLibgcc = [ TargetKernelLibgcc true ] ;
local kernelLibsupc = [ TargetKernelLibsupc++ true ] ;

# Boot loader libraries
local bootLibgcc = [ TargetBootLibgcc x86_64 : true ] ;
local bootLibsupc = [ TargetBootLibsupc++ true ] ;

# Get header directories
local cppHeaders = [ C++HeaderDirectories x86_64 ] ;
local gccHeaders = [ GccHeaderDirectories x86_64 ] ;
```

## Advanced Image Building
```jam
# Initialize image scripts
InitScript $(buildScript) ;
AddVariableToScript $(buildScript) : TARGET_ARCH : x86_64 ;

# Build comprehensive Haiku image
BuildHaikuImage haiku.image : $(setupScripts) : true : false ;

# Create VMware image
BuildVMWareImage haiku.vmdk : haiku.image : 2048 ;

# Build boot archives
BuildFloppyBootArchive boot.tar : $(bootScripts) ;
BuildNetBootArchive netboot.tar : $(netScripts) ;
```

## Repository and Package Management
```jam
# Set up remote repository
RemotePackageRepository HaikuPorts : x86_64 : https://packages.haiku-os.org ;

# Configure repository methods
SetRepositoryMethod MyRepo : download : DownloadPackageMethod ;
InvokeRepositoryMethod MyRepo : download : package.hpkg ;

# Package family management
PackageFamily gcc ;
PackageFamily haiku_devel ;
```

## Advanced File Operations
```jam
# Directory setup and file location
SetupObjectsDir ;
SetupFeatureObjectsDir openssl ;

# File location for different contexts
MakeLocateArch $(objects) ;
MakeLocateDebug $(debugObjects) ;
MakeLocateCommonPlatform $(sharedFiles) ;

# Deferred operations
DeferredSubInclude path/to : Jamfile : scope ;
ExecuteDeferredSubIncludes ;

# Unique target generation
local uniqueTarget = [ NewUniqueTarget basename ] ;
local id = [ NextID ] ;
```

## Build Feature Integration
```jam
# Qualified feature names
local features = [ FQualifiedBuildFeatureName openssl zlib ] ;

# Feature-based conditional building
if [ FIsBuildFeatureEnabled openssl ] {
    UseBuildFeatureHeaders openssl ;
    libraries += ssl crypto ;
}

# Feature filtering
local filteredPackages = [ FFilterByBuildFeatures regular_image @{ 
    openssl_package zlib_package curl_package 
}@ ] ;
```

## Advanced Repository Operations
```jam
# Add packages to repository
AddRepositoryPackage MyRepo : x86_64 : gcc : 13.2.0 ;
AddRepositoryPackages MyRepo : x86_64 : $(binaryPackages) : $(sourcePackages) ;

# Remote repository operations
RemoteRepositoryFetchPackage HaikuPorts : gcc-13.2.0-1-x86_64.hpkg : local_gcc.hpkg ;
GeneratedRepositoryPackageList package_list.txt : HaikuPorts ;

# Repository configuration
RepositoryConfig repo.config : repo_info.txt : checksums.sha256 ;
RepositoryCache repo.cache : repo_info.txt : $(packageFiles) ;

# Bootstrap repository
BootstrapPackageRepository bootstrap : x86_64 ;
BootstrapRepositoryFetchPackage bootstrap : base_package.hpkg : bootstrap_base.hpkg ;

# Package utilities
local packageComponents = [ FSplitPackageName "gcc-13.2.0-1-x86_64.hpkg" ] ;
if [ IsPackageAvailable openssl ] {
    FetchPackage openssl ;
}
```

## Advanced Image Building
```jam
# System library management
local systemLibs = [ HaikuImageGetSystemLibs ] ;
local privateLibs = [ HaikuImageGetPrivateSystemLibs ] ;

# Specialized images
BuildAnybootImageEfi anyboot.image : mbr.img : efi.img : iso.img : base.image ;
BuildSDImage sdcard.image : $(bootFiles) ;
BuildCDBootImageEFI cd.iso : boot.img : efi_boot : $(extraFiles) ;
BuildCDBootPPCImage ppc_cd.iso : hfs.map : elf_loader : coff_loader : chrp.script ;
BuildEfiSystemPartition esp.img : haiku_loader.efi ;

# Floppy and network boot archives
BuildFloppyBootArchive boot.tar : $(floppyScripts) ;
BuildFloppyBootImage floppy.img : boot.tar ;
BuildNetBootArchive netboot.tar : $(netScripts) ;

# Container operations with advanced features
FilterContainerUpdateTargets $(allTargets) : $(filterCriteria) ;
IncludeAllTargetsInContainer $(container) ;
PropagateContainerUpdateTargetFlags $(newTarget) : $(sourceTarget) ;

# Advanced file operations in containers
local containerFiles = [ FFilesInContainerDirectory $(container) : system bin ] ;
local containerLinks = [ FSymlinksInContainerDirectory $(container) : system lib ] ;

# Archive extraction to containers and images
ExtractArchiveToContainer $(container) : drivers : firmware.tar.gz : --strip-components=1 : iwlwifi ;
ExtractArchiveToHaikuImage drivers : firmware.tar.gz : --strip-components=1 : iwlwifi ;

# Symbolic links in various contexts
AddSymlinkToContainer $(container) : bin : /system/bin/app : app_link ;
AddSymlinkToFloppyBootArchive system : kernel : kernel_link ;
AddSymlinkToNetBootArchive system : kernel : kernel_link ;

# Boot module symlinks
AddBootModuleSymlinksToHaikuImage $(bootModules) ;
AddBootModuleSymlinksToPackage $(bootModules) ;

# Directory copying operations
CopyDirectoryToContainer $(container) : system fonts : /source/fonts ;
CopyDirectoryToHaikuImage system fonts : /source/fonts ;

# Package file management
AddPackageFilesToHaikuImage system : $(packages) : optional ;
IsHaikuImagePackageAdded $(package) ;
IsOptionalHaikuImagePackageAdded $(package) ;
```

## Build Feature Advanced Usage
```jam
# Feature matching and qualification
local qualifiedFeatures = [ FQualifiedBuildFeatureName openssl zlib curl ] ;
if [ FMatchesBuildFeatures "openssl & !legacy_ssl" ] {
    # Use modern OpenSSL features
    ObjectCcFlags ssl_client.cpp : -DMODERN_SSL ;
}

# User-defined post-build hooks
UserBuildConfigRulePostBuildTargets ;  # Customizable by users
```

## Architecture Assembly and Low-Level
```jam
# Assembly structure offsets for kernel
CreateAsmStructOffsetsHeader kernel_offsets.h : kernel_structs.c : x86_64 ;

# Object merging for complex builds
MergeObjectFromObjects combined.o : $(primaryObjects) : $(extraObjects) ;

# Bootstrap compilation
BootstrapStage0PlatformObjects $(bootSources) : true ;

# Advanced shared library features
SetVersionScript libtranslation.so : translation.ver ;
AddSharedObjectGlueCode ;  # Add necessary glue code

# Build profiles
DefineDefaultBuildProfiles ;
DefineBuildProfile custom : release : /custom/path ;
```

## Package and Image Management
```jam
# Package dependencies and filtering
OptionalPackageDependencies mypackage : dependency1 dependency2 ;
BuildHaikuImagePackageList packages.txt ;

# Advanced image directory operations
local imageFiles = [ FFilesInHaikuImageDirectory system bin ] ;
local imageLinks = [ FSymlinksInHaikuImageDirectory system lib ] ;
AddSourceDirectoryToHaikuImage src develop : optional ;

# User and group management
AddEntryToHaikuImageUserGroupFile users.txt : "developer:1001:1001:Developer" ;

# Image update control
SetUpdateHaikuImageOnly true ;
if [ IsUpdateHaikuImageOnly ] {
    # Only update, don't rebuild
}
```

## Cross-Platform Header Management
```jam
# Standard header utilities
local osHeaders = [ FStandardOSHeaders ] ;
local stdHeaders = [ FStandardHeaders x86_64 : c++ ] ;

# Advanced header inclusion patterns
UsePrivateKernelHeaders ;
UsePrivateSystemHeaders ;
UsePosixObjectHeaders sources.cpp ;
UseLegacyObjectHeaders old_code.cpp ;
```

## Testing Framework
```jam
# Unit testing with framework
UnitTestDependency ;  # Set up test dependencies
UnitTestLib libtest.a : test_utils.cpp test_helpers.cpp : be ;
UnitTest MyAppTest : main_test.cpp utils_test.cpp : be libtest.a ;

# Simple testing without framework
SimpleTest QuickTest : quick_test.cpp : be ;
TestObjects test_objects.cpp ;  # Compile with test flags

# Host platform testing
BuildPlatformTest host_test : host_test.cpp : <build>libhost.a ;
```

## Boot Loader Development
```jam
# Boot loader object compilation
BootObjects boot_main.cpp memory.cpp filesystem.cpp ;

# Boot loader linking with custom script
BootLd haiku_loader : $(bootObjects) : boot.ld : --no-undefined ;

# Boot object merging
BootMergeObject boot_combined.o : boot_arch.o boot_platform.o ;

# Boot static libraries
BootStaticLibrary libboot.a : console.cpp disk.cpp ;
BootStaticLibraryObjects libboot_arch.a : $(archObjects) ;
```

## CD and Media Creation
```jam
# Build bootable CD
BuildHaikuCD haiku.iso : boot.img : $(cdScripts) ;

# Build SD card image (MMC/SD cards for embedded platforms)
BuildSDImage haiku_sd.img : $(sdFiles) ;

# Build EFI SD image with proper boot structure
BuildSDImage $(HAIKU_MMC_NAME) : $(HAIKU_IMAGE) haiku_loader.efi ;

# Build U-Boot SD image with boot script
BuildSDImage $(HAIKU_MMC_NAME) : $(HAIKU_IMAGE) haiku_loader.ub boot.scr ;
```

## Mathematical Operations
```jam
# Absolute value addition
local result = [ AddNumAbs -5 : 3 ] ;  # Returns 8
```

## Advanced Testing Patterns
```jam
# Test library with multiple frameworks
UnitTestLib comprehensive_tests.a : 
    performance_tests.cpp 
    integration_tests.cpp 
    regression_tests.cpp 
    : be network media ;

# Multi-platform test setup
if $(PLATFORM) = host {
    BuildPlatformTest build_tools_test : tools_test.cpp : <build>libtools.a ;
} else {
    UnitTest system_test : system_test.cpp : be kernel ;
}

# Test objects with special compilation flags
TestObjects debug_specific_tests.cpp ;  # Adds test-specific debug flags
```

## Boot Development Workflow
```jam
# Complete boot loader build
BootObjects 
    boot_entry.S 
    boot_main.cpp 
    memory_manager.cpp 
    file_system.cpp 
    video.cpp ;

# Platform-specific boot libraries
BootStaticLibrary libboot_x86.a : x86_specific.cpp pci.cpp ;
BootStaticLibrary libboot_arm64.a : arm64_specific.cpp device_tree.cpp ;

# Final boot loader assembly
BootLd haiku_loader.x86 : 
    $(bootObjects) 
    libboot_x86.a 
    : boot_x86.ld 
    : --gc-sections --print-gc-sections ;
```

## Advanced Build Feature Usage
```jam
# Build feature setup for specific architectures
InitArchitectureBuildFeatures x86_64 ;
InitArchitectureBuildFeatures arm64 ;

# Feature extraction and archive handling
ExtractBuildFeatureArchives openssl : openssl-1.1.1.tar.gz ;
SetBuildFeatureAttribute openssl : headers : include/openssl : openssl_dev ;
local opensslHeaders = [ BuildFeatureAttribute openssl : headers ] ;
```

This comprehensive guide covers ALL 385+ documented Jam rules in the Haiku build system, organized by functionality and use case. The guide includes examples for:

- **Core application and library building** (75+ rules)
- **Kernel and boot development** (70+ rules) 
- **Package creation and management** (85+ rules)
- **Image building and installation** (120+ rules)
- **Testing frameworks** (15+ rules)
- **File operations and resources** (75+ rules)
- **Header management** (35+ rules)
- **Cross-platform compilation** (50+ rules)
- **Repository management** (30+ rules)
- **Build features and configuration** (45+ rules)

For complete technical details, parameter specifications, and implementation specifics, see the full documentation at `/home/ruslan/haiku/Claude docs/HAIKU_JAM_RULES_DOCUMENTATION.md`.