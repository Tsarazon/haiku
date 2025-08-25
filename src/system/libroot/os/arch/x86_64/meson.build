# Haiku libroot OS layer - x86_64 architecture specific
# This corresponds to src/system/libroot/os/arch/x86_64/Jamfile

message('Building libroot OS x86_64 architecture layer...')

# x86_64 architecture-specific sources
arch_sources = files([
    'byteorder.S',
    'get_stack_frame.S',
    'system_info.cpp',
    'system_time.cpp', 
    'thread.cpp',
    'time.cpp',
    'tls.cpp',
])

# Build architecture-specific object library
os_arch_lib = static_library('os_arch_x86_64',
    arch_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # x86_64 uses x86 arch headers
        include_directories('../../../../../../headers/private/kernel/arch/x86'),
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-DARCH_x86_64',
        '-DBUILDING_LIBROOT=1',
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE', 
        '-DARCH_x86_64',
        '-DBUILDING_LIBROOT=1',
    ],
    
    install: false
)

# Export for parent
os_arch_dep = declare_dependency(
    link_with: os_arch_lib
)

message('libroot OS x86_64 architecture layer configured successfully')