# Haiku libroot POSIX stdlib implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX stdlib layer...')

# Build features integration for stdlib
build_features_available = [
    'freetype',  # /home/ruslan/haiku/generated/build_packages/freetype-2.13.2-1-x86_64/develop/headers/freetype2
]
message('stdlib using build features:', build_features_available)

# stdlib implementation sources
posix_stdlib_sources = files([
    'abs.c',
    'atfork.c',
    'atof.c',
    'atoi.c',
    'bsearch.c',
    'div.c',
    'env.cpp',
    'exit.cpp',
    'heapsort.c',
    'merge.c',
    'mktemp.c',
    'pty.cpp',
    'qsort.c',
    'qsort_r.c',
    'radixsort.c',
    'random.c',
    'reallocarray.cpp',
    'realpath.cpp',
    'strfmon.c',
])

# Build stdlib object library
posix_stdlib_lib = static_library('posix_stdlib',
    posix_stdlib_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        include_directories('../../../../../headers/private/runtime_loader'),
        include_directories('../../../../../headers/private/kernel'),
        include_directories('../../../../../headers/private/drivers'),
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DHAVE_FREETYPE=1',
        '-I/home/ruslan/haiku/generated/build_packages/freetype-2.13.2-1-x86_64/develop/headers/freetype2',
        '-DARCH_' + target_arch,
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DHAVE_FREETYPE=1',
        '-I/home/ruslan/haiku/generated/build_packages/freetype-2.13.2-1-x86_64/develop/headers/freetype2',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
posix_stdlib_dep = declare_dependency(
    link_with: posix_stdlib_lib
)

message('libroot POSIX stdlib layer configured successfully')