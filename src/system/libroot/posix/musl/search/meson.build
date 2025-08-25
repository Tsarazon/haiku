# Haiku libroot POSIX musl search
# Corresponds to src/system/libroot/posix/musl/search/Jamfile

message('Building libroot POSIX musl search...')

target_arch = get_option('target_arch')

posix_musl_search_sources = files([
    'hsearch.c',
    'insque.c',
    'lsearch.c',
    'tdelete.c',
    'tdestroy.c',
    'tfind.c',
    'tsearch.c',
    'twalk.c',
])

posix_musl_search_lib = static_library('posix_musl_search',
    posix_musl_search_sources,
    
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

posix_musl_search_dep = declare_dependency(
    link_with: posix_musl_search_lib
)

message('libroot POSIX musl search configured successfully')