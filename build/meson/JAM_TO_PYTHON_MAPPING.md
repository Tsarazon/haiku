# Complete JAM Rules to Python Module Mapping with Import Locations

| JAM Rule | Import From |
|----------|-------------|
| **ArchitectureRules** | |
| ArchitectureSetup | `from HaikuCommon import *` or `from ArchitectureRules import ArchitectureConfig` |
| KernelArchitectureSetup | `from HaikuCommon import *` or `from ArchitectureRules import ArchitectureConfig` |
| ArchitectureSetupWarnings | `from HaikuCommon import *` or `from ArchitectureRules import ArchitectureConfig` |
| MultiArchIfPrimary | `from HaikuCommon import *` or `from ArchitectureRules import ArchitectureConfig` |
| MultiArchConditionalGristFiles | `from HaikuCommon import *` or `from ArchitectureRules import ArchitectureConfig` |
| MultiArchDefaultGristFiles | `from HaikuCommon import *` or `from ArchitectureRules import ArchitectureConfig` |
| MultiArchSubDirSetup | `from HaikuCommon import *` or `from ArchitectureRules import ArchitectureConfig` |
| **BeOSRules** | |
| AddFileDataAttribute | `from HaikuCommon import *` or `from BeOSRules import HaikuBeOSRules` |
| AddStringDataResource | `from HaikuCommon import *` or `from BeOSRules import HaikuBeOSRules` |
| AddFileDataResource | `from HaikuCommon import *` or `from BeOSRules import HaikuBeOSRules` |
| XRes | `from HaikuCommon import *` or `from BeOSRules import HaikuBeOSRules` |
| SetVersion | `from HaikuCommon import *` or `from BeOSRules import HaikuBeOSRules` |
| SetType | `from HaikuCommon import *` or `from BeOSRules import HaikuBeOSRules` |
| MimeSet | `from HaikuCommon import *` or `from BeOSRules import HaikuBeOSRules` |
| CreateAppMimeDBEntries | `from HaikuCommon import *` or `from BeOSRules import HaikuBeOSRules` |
| ResComp | `from HaikuCommon import *` or `from BeOSRules import HaikuBeOSRules` |
| ResAttr | `from HaikuCommon import *` or `from BeOSRules import HaikuBeOSRules` |
| **BootRules** | |
| MultiBootSubDirSetup | `from BootRules import HaikuBootRules` |
| MultiBootGristFiles | `from BootRules import HaikuBootRules` |
| SetupBoot | `from BootRules import HaikuBootRules` |
| BootObjects | `from BootRules import HaikuBootRules` |
| BootLd | `from BootRules import HaikuBootRules` |
| BootMergeObject | `from BootRules import HaikuBootRules` |
| BootStaticLibrary | `from BootRules import HaikuBootRules` |
| BootStaticLibraryObjects | `from BootRules import HaikuBootRules` |
| BuildMBR | `from BootRules import HaikuBootRules` |
| **BuildFeatureRules** | |
| FQualifiedBuildFeatureName | `from BuildFeatures import FQualifiedBuildFeatureName` |
| FIsBuildFeatureEnabled | `from BuildFeatures import FIsBuildFeatureEnabled` |
| FMatchesBuildFeatures | `from BuildFeatures import FMatchesBuildFeatures` |
| FFilterByBuildFeatures | `from BuildFeatures import FFilterByBuildFeatures` |
| EnableBuildFeatures | `from BuildFeatures import EnableBuildFeatures` |
| BuildFeatureObject | `from BuildFeatures import BuildFeatureObject` |
| SetBuildFeatureAttribute | `from BuildFeatures import SetBuildFeatureAttribute` |
| BuildFeatureAttribute | `from BuildFeatures import BuildFeatureAttribute` |
| ExtractBuildFeatureArchivesExpandValue | `from BuildFeatures import ExtractBuildFeatureArchivesExpandValue` |
| ExtractBuildFeatureArchives | `from BuildFeatures import ExtractBuildFeatureArchives` |
| InitArchitectureBuildFeatures | `from BuildFeatures import InitArchitectureBuildFeatures` |
| **CDRules** | |
| BuildHaikuCD | *(not created yet)* |
| **ConfigRules** | |
| ConfigObject | `from HaikuCommon import *` or `from ConfigRules import HaikuConfigRules` |
| SetConfigVar | `from HaikuCommon import *` or `from ConfigRules import HaikuConfigRules` |
| AppendToConfigVar | `from HaikuCommon import *` or `from ConfigRules import HaikuConfigRules` |
| ConfigVar | `from HaikuCommon import *` or `from ConfigRules import HaikuConfigRules` |
| PrepareSubDirConfigVariables | `from HaikuCommon import *` or `from ConfigRules import HaikuConfigRules` |
| PrepareConfigVariables | `from HaikuCommon import *` or `from ConfigRules import HaikuConfigRules` |
| **FileRules** | |
| Copy | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| SymLink | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| RelSymLink | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| AbsSymLink | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| HaikuInstall | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| InstallAbsSymLinkAdapter | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| HaikuInstallAbsSymLink | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| InstallRelSymLinkAdapter | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| HaikuInstallRelSymLink | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| UnarchiveObjects | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| ExtractArchive | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| ObjectReference | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| ObjectReferences | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| CopySetHaikuRevision | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| DetermineHaikuRevision | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| DataFileToSourceFile | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| DownloadLocatedFile | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| DownloadFile | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| Sed | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| StripFile | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| StripFiles | `from HaikuCommon import *` or `from FileRules import HaikuFileRules` |
| **HeadersRules** | |
| FIncludes | `from HeadersRules import HaikuHeadersRules` |
| FSysIncludes | `from HeadersRules import HaikuHeadersRules` |
| SubDirSysHdrs | `from HeadersRules import HaikuHeadersRules` |
| ObjectSysHdrs | `from HeadersRules import HaikuHeadersRules` |
| SourceHdrs | `from HeadersRules import HaikuHeadersRules` |
| SourceSysHdrs | `from HeadersRules import HaikuHeadersRules` |
| PublicHeaders | `from HeadersRules import HaikuHeadersRules` |
| PrivateHeaders | `from HeadersRules import HaikuHeadersRules` |
| PrivateBuildHeaders | `from HeadersRules import HaikuHeadersRules` |
| LibraryHeaders | `from HeadersRules import HaikuHeadersRules` |
| ArchHeaders | `from HeadersRules import HaikuHeadersRules` |
| UseHeaders | `from HeadersRules import HaikuHeadersRules` |
| UsePublicHeaders | `from HeadersRules import HaikuHeadersRules` |
| UsePublicObjectHeaders | `from HeadersRules import HaikuHeadersRules` |
| UsePrivateHeaders | `from HeadersRules import HaikuHeadersRules` |
| UsePrivateObjectHeaders | `from HeadersRules import HaikuHeadersRules` |
| UsePrivateBuildHeaders | `from HeadersRules import HaikuHeadersRules` |
| UseCppUnitHeaders | `from HeadersRules import HaikuHeadersRules` |
| UseCppUnitObjectHeaders | `from HeadersRules import HaikuHeadersRules` |
| UseArchHeaders | `from HeadersRules import HaikuHeadersRules` |
| UseArchObjectHeaders | `from HeadersRules import HaikuHeadersRules` |
| UsePosixObjectHeaders | `from HeadersRules import HaikuHeadersRules` |
| UseLibraryHeaders | `from HeadersRules import HaikuHeadersRules` |
| UseLegacyHeaders | `from HeadersRules import HaikuHeadersRules` |
| UseLegacyObjectHeaders | `from HeadersRules import HaikuHeadersRules` |
| UsePrivateKernelHeaders | `from HeadersRules import HaikuHeadersRules` |
| UsePrivateSystemHeaders | `from HeadersRules import HaikuHeadersRules` |
| UseBuildFeatureHeaders | `from HeadersRules import HaikuHeadersRules` |
| FStandardOSHeaders | `from HeadersRules import HaikuHeadersRules` |
| FStandardHeaders | `from HeadersRules import HaikuHeadersRules` |
| **HelperRules** | |
| FFilter | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| FGetGrist | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| FSplitString | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| FSplitPath | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| FConditionsHold | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| SetPlatformCompatibilityFlagVariables | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| SetIncludePropertiesVariables | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| SetPlatformForTarget | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| SetSubDirPlatform | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| SetSupportedPlatformsForTarget | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| SetSubDirSupportedPlatforms | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| AddSubDirSupportedPlatforms | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| IsPlatformSupportedForTarget | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| InheritPlatform | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| SubDirAsFlags | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| FDirName | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| FGristFiles | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| FReverse | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| FSum | `from HaikuCommon import *` or `from HelperRules import HaikuHelperRules` |
| **ImageRules** | |
| FSameTargetWithPrependedGrist | `from ImageRules import HaikuImageRules` |
| InitScript | `from ImageRules import HaikuImageRules` |
| AddVariableToScript | `from ImageRules import HaikuImageRules` |
| AddTargetVariableToScript | `from ImageRules import HaikuImageRules` |
| AddDirectoryToContainer | `from ImageRules import HaikuImageRules` |
| AddFilesToContainer | `from ImageRules import HaikuImageRules` |
| AddSymlinkToContainer | `from ImageRules import HaikuImageRules` |
| AddDriversToContainer | `from ImageRules import HaikuImageRules` |
| AddDriverRegistrationToContainer | `from ImageRules import HaikuImageRules` |
| AddBootModuleSymlinksToContainer | `from ImageRules import HaikuImageRules` |
| AddPackageFilesToContainer | `from ImageRules import HaikuImageRules` |
| ExtractFilesFromContainer | `from ImageRules import HaikuImageRules` |
| CreateContainerMakeDirectoriesScript | `from ImageRules import HaikuImageRules` |
| CreateContainerCopyFilesScript | `from ImageRules import HaikuImageRules` |
| CreateContainerExtractFilesScript | `from ImageRules import HaikuImageRules` |
| CreateContainerAttributeScript | `from ImageRules import HaikuImageRules` |
| CreateContainerPlaceBootLoader | `from ImageRules import HaikuImageRules` |
| CreateContainerUpdateScript | `from ImageRules import HaikuImageRules` |
| MakefileEngine | `from ImageRules import HaikuImageRules` |
| BuildHaikuImage | `from ImageRules import HaikuImageRules` |
| BuildFloppyBootImage | `from ImageRules import HaikuImageRules` |
| BuildCDBootImage | `from ImageRules import HaikuImageRules` |
| BuildCDBootPPCImage | `from ImageRules import HaikuImageRules` |
| BuildAnybootImage | `from ImageRules import HaikuImageRules` |
| BuildAnybootMBR | `from ImageRules import HaikuImageRules` |
| BuildHaikuImagePackageList | `from ImageRules import HaikuImageRules` |
| **KernelRules** | |
| SetupKernel | `from KernelRules import HaikuKernelRules` |
| KernelObjects | `from KernelRules import HaikuKernelRules` |
| KernelLd | `from KernelRules import HaikuKernelRules` |
| KernelSo | `from KernelRules import HaikuKernelRules` |
| KernelAddon | `from KernelRules import HaikuKernelRules` |
| KernelMergeObject | `from KernelRules import HaikuKernelRules` |
| KernelStaticLibrary | `from KernelRules import HaikuKernelRules` |
| KernelStaticLibraryObjects | `from KernelRules import HaikuKernelRules` |
| **LocaleRules** | |
| ExtractCatalogEntries | `from LocaleRules import HaikuLocaleRules` |
| LinkApplicationCatalog | `from LocaleRules import HaikuLocaleRules` |
| DoCatalogs | `from LocaleRules import HaikuLocaleRules` |
| AddCatalogEntryAttribute | `from LocaleRules import HaikuLocaleRules` |
| **MainBuildRules** | |
| Application | `from MainBuildRules import HaikuBuildRules` |
| App | `from MainBuildRules import HaikuBuildRules` |
| BinCommand | `from MainBuildRules import HaikuBuildRules` |
| StdBinCommands | `from MainBuildRules import HaikuBuildRules` |
| Preference | `from MainBuildRules import HaikuBuildRules` |
| Server | `from MainBuildRules import HaikuBuildRules` |
| Addon | `from MainBuildRules import HaikuBuildRules` |
| Translator | `from MainBuildRules import HaikuBuildRules` |
| ScreenSaver | `from MainBuildRules import HaikuBuildRules` |
| StaticLibrary | `from MainBuildRules import HaikuBuildRules` |
| StaticLibraryFromObjects | `from MainBuildRules import HaikuBuildRules` |
| SharedLibraryFromObjects | `from MainBuildRules import HaikuBuildRules` |
| SharedLibrary | `from MainBuildRules import HaikuBuildRules` |
| LinkAgainst | `from MainBuildRules import HaikuBuildRules` |
| LinkStaticOSLibs | `from MainBuildRules import HaikuBuildRules` |
| LinkSharedOSLibs | `from MainBuildRules import HaikuBuildRules` |
| AddResources | `from MainBuildRules import HaikuBuildRules` |
| SetVersionScript | `from MainBuildRules import HaikuBuildRules` |
| BuildPlatformMain | `from MainBuildRules import HaikuBuildRules` |
| BuildPlatformSharedLibrary | `from MainBuildRules import HaikuBuildRules` |
| BuildPlatformStaticLibrary | `from MainBuildRules import HaikuBuildRules` |
| BuildPlatformStaticLibraryFromObjects | `from MainBuildRules import HaikuBuildRules` |
| BuildPlatformMergeObject | `from MainBuildRules import HaikuBuildRules` |
| BuildPlatformMergeObjectFromObjects | `from MainBuildRules import HaikuBuildRules` |
| HostSysIncludes | `from MainBuildRules import HaikuBuildRules` |
| **MathRules** | |
| AddNumAbs | `from HaikuCommon import *` or `from MathRules import HaikuMathRules` |
| **MiscRules** | |
| SetupObjectsDir | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| SetupFeatureObjectsDir | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| MakeLocateCommonPlatform | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| MakeLocatePlatform | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| MakeLocateArch | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| MakeLocateDebug | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| DeferredSubInclude | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| ExecuteDeferredSubIncludes | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| HaikuSubInclude | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| NextID | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| NewUniqueTarget | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| RunCommandLine | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| DefineBuildProfile | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| Echo | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| Exit | `from HaikuCommon import *` or `from MiscRules import HaikuMiscRules` |
| **OverriddenJamRules** | |
| Link | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| Object | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| As | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| Lex | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| Yacc | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| Cc | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| C++ | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| Library | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| LibraryFromObjects | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| Main | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| MainFromObjects | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| MakeLocate | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| MkDir | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| ObjectCcFlags | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| ObjectC++Flags | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| ObjectDefines | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| ObjectHdrs | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| SubInclude | `from OverriddenJamRules import HaikuOverriddenJamRules` |
| **PackageRules** | |
| HaikuPackage | `from PackageRules import HaikuPackageRules` |
| PreprocessPackageInfo | `from PackageRules import HaikuPackageRules` |
| PackageInfoAttribute | `from PackageRules import HaikuPackageRules` |
| PackageVersion | `from PackageRules import HaikuPackageRules` |
| PackageLicense | `from PackageRules import HaikuPackageRules` |
| PackageURL | `from PackageRules import HaikuPackageRules` |
| PackageSourceURL | `from PackageRules import HaikuPackageRules` |
| Provides | `from PackageRules import HaikuPackageRules` |
| Requires | `from PackageRules import HaikuPackageRules` |
| Conflicts | `from PackageRules import HaikuPackageRules` |
| Freshens | `from PackageRules import HaikuPackageRules` |
| Replaces | `from PackageRules import HaikuPackageRules` |
| Supplements | `from PackageRules import HaikuPackageRules` |
| GlobalWritableFile | `from PackageRules import HaikuPackageRules` |
| GlobalSettingsFile | `from PackageRules import HaikuPackageRules` |
| UserSettingsFile | `from PackageRules import HaikuPackageRules` |
| PostInstallScript | `from PackageRules import HaikuPackageRules` |
| PreUninstallScript | `from PackageRules import HaikuPackageRules` |
| RequiresUpdatable | `from PackageRules import HaikuPackageRules` |
| AddPackageFilesToHaikuImage | `from PackageRules import HaikuPackageRules` |
| CopyDirectoryToHaikuImage | `from PackageRules import HaikuPackageRules` |
| AddDirectoryToHaikuImage | `from PackageRules import HaikuPackageRules` |
| AddSymlinkToHaikuImage | `from PackageRules import HaikuPackageRules` |
| AddDriversToHaikuImage | `from PackageRules import HaikuPackageRules` |
| AddDriverRegistrationToHaikuImage | `from PackageRules import HaikuPackageRules` |
| AddBootModuleSymlinksToHaikuImage | `from PackageRules import HaikuPackageRules` |
| AddPackagesToHaikuImage | `from PackageRules import HaikuPackageRules` |
| AddOptionalHaikuImagePackages | `from PackageRules import HaikuPackageRules` |
| SuppressOptionalHaikuImagePackages | `from PackageRules import HaikuPackageRules` |
| AddHaikuImageSystemPackages | `from PackageRules import HaikuPackageRules` |
| InstallOptionalHaikuImagePackage | `from PackageRules import HaikuPackageRules` |
| **RepositoryRules** | |
| PackageFamily | `from RepositoryRules import HaikuRepositoryRules` |
| SetRepositoryMethod | `from RepositoryRules import HaikuRepositoryRules` |
| InvokeRepositoryMethod | `from RepositoryRules import HaikuRepositoryRules` |
| AddRepositoryPackage | `from RepositoryRules import HaikuRepositoryRules` |
| AddRepositoryPackages | `from RepositoryRules import HaikuRepositoryRules` |
| PackageRepository | `from RepositoryRules import HaikuRepositoryRules` |
| RemoteRepositoryPackageFamily | `from RepositoryRules import HaikuRepositoryRules` |
| RemoteRepositoryFetchPackage | `from RepositoryRules import HaikuRepositoryRules` |
| RemotePackageRepository | `from RepositoryRules import HaikuRepositoryRules` |
| GeneratedRepositoryPackageList | `from RepositoryRules import HaikuRepositoryRules` |
| RepositoryConfig | `from RepositoryRules import HaikuRepositoryRules` |
| RepositoryCache | `from RepositoryRules import HaikuRepositoryRules` |
| BootstrapRepositoryPackageFamily | `from RepositoryRules import HaikuRepositoryRules` |
| BootstrapRepositoryFetchPackage | `from RepositoryRules import HaikuRepositoryRules` |
| BootstrapPackageRepository | `from RepositoryRules import HaikuRepositoryRules` |
| FSplitPackageName | `from RepositoryRules import HaikuRepositoryRules` |
| IsPackageAvailable | `from RepositoryRules import HaikuRepositoryRules` |
| FetchPackage | `from RepositoryRules import HaikuRepositoryRules` |
| BuildHaikuPortsSourcePackageDirectory | `from RepositoryRules import HaikuRepositoryRules` |
| BuildHaikuPortsRepositoryConfig | `from RepositoryRules import HaikuRepositoryRules` |
| HaikuRepository | `from RepositoryRules import HaikuRepositoryRules` |
| **SystemLibraryRules** | |
| Libstdc++ForImage | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetLibstdc++ | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetLibsupc++ | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetStaticLibsupc++ | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetKernelLibsupc++ | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetBootLibsupc++ | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetLibgcc | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetStaticLibgcc | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetKernelLibgcc | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetBootLibgcc | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetStaticLibgcceh | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| TargetKernelLibgcceh | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| C++HeaderDirectories | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| GccHeaderDirectories | `from HaikuCommon import *` or `from SystemLibraryRules import HaikuSystemLibraryRules` |
| **TestsRules** | |
| UnitTestDependency | `from TestsRules import HaikuTestsRules` |
| UnitTestLib | `from TestsRules import HaikuTestsRules` |
| UnitTest | `from TestsRules import HaikuTestsRules` |
| TestObjects | `from TestsRules import HaikuTestsRules` |
| SimpleTest | `from TestsRules import HaikuTestsRules` |
| BuildPlatformTest | `from TestsRules import HaikuTestsRules` |
| **Additional Python Modules** | |
| *(build feature detection)* | `from detect_build_features import *` |
| *(resource handling)* | `from ResourceHandler import HaikuResourceHandler` |
| *(central orchestration)* | `import HaikuCommon` |
| *(command line processing)* | `from CommandLineArguments import *` |
| *(Haiku packages definition)* | `from HaikuPackages import *` |
| *(optional packages)* | `from OptionalPackages import *` |

