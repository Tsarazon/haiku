# Haiku Support Kit - Meson Build Configuration
# Converts from Jamfile MergeObject to Meson equivalent
# This creates support_kit.o as component for libbe.so

# Sources from Jamfile MergeObject (exact match)
support_kit_sources = files([
    'Architecture.cpp',
    'Archivable.cpp',
    'ArchivingManagers.cpp',
    'Base64.cpp',
    'Beep.cpp',
    'BlockCache.cpp',
    'BufferIO.cpp',
    'BufferedDataIO.cpp',
    'ByteOrder.cpp',
    'CompressionAlgorithm.cpp',
    'DataIO.cpp',
    'DataPositionIOWrapper.cpp',
    'DateTime.cpp',
    'Flattenable.cpp',
    'Job.cpp',
    'JobQueue.cpp',
    'List.cpp',
    'Locker.cpp',
    'PointerList.cpp',
    'Referenceable.cpp',
    'StopWatch.cpp',
    'String.cpp',
    'StringList.cpp',
    'Url.cpp',
    'Uuid.cpp',
    'ZlibCompressionAlgorithm.cpp',
    'ZstdCompressionAlgorithm.cpp',
])

# Compile definitions based on Jamfile conditionals + CMakeLists.txt analysis
support_kit_defines = []

# Critical: Add ZSTD_ENABLED (from Jamfile line 18)
support_kit_defines += ['-DZSTD_ENABLED']

# Critical: Add HAIKU_TARGET_PLATFORM_HAIKU (enables IDNA functions in Url.cpp)
support_kit_defines += ['-DHAIKU_TARGET_PLATFORM_HAIKU']

# Build packages headers (matching CMakeLists.txt exactly)
# Package versions from CMakeLists.txt analysis
target_arch = get_option('target_arch')
build_packages_base = '../generated/build_packages'
zlib_package = 'zlib-1.3.1-3-' + target_arch
zstd_package = 'zstd-1.5.6-1-' + target_arch
icu_package = 'icu74-74.1-3-' + target_arch
gcc_package = 'gcc_syslibs_devel-13.3.0_2023_08_10-1-' + target_arch

# Critical missing headers from CMakeLists.txt
build_packages_includes = []
build_pkg_paths = [
    build_packages_base + '/' + zlib_package + '/develop/headers',
    build_packages_base + '/' + zstd_package + '/develop/headers', 
    build_packages_base + '/' + icu_package + '/develop/headers',
    # GCC C++ headers
    build_packages_base + '/' + gcc_package + '/develop/headers/c++',
    build_packages_base + '/' + gcc_package + '/develop/headers/c++/' + target_arch + '-unknown-haiku',
    build_packages_base + '/' + gcc_package + '/develop/headers/c++/backward',
    build_packages_base + '/' + gcc_package + '/develop/headers/c++/ext',
    # GCC built-in headers
    build_packages_base + '/' + gcc_package + '/develop/headers/gcc/include',
    build_packages_base + '/' + gcc_package + '/develop/headers/gcc/include-fixed',
]

foreach pkg_path : build_pkg_paths
    if fs.is_dir(pkg_path)
        build_packages_includes += include_directories(pkg_path)
    endif
endforeach

# Use CORRECT ORDER: build packages headers FIRST (highest precedence), then others
# This matches CMakeLists.txt lines 89-92 then 101-169
support_kit_includes = build_packages_includes + haiku_config.get('get_all_include_dirs')

# Create support_kit.o equivalent to Jamfile MergeObject using proper Meson approach
# Use static_library to compile, then extract and merge object files
support_kit_lib = static_library(
    'support_kit_temp',
    support_kit_sources,
    include_directories: support_kit_includes,
    cpp_args: [
        '-O2',                    # Optimization (matching Jam)
        '-fno-strict-aliasing'    # Standard Haiku flag
    ] + support_kit_defines,
    pic: true,                   # Use Meson's pic instead of manual -fPIC
    build_by_default: false,
    install: false
)

# Create merged object file support_kit.o (equivalent to Jamfile MergeObject)
# Output to generated_meson mirroring Jam structure: /generated_meson/objects/haiku/x86_64/release/kits/support/support_kit.o
support_kit_output_dir = haiku_config['kits_output_dir'] + '/support'
support_kit_merged = custom_target(
    'support_kit.o',
    input: support_kit_lib,
    output: 'support_kit.o',
    command: [
        'sh', '-c',
        'mkdir -p ' + support_kit_output_dir + ' && /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-ld -r -o ' + support_kit_output_dir + '/support_kit.o @INPUT@.p/*.o'
    ],
    build_by_default: true,
    install: false
)

# Export the merged object for libbe.so integration
support_kit_dep = declare_dependency(
    sources: support_kit_merged,
    include_directories: support_kit_includes,
    compile_args: support_kit_defines
)