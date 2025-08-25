# Haiku libroot POSIX malloc implementation
# Complete malloc sources matching Jam build

message('Building libroot POSIX malloc layer...')

# Build features integration for malloc
build_features_available = [
    'icu',  # /home/ruslan/haiku/generated/build_packages/icu74-74.1-3-x86_64/develop/headers/unicode
]
message('malloc using build features:', build_features_available)

# malloc implementation sources (matching Jam openbsd and debug builds)
posix_malloc_sources = files([
    # OpenBSD malloc implementation
    'openbsd/malloc.c',
    'openbsd/PagesAllocator.cpp',
    'openbsd/r5_compatibility.c',
    
    # Debug malloc implementation
    'debug/guarded_heap.cpp',
    'debug/heap.cpp', 
    'debug/malloc_debug_api.cpp',
])

# Build malloc object library
posix_malloc_lib = static_library('posix_malloc',
    posix_malloc_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        include_directories('../../../../../headers/compatibility/bsd'),
        include_directories('../../../../../headers/private/runtime_loader'),
        include_directories('.'),  # For local headers
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DNBBY=8',
        '-DHAVE_ICU=1',
        '-I/home/ruslan/haiku/generated/build_packages/icu74-74.1-3-x86_64/develop/headers/unicode',
        '-DARCH_' + target_arch,
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DNBBY=8',
        '-DHAVE_ICU=1',
        '-I/home/ruslan/haiku/generated/build_packages/icu74-74.1-3-x86_64/develop/headers/unicode',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
posix_malloc_dep = declare_dependency(
    link_with: posix_malloc_lib
)

message('libroot POSIX malloc layer configured successfully')