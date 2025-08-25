# Haiku libroot POSIX malloc openbsd
# Corresponds to src/system/libroot/posix/malloc/openbsd/Jamfile

message('Building libroot POSIX malloc openbsd...')

# Architecture detection
target_arch = get_option('target_arch')

# Malloc openbsd sources (from Jamfile MergeObject)
posix_malloc_openbsd_sources = files([
    'PagesAllocator.cpp',
    'malloc.c',
    'r5_compatibility.c',
])

# Build malloc openbsd object library
posix_malloc_openbsd_lib = static_library('posix_malloc_openbsd',
    posix_malloc_openbsd_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: UsePrivateSystemHeaders
        # From Jamfile: UsePrivateHeaders kernel libroot shared
        include_directories('../../../../../headers/private/kernel'),
        include_directories('../../../../../headers/private/libroot'),
        include_directories('../../../../../headers/private/shared'),
        # From Jamfile: UseHeaders [ FDirName $(HAIKU_TOP) headers compatibility bsd ] : true
        include_directories('../../../../../headers/compatibility/bsd'),
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
posix_malloc_openbsd_dep = declare_dependency(
    link_with: posix_malloc_openbsd_lib
)

message('libroot POSIX malloc openbsd configured successfully')