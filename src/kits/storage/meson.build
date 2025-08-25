# Haiku Storage Kit - Meson Build Configuration
# Converts from Jamfile MergeObject to Meson equivalent
# This creates storage_kit.o as component for libbe.so

# Sources from Jamfile MergeObject (exact match)
storage_kit_sources = files([
    # Main storage kit sources
    'AppFileInfo.cpp',
    'CopyEngine.cpp',
    'Directory.cpp',
    'DriverSettings.cpp',
    'Entry.cpp',
    'EntryList.cpp',
    'EntryOperationEngineBase.cpp',
    'FdIO.cpp',
    'File.cpp',
    'FileDescriptorIO.cpp',
    'FileIO.cpp',
    'FindDirectory.cpp',
    'MergedDirectory.cpp',
    'Mime.cpp',
    'MimeType.cpp',
    'Node.cpp',
    'NodeInfo.cpp',
    'NodeMonitor.cpp',
    'OffsetFile.cpp',
    'Path.cpp',
    'PathFinder.cpp',
    'PathMonitor.cpp',
    'Query.cpp',
    'QueryPredicate.cpp',
    'RemoveEngine.cpp',
    'ResourceFile.cpp',
    'ResourceItem.cpp',
    'Resources.cpp',
    'ResourcesContainer.cpp',
    'ResourceStrings.cpp',
    'Statable.cpp',
    'SymLink.cpp',
    'Volume.cpp',
    'VolumeRoster.cpp',
    'storage_support.cpp',

    # Sniffer sources (from sniffer subdirectory)
    'sniffer/CharStream.cpp',
    'sniffer/Err.cpp',
    'sniffer/DisjList.cpp',
    'sniffer/Pattern.cpp',
    'sniffer/PatternList.cpp',
    'sniffer/Parser.cpp',
    'sniffer/Range.cpp',
    'sniffer/RPattern.cpp',
    'sniffer/RPatternList.cpp',
    'sniffer/Rule.cpp',

    # Disk device API sources (from disk_device subdirectory)
    'disk_device/DiskDevice.cpp',
    'disk_device/DiskDeviceJob.cpp',
    'disk_device/DiskDeviceJobGenerator.cpp',
    'disk_device/DiskDeviceJobQueue.cpp',
    'disk_device/DiskDeviceList.cpp',
    'disk_device/DiskDevicePrivate.cpp',
    'disk_device/DiskDeviceRoster.cpp',
    'disk_device/DiskDeviceTypes.cpp',
    'disk_device/DiskDeviceVisitor.cpp',
    'disk_device/DiskSystem.cpp',
    'disk_device/DiskSystemAddOn.cpp',
    'disk_device/DiskSystemAddOnManager.cpp',
    'disk_device/MutablePartition.cpp',
    'disk_device/Partition.cpp',
    'disk_device/PartitionDelegate.cpp',
    'disk_device/PartitioningInfo.cpp',
    'disk_device/PartitionParameterEditor.cpp',
    'disk_device/PartitionReference.cpp',

    # Disk device job sources (from disk_device/jobs subdirectory)
    'disk_device/jobs/CreateChildJob.cpp',
    'disk_device/jobs/DeleteChildJob.cpp',
    'disk_device/jobs/DefragmentJob.cpp',
    'disk_device/jobs/InitializeJob.cpp',
    'disk_device/jobs/MoveJob.cpp',
    'disk_device/jobs/RepairJob.cpp',
    'disk_device/jobs/ResizeJob.cpp',
    'disk_device/jobs/SetStringJob.cpp',
    'disk_device/jobs/UninitializeJob.cpp',

    # MIME sources (from mime subdirectory - included in main storage_kit.o)
    'mime/AppMetaMimeCreator.cpp',
    'mime/AssociatedTypes.cpp',
    'mime/Database.cpp',
    'mime/DatabaseDirectory.cpp',
    'mime/DatabaseLocation.cpp',
    'mime/database_support.cpp',
    'mime/InstalledTypes.cpp',
    'mime/MimeEntryProcessor.cpp',
    'mime/MimeInfoUpdater.cpp',
    'mime/MimeSniffer.cpp',
    'mime/MimeSnifferAddon.cpp',
    'mime/MimeSnifferAddonManager.cpp',
    'mime/SnifferRules.cpp',
    'mime/Supertype.cpp',
    'mime/SupportingApps.cpp',
    'mime/TextSnifferAddon.cpp',
])

# Compile definitions based on Jamfile conditionals + CMakeLists.txt analysis
storage_kit_defines = []

# Critical: Add ZSTD_ENABLED (missing from our build, present in CMakeLists.txt)
storage_kit_defines += ['-DZSTD_ENABLED']

# Build packages headers (matching CMakeLists.txt exactly)
# Package versions from CMakeLists.txt analysis
target_arch = get_option('target_arch')
build_packages_base = 'generated/build_packages'
zlib_package = 'zlib-1.3.1-3-' + target_arch
zstd_package = 'zstd-1.5.6-1-' + target_arch
icu_package = 'icu74-74.1-3-' + target_arch
gcc_package = 'gcc_syslibs_devel-13.3.0_2023_08_10-1-' + target_arch

# Build packages headers as compiler args (highest precedence)
build_packages_args = []
build_packages_abs = meson.project_source_root() + '/generated/build_packages'
build_pkg_abs_paths = [
    build_packages_abs + '/' + zlib_package + '/develop/headers',
    build_packages_abs + '/' + zstd_package + '/develop/headers', 
    build_packages_abs + '/' + icu_package + '/develop/headers',
]

foreach pkg_path : build_pkg_abs_paths
    if fs.is_dir(pkg_path)
        build_packages_args += ['-I' + pkg_path]
    endif
endforeach

# Use CORRECT ORDER: build packages headers FIRST (highest precedence), then others
# This matches CMakeLists.txt lines 88-92 then 100-168
# Add subdirectory include paths for storage kit (matching Jamfile SEARCH_SOURCE)
storage_kit_local_includes = [
    include_directories('disk_device'),
    include_directories('disk_device/jobs'),
    include_directories('mime'),
    include_directories('sniffer'),
]
storage_kit_includes = haiku_config.get('get_all_include_dirs') + storage_kit_local_includes

# Create storage_kit.o equivalent to Jamfile MergeObject using proper Meson approach
# Use static_library to compile, then extract and merge object files
storage_kit_lib = static_library(
    'storage_kit_temp',
    storage_kit_sources,
    include_directories: storage_kit_includes,
    cpp_args: build_packages_args + [
        '-O2',                           # Optimization (matching Jam)
        '-fno-strict-aliasing',          # Standard Haiku flag
        # Note: -fno-implicit-templates removed for shared library linking
        # The flag works for static builds but breaks template instantiation for .so
    ] + storage_kit_defines,
    pic: true,                          # Use Meson's pic instead of manual -fPIC
    build_by_default: false,
    install: false
)

# Create merged object file storage_kit.o (equivalent to Jamfile MergeObject)
storage_kit_merged = custom_target(
    'storage_kit.o',
    input: storage_kit_lib,
    output: 'storage_kit.o',
    command: [
        'sh', '-c',
        '/home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-ld -r -o "$1" "$2".p/*.o',
        'sh', '@OUTPUT@', '@INPUT@'
    ],
    build_by_default: true,
    install: false
)

# Export the merged object for libbe.so integration
storage_kit_dep = declare_dependency(
    sources: storage_kit_merged,
    include_directories: storage_kit_includes,
    compile_args: storage_kit_defines
)

# Build MIME library
subdir('mime')