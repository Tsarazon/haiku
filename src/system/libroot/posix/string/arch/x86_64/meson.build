# Haiku libroot POSIX string arch x86_64
# Corresponds to src/system/libroot/posix/string/arch/x86_64/Jamfile

message('Building libroot POSIX string arch x86_64...')

# Architecture detection - must be x86_64
target_arch = get_option('target_arch')
if target_arch != 'x86_64'
    error('This meson.build is only for x86_64 architecture')
endif

# String arch x86_64 sources (from Jamfile MergeObject)
posix_string_arch_x86_64_sources = files([
    'memcpy.cpp',
    'memset.cpp',
])

# Build string arch x86_64 object library
posix_string_arch_x86_64_lib = static_library('posix_string_arch_x86_64',
    posix_string_arch_x86_64_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: UsePrivateSystemHeaders
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    cpp_args: [
        '-fno-builtin',  # From Jamfile: SubDirC++Flags -fno-builtin (Optimizations create infinite recursion otherwise)
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
posix_string_arch_x86_64_dep = declare_dependency(
    link_with: posix_string_arch_x86_64_lib
)

message('libroot POSIX string arch x86_64 configured successfully')