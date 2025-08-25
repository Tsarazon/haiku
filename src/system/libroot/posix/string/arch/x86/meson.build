# Haiku libroot POSIX string arch x86
# Corresponds to src/system/libroot/posix/string/arch/x86/Jamfile

message('Building libroot POSIX string arch x86...')

# Architecture detection - must be x86
target_arch = get_option('target_arch')
if target_arch != 'x86'
    error('This meson.build is only for x86 architecture')
endif

# String arch x86 sources (from Jamfile MergeObject with MultiArchIfPrimary logic)
# From Jamfile: [ MultiArchIfPrimary generic_memcpy.c generic_memset.c : commpage_string.S : x86_64 ]
# This means: use generic_memcpy.c generic_memset.c if primary arch, otherwise commpage_string.S (but not for x86_64)
posix_string_arch_x86_sources = files([
    '../generic/generic_memcpy.c',  # From SEARCH_SOURCE += [ FDirName $(SUBDIR) $(DOTDOT) generic ]
    '../generic/generic_memset.c',
])

# Build string arch x86 object library
posix_string_arch_x86_lib = static_library('posix_string_arch_x86',
    posix_string_arch_x86_sources,
    
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
posix_string_arch_x86_dep = declare_dependency(
    link_with: posix_string_arch_x86_lib
)

message('libroot POSIX string arch x86 configured successfully')