## Import Strategy

### Base Modules (Available through HaikuCommon)
Rules from these modules are imported by HaikuCommon and re-exported:
- **HelperRules** - Basic utilities like FDirName, FGristFiles
- **MathRules** - Math operations
- **MiscRules** - Echo, Exit, GLOB, etc.
- **ConfigRules** - Configuration management
- **ArchitectureRules** - Architecture setup
- **FileRules** - File operations
- **BeOSRules** - BeOS compatibility
- **SystemLibraryRules** - System libraries

Use: `from modules.HaikuCommon import *` to get all base functionality.

### Specialized Modules (Import Directly)
These modules should be imported directly when needed:
- **BuildFeatures** - Build feature management (avoid circular deps)
- **PackageRules** - Package creation
- **ImageRules** - Image building
- **KernelRules** - Kernel building
- **BootRules** - Boot loader
- **HeadersRules** - Header management
- **LocaleRules** - Localization
- **MainBuildRules** - Main build rules
- **TestsRules** - Testing framework
- **RepositoryRules** - Repository management
- **OverriddenJamRules** - JAM overrides

### Example Usage in Meson

```python
# In a meson.build file:

# Get base functionality
from modules.HaikuCommon import *

# Get specialized functionality as needed
from modules.BuildFeatures import BuildFeatureAttribute, EnableBuildFeatures
from modules.PackageRules import HaikuPackageRules
from modules.MainBuildRules import HaikuBuildRules

# Use the rules
arch_config = get_architecture_config('x86_64')
build_rules = HaikuBuildRules('x86_64')
```

## Notes
- Rules marked with `from HaikuCommon import *` are available through central orchestration
- Specialized modules must be imported directly to avoid circular dependencies
- BuildFeatures, DefaultBuildProfiles, and BuildSetup have special handling for circular deps
- Use lazy loading (functions) in HaikuCommon for accessing specialized modules