# Haiku libroot POSIX malloc debug
# Corresponds to src/system/libroot/posix/malloc/debug/Jamfile

message('Building libroot POSIX malloc debug...')

# Architecture detection
target_arch = get_option('target_arch')

# Malloc debug sources (from Jamfile MergeObject)
posix_malloc_debug_sources = files([
    'heap.cpp',
    'guarded_heap.cpp',
    'malloc_debug_api.cpp',
])

# Build malloc debug object library
posix_malloc_debug_lib = static_library('posix_malloc_debug',
    posix_malloc_debug_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: UsePrivateSystemHeaders
        # From Jamfile: UsePrivateHeaders libroot shared runtime_loader
        include_directories('../../../../../headers/private/libroot'),
        include_directories('../../../../../headers/private/shared'),
        include_directories('../../../../../headers/private/runtime_loader'),
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
posix_malloc_debug_dep = declare_dependency(
    link_with: posix_malloc_debug_lib
)

message('libroot POSIX malloc debug configured successfully')