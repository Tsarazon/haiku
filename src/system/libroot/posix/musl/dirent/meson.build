# Haiku libroot POSIX musl dirent
# Corresponds to src/system/libroot/posix/musl/dirent/Jamfile

message('Building libroot POSIX musl dirent...')

target_arch = get_option('target_arch')

posix_musl_dirent_sources = files([
    'alphasort.c',
    'scandir.c',
])

posix_musl_dirent_lib = static_library('posix_musl_dirent',
    posix_musl_dirent_sources,
    
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

posix_musl_dirent_dep = declare_dependency(
    link_with: posix_musl_dirent_lib
)

message('libroot POSIX musl dirent configured successfully')