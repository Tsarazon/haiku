# Haiku libroot POSIX musl misc
# Corresponds to src/system/libroot/posix/musl/misc/Jamfile

message('Building libroot POSIX musl misc...')

target_arch = get_option('target_arch')

posix_musl_misc_sources = files([
    'a64l.c',
    'basename.c',
    'dirname.c',
    'ffs.c',
    'ftw.c',
    'getsubopt.c',
    'nftw.c',
])

posix_musl_misc_lib = static_library('posix_musl_misc',
    posix_musl_misc_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        include_directories('../include'),
        include_directories('../internal'),
        include_directories('../arch/' + target_arch),
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

posix_musl_misc_dep = declare_dependency(
    link_with: posix_musl_misc_lib
)

message('libroot POSIX musl misc configured successfully')