# Haiku libroot POSIX libstdthreads implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX libstdthreads layer...')

# libstdthreads implementation sources
posix_libstdthreads_sources = files([
    'call_once.c',
    'cnd.c',
    'mtx.c',
    'thrd.c',
    'tss.c',
])

# Build libstdthreads object library
posix_libstdthreads_lib = static_library('posix_libstdthreads',
    posix_libstdthreads_sources,
    
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
posix_libstdthreads_dep = declare_dependency(
    link_with: posix_libstdthreads_lib
)

message('libroot POSIX libstdthreads layer configured successfully')