# Haiku libroot POSIX string arch arm64
# Corresponds to src/system/libroot/posix/string/arch/arm64/Jamfile

message('Building libroot POSIX string arch arm64...')

# Architecture detection - must be arm64
target_arch = get_option('target_arch')
if target_arch != 'arm64'
    error('This meson.build is only for arm64 architecture')
endif

# String arch arm64 sources (from Jamfile MergeObject)
posix_string_arch_arm64_sources = files([
    '../generic/generic_memcpy.c',  # From SEARCH_SOURCE += [ FDirName $(SUBDIR) $(DOTDOT) generic ]
    '../generic/generic_memset.c',
])

# Build string arch arm64 object library
posix_string_arch_arm64_lib = static_library('posix_string_arch_arm64',
    posix_string_arch_arm64_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: UsePrivateSystemHeaders
    ],
    
    c_args: [
        '-fno-builtin',  # From Jamfile: SubDirCcFlags -fno-builtin (Optimizations create infinite recursion otherwise)
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
posix_string_arch_arm64_dep = declare_dependency(
    link_with: posix_string_arch_arm64_lib
)

message('libroot POSIX string arch arm64 configured successfully')