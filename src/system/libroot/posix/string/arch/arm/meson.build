# Haiku libroot POSIX string arch arm
# Corresponds to src/system/libroot/posix/string/arch/arm/Jamfile

message('Building libroot POSIX string arch arm...')

# Architecture detection - must be arm
target_arch = get_option('target_arch')
if target_arch != 'arm'
    error('This meson.build is only for arm architecture')
endif

# String arch arm sources (from Jamfile MergeObject)
posix_string_arch_arm_sources = files([
    'memcpy.S',
    '../generic/generic_memset.c',  # From SEARCH_SOURCE += [ FDirName $(SUBDIR) $(DOTDOT) generic ]
])

# Build string arch arm object library
posix_string_arch_arm_lib = static_library('posix_string_arch_arm',
    posix_string_arch_arm_sources,
    
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
posix_string_arch_arm_dep = declare_dependency(
    link_with: posix_string_arch_arm_lib
)

message('libroot POSIX string arch arm configured successfully')