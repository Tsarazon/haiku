# Haiku libroot POSIX layer - x86_64 architecture specific  
# This corresponds to src/system/libroot/posix/arch/x86_64/Jamfile

message('Building libroot POSIX x86_64 architecture layer...')

# x86_64 POSIX architecture-specific sources
posix_arch_sources = files([
    'fenv.c',
    'siglongjmp.S',
    'sigsetjmp.S',
])

# Build POSIX architecture-specific object library
posix_arch_lib = static_library('posix_arch_x86_64',
    posix_arch_sources,
    
    include_directories: haiku_config['get_all_include_dirs'],
    
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
posix_arch_dep = declare_dependency(
    link_with: posix_arch_lib
)

message('libroot POSIX x86_64 architecture layer configured successfully')