# Haiku libroot POSIX layer - Meson build
# This corresponds to src/system/libroot/posix/Jamfile

message('Building libroot POSIX layer...')

# Architecture detection  
target_arch = get_option('target_arch')

# Essential POSIX layer sources (from Jamfile MergeObject posix_main.o)
posix_sources = files([
    'assert.cpp',
    'cat.cpp',
    'devctl.c',
    'dirent.cpp', 
    'dlfcn.c',
    'errno.c',
    'fcntl.cpp',
    'glob.c',
    'grp.cpp',
    'inttypes.c',
    'math.c',
    'poll.cpp',
    'pwd.cpp',
    'scheduler.cpp',
    'semaphore.cpp',
    'shadow.cpp',
    'spawn.cpp',
    'syslog.cpp',
    'termios.c', 
    'user_group_common.cpp',
    'utime.c',
    'utmpx.cpp',
])

# Architecture-specific subdirectory
subdir('arch/' + target_arch)

# Essential C library function subdirectories (auto-generated)
subdir('crypt')   # crypt
subdir('glibc')   # glibc
subdir('locale')   # locale
subdir('malloc')   # malloc
subdir('musl')   # musl
subdir('pthread')   # pthread
subdir('signal')   # signal
subdir('stdio')   # stdio
subdir('stdlib')   # stdlib
subdir('string')   # string
subdir('sys')   # sys
subdir('time')   # time
subdir('unistd')   # unistd
subdir('wchar')   # wchar
subdir('libstdthreads')   # libstdthreads

# Build POSIX layer object library
posix_main_lib = static_library('posix_main',
    posix_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # Ensure runtime_loader headers are found
        include_directories('../../../../headers/private/runtime_loader'),
        # Ensure syslog_daemon headers are found
        include_directories('../../../../headers/private/syslog_daemon'),
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
posix_main_dep = declare_dependency(
    link_with: posix_main_lib
)

message('libroot POSIX layer configured successfully')