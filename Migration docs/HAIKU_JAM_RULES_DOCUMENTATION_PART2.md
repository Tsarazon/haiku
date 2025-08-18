# Haiku Jam Build System Rules Documentation - Part 2: Package & Image Management

## Overview

This is Part 2 of the comprehensive Haiku Jam rules documentation, covering package management, image building, and file operations. 

**Documentation Parts**:
- **Part 1**: Core Rules, Application Building, Libraries, Kernel
- **Part 2**: Package Management, Image Building, File Operations (this document)
- **Part 3**: Advanced Features, Cross-compilation, Platform Configuration

## Table of Contents - Part 2

1. [Package Build Rules](#package-build-rules)
2. [Image Creation Rules](#image-creation-rules)
3. [Container Operations](#container-operations)
4. [File Handling Rules](#file-handling-rules)
5. [Resource and Asset Rules](#resource-and-asset-rules)
6. [Repository Management Rules](#repository-management-rules)
7. [Test Build Rules](#test-build-rules)
8. [Localization Rules](#localization-rules)

## Package Build Rules

### FHaikuPackageGrist
**File**: PackageRules:1
```jam
FHaikuPackageGrist <package>
```

**Purpose**: Returns appropriate grist for Haiku packages.

### HaikuPackage
**File**: PackageRules:8
```jam
HaikuPackage <package>
```

**Purpose**: Declares a Haiku package target.

**Generated Commands**:
```bash
# Package creation with metadata
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
$(HAIKU_PACKAGE_TOOL) create -q -C $(packageDir) package.hpkg
```

### PreprocessPackageInfo
**File**: PackageRules:42
```jam
PreprocessPackageInfo <source> : <directory> : <architecture>
```

**Purpose**: Processes package info templates with architecture-specific substitutions.

**Generated Commands**:
```bash
$(HOST_CC) -E -P -include $(configHeader) \
  -DTARGET_PACKAGING_ARCH=$(architecture) source.in > processed.PackageInfo
```

### PreprocessPackageOrRepositoryInfo
**File**: PackageRules:83
```jam
PreprocessPackageOrRepositoryInfo <target> : <source> : <architecture>
```

**Purpose**: Processes package or repository info with conditional compilation.

### BuildHaikuPackage
**File**: PackageRules:148
```jam
BuildHaikuPackage <package> : <packageInfo>
```

**Purpose**: Builds complete Haiku package with dependencies and metadata.

**Generated Commands**:
```bash
# Create package directory structure
mkdir -p $(packageDir)
# Copy files with attributes
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_COPYATTR)" -d $(sources) $(packageDir)/
# Create final package
$(HAIKU_PACKAGE_TOOL) create -q -C $(packageDir) \
  -I $(packageInfo) package.hpkg
```

### FDontRebuildCurrentPackage
**File**: PackageRules:244
```jam
FDontRebuildCurrentPackage
```

**Purpose**: Checks if current package should skip rebuild.

### AddDirectoryToPackage
**File**: PackageRules:251
```jam
AddDirectoryToPackage <directoryTokens> : <attributeFiles>
```

**Purpose**: Adds directory structure to package.

### AddFilesToPackage
**File**: PackageRules:260
```jam
AddFilesToPackage <directory> : <targets> : <destName>
```

**Purpose**: Adds files to package with optional renaming.

### AddSymlinkToPackage
**File**: PackageRules:269
```jam
AddSymlinkToPackage <directoryTokens> : <linkTarget> : <linkName>
```

**Purpose**: Adds symbolic links to package.

### CopyDirectoryToPackage
**File**: PackageRules:280
```jam
CopyDirectoryToPackage <directoryTokens> : <sourceDirectory>
```

**Purpose**: Copies entire directory tree to package.

### AddHeaderDirectoryToPackage
**File**: PackageRules:291
```jam
AddHeaderDirectoryToPackage <dirTokens> : <dirName> : <flags>
```

**Purpose**: Adds header directories to development packages.

### AddWifiFirmwareToPackage
**File**: PackageRules:300
```jam
AddWifiFirmwareToPackage <driver> : <subDirToExtract> : <archive>
```

**Purpose**: Extracts and adds WiFi firmware to packages.

### ExtractArchiveToPackage
**File**: PackageRules:310
```jam
ExtractArchiveToPackage <dirTokens> : <archiveFile> : <flags> : <extractedSubDir>
```

**Purpose**: Extracts archive contents to package directory.

### AddDriversToPackage
**File**: PackageRules:319
```jam
AddDriversToPackage <relativeDirectoryTokens> : <targets>
```

**Purpose**: Adds kernel drivers to package.

### AddNewDriversToPackage
**File**: PackageRules:328
```jam
AddNewDriversToPackage <relativeDirectoryTokens> : <targets>
```

**Purpose**: Adds new-style kernel drivers to package.

### AddBootModuleSymlinksToPackage
**File**: PackageRules:337
```jam
AddBootModuleSymlinksToPackage <targets>
```

**Purpose**: Adds boot module symbolic links to package.

### AddLibrariesToPackage
**File**: PackageRules:346
```jam
AddLibrariesToPackage <directory> : <libs>
```

**Purpose**: Adds shared libraries to package.

## Image Creation Rules

### FSameTargetWithPrependedGrist
**File**: ImageRules:1
```jam
FSameTargetWithPrependedGrist <target> : <grist>
```

**Purpose**: Utility for managing target grist in image building.

### InitScript
**File**: ImageRules:18
```jam
InitScript <script>
```

**Purpose**: Initializes build script for image creation.

### AddVariableToScript
**File**: ImageRules:44
```jam
AddVariableToScript <script> : <variable> : <value>
```

**Purpose**: Adds shell variable to build script.

**Generated Commands**:
```bash
echo "variable=value" >> script.sh
```

### AddTargetVariableToScript
**File**: ImageRules:74
```jam
AddTargetVariableToScript <script> : <targets> : <variable>
```

**Purpose**: Adds target-specific variables to build script.

### AddDirectoryToContainer
**File**: ImageRules:119
```jam
AddDirectoryToContainer <container> : <directoryTokens> : <attributeFiles>
```

**Purpose**: Adds directory structure to image container.

### FilterContainerUpdateTargets
**File**: ImageRules:152
```jam
FilterContainerUpdateTargets <targets> : <filterVariable>
```

**Purpose**: Filters which targets need container updates.

### IncludeAllTargetsInContainer
**File**: ImageRules:167
```jam
IncludeAllTargetsInContainer <container>
```

**Purpose**: Marks all targets for inclusion in container.

### PropagateContainerUpdateTargetFlags
**File**: ImageRules:179
```jam
PropagateContainerUpdateTargetFlags <toTarget> : <fromTarget>
```

**Purpose**: Propagates container update flags between targets.

### AddFilesToContainer
**File**: ImageRules:191
```jam
AddFilesToContainer <container> : <directoryTokens> : <targets> : <destName>
```

**Purpose**: Adds files to image container with optional renaming.

**Generated Commands**:
```bash
# Copy files with Haiku attributes
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_COPYATTR)" -d $(sources) $(containerDir)/$(destPath)/
```

### FFilesInContainerDirectory
**File**: ImageRules:307
```jam
FFilesInContainerDirectory <container> : <directoryTokens>
```

**Purpose**: Returns list of files in container directory.

### AddSymlinkToContainer
**File**: ImageRules:320
```jam
AddSymlinkToContainer <container> : <directoryTokens> : <linkTarget> : <linkName>
```

**Purpose**: Adds symbolic links to container.

### FSymlinksInContainerDirectory
**File**: ImageRules:345
```jam
FSymlinksInContainerDirectory <container> : <directoryTokens>
```

**Purpose**: Returns symbolic links in container directory.

### CopyDirectoryToContainer
**File**: ImageRules:358
```jam
CopyDirectoryToContainer <container> : <directoryTokens> : <sourceDirectory>
```

**Purpose**: Copies directory tree to container.

### AddHeaderDirectoryToContainer
**File**: ImageRules:399
```jam
AddHeaderDirectoryToContainer <container> : <dirTokens> : <dirName>
```

**Purpose**: Adds header directories to container.

### AddWifiFirmwareToContainer
**File**: ImageRules:416
```jam
AddWifiFirmwareToContainer <container> : <driver> : <package> : <archive> : <extract>
```

**Purpose**: Extracts WiFi firmware to container.

### ExtractArchiveToContainer
**File**: ImageRules:438
```jam
ExtractArchiveToContainer <container> : <directoryTokens> : <archiveFile>
```

**Purpose**: Extracts archive contents to container.

**Generated Commands**:
```bash
tar -xf $(archiveFile) -C $(containerDir)/$(extractPath)/
```

### AddDriversToContainer
**File**: ImageRules:460
```jam
AddDriversToContainer <container> : <relativeDirectoryTokens> : <targets>
```

**Purpose**: Adds kernel drivers to container.

### AddNewDriversToContainer
**File**: ImageRules:511
```jam
AddNewDriversToContainer <container> : <relativeDirectoryTokens>
```

**Purpose**: Adds new-style drivers to container.

### AddBootModuleSymlinksToContainer
**File**: ImageRules:531
```jam
AddBootModuleSymlinksToContainer <container> : <targets>
```

**Purpose**: Adds boot module symlinks to container.

### AddLibrariesToContainer
**File**: ImageRules:579
```jam
AddLibrariesToContainer <container> : <directory> : <libs>
```

**Purpose**: Adds shared libraries to container.

## Container Operations

### BuildHaikuImage
**File**: ImageRules:1294 (complex rule)
```jam
BuildHaikuImage <haikuImage> : <scripts> : <isImage> : <isVMwareImage>
```

**Purpose**: Creates complete Haiku OS images including boot loader, kernel, and filesystem.

**Parameters**:
- `<haikuImage>`: Image file name (e.g., "haiku.image")
- `<scripts>`: Build scripts for image creation
- `<isImage>`: true for image files, false for directories
- `<isVMwareImage>`: true for VMware-specific image generation

**Generated Commands**:
```bash
# Create image file
dd if=/dev/zero of=haiku.image bs=1M count=$(imageSize)
# Format as BFS
$(HAIKU_FORMAT_TOOL) haiku.image "Haiku"
# Mount and populate
mkdir -p $(mountPoint)
$(HAIKU_MOUNT_TOOL) haiku.image $(mountPoint)
# Copy system files
$(scripts) # Execute all setup scripts
# Unmount
$(HAIKU_UNMOUNT_TOOL) $(mountPoint)
```

**Key Features**:
- Manages image vs. directory builds
- Handles VMware-specific image generation
- Coordinates multiple build scripts
- Supports both development and release configurations

### BuildHaikuImagePackageList
**File**: ImageRules:1191
```jam
BuildHaikuImagePackageList <packageList>
```

**Purpose**: Generates list of packages to include in OS images.

**Key Features**:
- Handles build profiles (minimum, regular, development)
- Resolves package dependencies
- Manages optional vs required packages
- Supports different package repositories

## File Handling Rules

### Copy
**File**: FileRules:9
```jam
Copy <target> : <source>
```

**Purpose**: Copies files while preserving Haiku-specific attributes.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_COPYATTR)" -d $(source) $(target)
```

### SymLink
**File**: FileRules:26
```jam
SymLink <target> : <source> : <makeDefaultDependencies>
```

**Purpose**: Creates symbolic links.

**Generated Commands**:
```bash
$(RM) "$(target)" && $(LN) -s "$(source)" "$(target)"
```

### RelSymLink
**File**: FileRules:54
```jam
RelSymLink <link> : <linkTarget> : <makeDefaultDependencies>
```

**Purpose**: Creates relative symbolic links with automatic path calculation.

### AbsSymLink
**File**: FileRules:82
```jam
AbsSymLink <link> : <linkTarget> : <linkDir> : <makeDefaultDependencies>
```

**Purpose**: Creates absolute symbolic links.

### HaikuInstall
**File**: FileRules:119
```jam
HaikuInstall <installAndUninstall> : <dir> : <sources> : <installgrist>
```

**Purpose**: Generic installation rule for Haiku image.

### InstallAbsSymLinkAdapter
**File**: FileRules:168
```jam
InstallAbsSymLinkAdapter <dir> : <sources>
```

**Purpose**: Adapter for installing absolute symbolic links.

### HaikuInstallAbsSymLink
**File**: FileRules:177
```jam
HaikuInstallAbsSymLink <dir> : <sources>
```

**Purpose**: Installs absolute symbolic links to Haiku image.

### InstallRelSymLinkAdapter
**File**: FileRules:185
```jam
InstallRelSymLinkAdapter <dir> : <sources>
```

**Purpose**: Adapter for installing relative symbolic links.

### HaikuInstallRelSymLink
**File**: FileRules:194
```jam
HaikuInstallRelSymLink <dir> : <sources>
```

**Purpose**: Installs relative symbolic links to Haiku image.

### UnarchiveObjects
**File**: FileRules:203
```jam
UnarchiveObjects <archive> : <objects>
```

**Purpose**: Extracts specific object files from archives.

**Generated Commands**:
```bash
$(TARGET_AR_$(TARGET_PACKAGING_ARCH)) x $(archive) $(objects)
```

### ExtractArchive
**File**: FileRules:219
```jam
ExtractArchive <directory> : <entries> : <archiveFile> : <grist>
```

**Purpose**: Extracts archive contents to specified directory.

**Generated Commands**:
```bash
mkdir -p $(directory)
tar -xf $(archiveFile) -C $(directory) $(entries)
```

### ObjectReference
**File**: FileRules:311
```jam
ObjectReference <target> : <source>
```

**Purpose**: Creates object file references for dependency tracking.

### ObjectReferences
**File**: FileRules:326
```jam
ObjectReferences <targets> : <sources>
```

**Purpose**: Creates multiple object references.

### CopySetHaikuRevision
**File**: FileRules:340
```jam
CopySetHaikuRevision <target> : <source>
```

**Purpose**: Copies file and updates Haiku revision information.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_COPYATTR)" -d $(source) $(target)
$(HAIKU_SET_HAIKU_REVISION) $(target)
```

### DetermineHaikuRevision
**File**: FileRules:378
```jam
DetermineHaikuRevision
```

**Purpose**: Determines current Haiku revision from version control.

### DataFileToSourceFile
**File**: FileRules:422
```jam
DataFileToSourceFile <sourceFile> : <dataFile> : <dataVariable> : <sizeVariable>
```

**Purpose**: Converts binary data file to C source file.

**Generated Commands**:
```bash
$(HAIKU_DATA_TO_SOURCE) $(dataFile) $(sourceFile) $(dataVariable) $(sizeVariable)
```

### DownloadLocatedFile
**File**: FileRules:443
```jam
DownloadLocatedFile <target> : <url> : <source>
```

**Purpose**: Downloads file from URL to specific location.

### DownloadFile
**File**: FileRules:481
```jam
DownloadFile <file> : <url> : <source>
```

**Purpose**: Downloads file from URL.

**Generated Commands**:
```bash
wget -O $(file) $(url)
```

### Sed
**File**: FileRules:513
```jam
Sed <target> : <source> : <substitutions> : <targetMap>
```

**Purpose**: Processes files with sed substitutions.

**Generated Commands**:
```bash
sed $(substitutions) $(source) > $(target)
```

### StripFile
**File**: FileRules:601
```jam
StripFile <target> : <source>
```

**Purpose**: Strips debug symbols from binaries.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_COPYATTR)" -d $(source) $(target)
$(TARGET_STRIP_$(TARGET_PACKAGING_ARCH)) $(target)
```

### StripFiles
**File**: FileRules:624
```jam
StripFiles <files>
```

**Purpose**: Strips debug symbols from multiple files in place.

## Resource and Asset Rules

### AddFileDataAttribute
**File**: BeOSRules:3
```jam
AddFileDataAttribute <target> : <attrName> : <attrType> : <dataFile>
```

**Purpose**: Adds BeOS/Haiku file attributes from data files.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HOST_ADDATTR)" -t $(attrType) $(attrName) -f $(dataFile) $(target)
```

### AddStringDataResource
**File**: BeOSRules:46
```jam
AddStringDataResource <target> : <resourceID> : <dataString>
```

**Purpose**: Adds string data as resource to executable.

### AddFileDataResource
**File**: BeOSRules:82
```jam
AddFileDataResource <target> : <resourceID> : <dataFile>
```

**Purpose**: Adds file data as resource to executable.

### XRes
**File**: BeOSRules:122
```jam
XRes <target> : <resourceFiles>
```

**Purpose**: Embeds compiled resources into executables.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_XRES)" -o $(target) $(resourceFiles)
```

### SetVersion
**File**: BeOSRules:138
```jam
SetVersion <target>
```

**Purpose**: Sets version information on executable.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_SETVERSION)" $(target) $(HAIKU_APP_VERSION_ARGS)
```

### SetType
**File**: BeOSRules:152
```jam
SetType <target> : <type>
```

**Purpose**: Sets MIME type on file.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HOST_ADDATTR)" -t mime BEOS:TYPE $(type) $(target)
```

### MimeSet
**File**: BeOSRules:170
```jam
MimeSet <target>
```

**Purpose**: Updates MIME database with file information.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_MIMESET)" -f $(target)
```

### CreateAppMimeDBEntries
**File**: BeOSRules:186
```jam
CreateAppMimeDBEntries <target>
```

**Purpose**: Creates MIME database entries for applications.

### ResComp
**File**: BeOSRules:216
```jam
ResComp <resourceFile> : <sourceFile>
```

**Purpose**: Compiles resource definition files (.rdef) to binary resources (.rsrc).

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_RC)" -I $(HAIKU_CONFIG_HEADERS) -I $(SEARCH_SOURCE) \
  -I $(SUBDIRHDRS) $(HOST_RC_INCLUDE_PATHS) -o $(resourceFile) $(sourceFile)
```

### ResAttr
**File**: BeOSRules:272
```jam
ResAttr <attributeFile> : <resourceFiles> : <deleteAttributeFile>
```

**Purpose**: Creates file attributes from resource files.

## Repository Management Rules

### PackageFamily
**File**: RepositoryRules:7
```jam
PackageFamily <packageBaseName>
```

**Purpose**: Declares a package family for dependency management.

### SetRepositoryMethod
**File**: RepositoryRules:13
```jam
SetRepositoryMethod <repository> : <methodName> : <method>
```

**Purpose**: Sets method for repository operations.

### InvokeRepositoryMethod
**File**: RepositoryRules:18
```jam
InvokeRepositoryMethod <repository> : <methodName> : <arg1> : <arg2> : <arg3> : <arg4>
```

**Purpose**: Invokes repository method with arguments.

### AddRepositoryPackage
**File**: RepositoryRules:33
```jam
AddRepositoryPackage <repository> : <architecture> : <baseName> : <version>
```

**Purpose**: Adds package to repository.

### AddRepositoryPackages
**File**: RepositoryRules:65
```jam
AddRepositoryPackages <repository> : <architecture> : <packages> : <sourcePackages>
```

**Purpose**: Adds multiple packages to repository.

### PackageRepository
**File**: RepositoryRules:90
```jam
PackageRepository <repository> : <architecture> : <anyPackages> : <packages>
```

**Purpose**: Creates package repository with architecture-specific packages.

### RemoteRepositoryPackageFamily
**File**: RepositoryRules:111
```jam
RemoteRepositoryPackageFamily <repository> : <packageBaseName>
```

**Purpose**: Declares remote package family.

### RemoteRepositoryFetchPackage
**File**: RepositoryRules:117
```jam
RemoteRepositoryFetchPackage <repository> : <package> : <fileName>
```

**Purpose**: Fetches package from remote repository.

**Generated Commands**:
```bash
wget -O $(fileName) $(repositoryURL)/$(package)
```

### RemotePackageRepository
**File**: RepositoryRules:134
```jam
RemotePackageRepository <repository> : <architecture> : <repositoryUrl>
```

**Purpose**: Sets up remote package repository.

### GeneratedRepositoryPackageList
**File**: RepositoryRules:216
```jam
GeneratedRepositoryPackageList <target> : <repository>
```

**Purpose**: Generates list of packages in repository.

### RepositoryConfig
**File**: RepositoryRules:244
```jam
RepositoryConfig <repoConfig> : <repoInfo> : <checksumFile>
```

**Purpose**: Creates repository configuration file.

### RepositoryCache
**File**: RepositoryRules:264
```jam
RepositoryCache <repoCache> : <repoInfo> : <packageFiles>
```

**Purpose**: Creates repository cache file.

### BootstrapPackageRepository
**File**: RepositoryRules:431
```jam
BootstrapPackageRepository <repository> : <architecture>
```

**Purpose**: Sets up bootstrap package repository for cross-compilation.

### FSplitPackageName
**File**: RepositoryRules:515
```jam
FSplitPackageName <packageName>
```

**Purpose**: Splits package name into components (name, version, architecture).

### IsPackageAvailable
**File**: RepositoryRules:527
```jam
IsPackageAvailable <packageName> : <flags>
```

**Purpose**: Checks if package is available in repositories.

### FetchPackage
**File**: RepositoryRules:558
```jam
FetchPackage <packageName> : <flags>
```

**Purpose**: Fetches package from repository.

### BuildHaikuPortsSourcePackageDirectory
**File**: RepositoryRules:587
```jam
BuildHaikuPortsSourcePackageDirectory
```

**Purpose**: Builds HaikuPorts source package directory structure.

### BuildHaikuPortsRepositoryConfig
**File**: RepositoryRules:683
```jam
BuildHaikuPortsRepositoryConfig <treePath>
```

**Purpose**: Builds HaikuPorts repository configuration.

### HaikuRepository
**File**: RepositoryRules:716
```jam
HaikuRepository <repository> : <repoInfoTemplate> : <packages>
```

**Purpose**: Creates Haiku system package repository.

## Test Build Rules

### UnitTestDependency
**File**: TestsRules:4
```jam
UnitTestDependency
```

**Purpose**: Sets up dependencies for unit testing framework.

### UnitTestLib
**File**: TestsRules:10
```jam
UnitTestLib <lib> : <sources> : <libraries>
```

**Purpose**: Builds library with unit testing framework integration.

**Generated Commands**:
```bash
$(TARGET_CC_$(TARGET_PACKAGING_ARCH)) $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH)) \
  -DUNIT_TEST_FRAMEWORK -I$(HAIKU_UNIT_TEST_HEADERS) -c source.cpp -o object.o
$(TARGET_AR_$(TARGET_PACKAGING_ARCH)) $(ARFLAGS) library.a objects...
```

### UnitTest
**File**: TestsRules:43
```jam
UnitTest <test> : <sources> : <libraries>
```

**Purpose**: Builds unit test executable.

**Key Features**:
- Links against unit testing framework
- Adds test-specific headers and flags
- Creates runnable test executable

### TestObjects
**File**: TestsRules:73
```jam
TestObjects <sources>
```

**Purpose**: Compiles objects with test-specific flags.

### SimpleTest
**File**: TestsRules:92
```jam
SimpleTest <test> : <sources> : <libraries>
```

**Purpose**: Builds simple test without unit testing framework.

### BuildPlatformTest
**File**: TestsRules:105
```jam
BuildPlatformTest <test> : <sources> : <libraries>
```

**Purpose**: Builds test for host platform.

## Localization Rules

### ExtractCatalogEntries
**File**: LocaleRules:6
```jam
ExtractCatalogEntries <target> : <sources> : <signature> : <regexp>
```

**Purpose**: Extracts translatable strings from source files.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_CATALOG_TOOL)" extract -s $(signature) -r $(regexp) \
  -o $(target) $(sources)
```

### LinkApplicationCatalog
**File**: LocaleRules:83
```jam
LinkApplicationCatalog <target> : <sources> : <signature> : <language>
```

**Purpose**: Links translated catalog files to application.

**Generated Commands**:
```bash
$(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR) \
"$(HAIKU_CATALOG_TOOL)" link -s $(signature) -l $(language) \
  -o $(target) $(sources)
```

### DoCatalogs
**File**: LocaleRules:107
```jam
DoCatalogs <target> : <signature> : <sources> : <sourceLanguage> : <regexp>
```

**Purpose**: Complete localization workflow for application.

**Key Features**:
- Extracts strings from source files
- Processes translation files
- Links catalogs to executable
- Handles multiple languages

### AddCatalogEntryAttribute
**File**: LocaleRules:170
```jam
AddCatalogEntryAttribute <target>
```

**Purpose**: Adds catalog entry attributes to application.

---

This concludes Part 2 of the Haiku Jam Rules Documentation. Continue with Part 3 for Advanced Features, Cross-compilation setup, and Platform Configuration.