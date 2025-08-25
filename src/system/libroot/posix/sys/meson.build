# Haiku libroot POSIX sys implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX sys layer...')

# sys implementation sources
posix_sys_sources = files([
    'chmod.c',
    'empty_placeholder.c',
    'flock.c',
    'ftime.c',
    'ftok.c',
    'getrusage.c',
    'gettimeofday.c',
    'itimer.cpp',
    'mkdir.c',
    'mkfifo.c',
    'mknod.c',
    'mman.cpp',
    'priority.c',
    'rlimit.c',
    'select.cpp',
    'stat.c',
    'statvfs.c',
    'times.cpp',
    'uio.c',
    'umask.c',
    'uname.c',
    'utimes.c',
    'wait.cpp',
    'xsi_msg_queue.cpp',
    'xsi_sem.cpp',
])

# Build sys object library
posix_sys_lib = static_library('posix_sys',
    posix_sys_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
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
posix_sys_dep = declare_dependency(
    link_with: posix_sys_lib
)

message('libroot POSIX sys layer configured successfully')