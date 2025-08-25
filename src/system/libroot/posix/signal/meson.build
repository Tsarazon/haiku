# Haiku libroot POSIX signal implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX signal layer...')

# signal implementation sources
posix_signal_sources = files([
    'empty_placeholder.c',
    'kill.c',
    'killpg.cpp',
    'psiginfo.cpp',
    'psignal.cpp',
    'raise.c',
    'send_signal.c',
    'set_signal_mask.cpp',
    'set_signal_stack.c',
    'sigaction.cpp',
    'sigaltstack.c',
    'sighold.cpp',
    'sigignore.cpp',
    'siginterrupt.cpp',
    'signal.cpp',
    'signal_limits.cpp',
    'sigpause.cpp',
    'sigpending.cpp',
    'sigqueue.cpp',
    'sigrelse.cpp',
    'sigset.cpp',
    'sigset_accessors.cpp',
    'sigsuspend.cpp',
    'sigwait.cpp',
    'sigwaitinfo.cpp',
    'strsignal.cpp',
])

# Build signal object library
posix_signal_lib = static_library('posix_signal',
    posix_signal_sources,
    
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
posix_signal_dep = declare_dependency(
    link_with: posix_signal_lib
)

message('libroot POSIX signal layer configured successfully')