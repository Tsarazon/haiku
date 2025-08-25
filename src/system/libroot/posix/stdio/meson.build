# Haiku libroot POSIX stdio implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX stdio layer...')

# stdio implementation sources
posix_stdio_sources = files([
    '_fseek.c',
    'remove.c',
    'rename.c',
    'tempnam.c',
    'tmpfile.c',
    'tmpnam.c',
])

# Build stdio object library
posix_stdio_lib = static_library('posix_stdio',
    posix_stdio_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
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
posix_stdio_dep = declare_dependency(
    link_with: posix_stdio_lib
)

message('libroot POSIX stdio layer configured successfully')