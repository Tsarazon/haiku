# Haiku libroot POSIX malloc hoard2
# Corresponds to src/system/libroot/posix/malloc/hoard2/Jamfile

message('Building libroot POSIX malloc hoard2...')

# Architecture detection
target_arch = get_option('target_arch')

# Malloc hoard2 sources (from Jamfile MergeObject)
posix_malloc_hoard2_sources = files([
    'arch-specific.cpp',
    'heap.cpp',
    'processheap.cpp',
    'superblock.cpp',
    'threadheap.cpp',
    'wrapper.cpp',
])

# Build malloc hoard2 object library
posix_malloc_hoard2_lib = static_library('posix_malloc_hoard2',
    posix_malloc_hoard2_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: UsePrivateSystemHeaders
        # From Jamfile: UsePrivateHeaders libroot shared
        include_directories('../../../../../headers/private/libroot'),
        include_directories('../../../../../headers/private/shared'),
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
posix_malloc_hoard2_dep = declare_dependency(
    link_with: posix_malloc_hoard2_lib
)

message('libroot POSIX malloc hoard2 configured successfully')