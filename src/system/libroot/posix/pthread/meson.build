# Haiku libroot POSIX pthread implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX pthread layer...')

# pthread implementation sources (from Jamfile MergeObject)
posix_pthread_sources = files([
    'pthread.cpp',
    'pthread_atfork.c',
    'pthread_attr.c',
    'pthread_barrier.cpp',
    'pthread_cancel.cpp',
    'pthread_cleanup.cpp',
    'pthread_cond.cpp',
    'pthread_condattr.c',
    'pthread_key.cpp',
    'pthread_mutex.cpp',
    'pthread_mutexattr.c',
    'pthread_once.cpp',
    'pthread_rwlock.cpp',
    'pthread_spinlock.cpp',
    # NO empty_placeholder.c - not in Jamfile!
])

# Build pthread object library
posix_pthread_lib = static_library('posix_pthread',
    posix_pthread_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: UsePrivateHeaders kernel (for util/DoublyLinkedList.h)
        include_directories('../../../../../headers/private/kernel'),
        # From Jamfile: UsePrivateHeaders libroot shared
        include_directories('../../../../../headers/private/libroot'),
        include_directories('../../../../../headers/private/shared'),
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-D_REENTRANT',
        '-DARCH_' + target_arch,
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-D_REENTRANT',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
posix_pthread_dep = declare_dependency(
    link_with: posix_pthread_lib
)

message('libroot POSIX pthread layer configured successfully')