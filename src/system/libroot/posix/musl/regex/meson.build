# Haiku libroot POSIX musl regex
# Corresponds to src/system/libroot/posix/musl/regex/Jamfile

message('Building libroot POSIX musl regex...')

target_arch = get_option('target_arch')

posix_musl_regex_sources = files([
    'fnmatch.c',
])

posix_musl_regex_lib = static_library('posix_musl_regex',
    posix_musl_regex_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        include_directories('../include'),
        include_directories('../internal'),
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

posix_musl_regex_dep = declare_dependency(
    link_with: posix_musl_regex_lib
)

message('libroot POSIX musl regex configured successfully')