# Haiku libroot POSIX unistd implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX unistd layer...')

# Build features integration for unistd
build_features_available = [
    'icu',  # /home/ruslan/haiku/generated/build_packages/icu74-74.1-3-x86_64/develop/headers/unicode
]
message('unistd using build features:', build_features_available)

# unistd implementation sources
posix_unistd_sources = files([
    '_exit.c',
    'access.c',
    'alarm.c',
    'brk.c',
    'chown.c',
    'chroot.cpp',
    'close.c',
    'conf.cpp',
    'directory.c',
    'dup.c',
    'empty_placeholder.c',
    'exec.cpp',
    'fork.c',
    'getentropy.c',
    'getlogin.cpp',
    'getpagesize.c',
    'hostname.cpp',
    'ioctl.c',
    'link.c',
    'lockf.cpp',
    'lseek.c',
    'mount.c',
    'nice.c',
    'pause.c',
    'pipe.c',
    'process.c',
    'read.c',
    'sleep.c',
    'sync.c',
    'system.cpp',
    'terminal.c',
    'truncate.c',
    'ttyname.c',
    'ualarm.c',
    'usergroup.cpp',
    'usleep.c',
    'write.c',
])

# Build unistd object library
posix_unistd_lib = static_library('posix_unistd',
    posix_unistd_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # runtime_loader.h location
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
        '-DHAVE_ICU=1',
        '-I/home/ruslan/haiku/generated/build_packages/icu74-74.1-3-x86_64/develop/headers/unicode',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
posix_unistd_dep = declare_dependency(
    link_with: posix_unistd_lib
)

message('libroot POSIX unistd layer configured successfully')