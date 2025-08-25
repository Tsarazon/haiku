# Haiku libroot POSIX glibc stdlib
# Corresponds to src/system/libroot/posix/glibc/stdlib/Jamfile

message('Building libroot POSIX glibc stdlib...')

# Architecture detection
target_arch = get_option('target_arch')

# Glibc stdlib sources (from Jamfile MergeObject - complete list from Jamfile)
glibc_stdlib_sources = files([
    'drand48.c',
    'drand48_r.c',
    'drand48-iter.c',
    'erand48.c',
    'fpioconst.c',
    'jrand48.c',
    'inlines.c',
    'lcong48.c',
    'lrand48.c',
    'mrand48.c',
    'nrand48.c',
    'random_r.c',
    'seed48.c',
    'srand48.c',
    'strtod.c',
    'strtold.c',
    'strtof.c',
    'strtol.c',
    'strtoll.c',
    'strtoul.c',
    'strtoull.c',
    # These are nonstandard but were declared in BeOS's headers (from Jamfile comment)
    'erand48_r.c',
    'jrand48_r.c',
    'lcong48_r.c',
    'lrand48_r.c',
    'mrand48_r.c',
    'nrand48_r.c',
    'seed48_r.c',
    'srand48_r.c',
])

# Build glibc stdlib object library
glibc_stdlib_lib = static_library('glibc_stdlib',
    glibc_stdlib_sources,
    
    include_directories: [
        # glibc headers FIRST to override system headers
        include_directories('../include'),
        include_directories('../include/arch/' + target_arch),
        include_directories('../include/arch/generic'),
        include_directories('..'),
        include_directories('../locale'),
        include_directories('.'),
        haiku_config['get_all_include_dirs'],
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',  # From Jamfile: SubDirCcFlags -D_GNU_SOURCE -DUSE_IN_LIBIO
        '-DUSE_IN_LIBIO',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
        '-DNOT_IN_libc=1',
        '-D__extern_inline=inline',  # Fix __extern_inline
        '-Dtypeof=__typeof__',       # Fix typeof macro
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-DUSE_IN_LIBIO',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
        '-DNOT_IN_libc=1',
        '-D__extern_inline=inline',
        '-Dtypeof=__typeof__',
    ],
    
    install: false
)

# Export for parent
glibc_stdlib_dep = declare_dependency(
    link_with: glibc_stdlib_lib
)

message('glibc stdlib configured successfully')