# Haiku libroot POSIX arch x86
# Corresponds to src/system/libroot/posix/arch/x86/Jamfile

message('Building libroot POSIX arch x86...')

# Architecture detection - must be x86
target_arch = get_option('target_arch')
if target_arch != 'x86'
    error('This meson.build is only for x86 architecture')
endif

# Arch x86 sources (from Jamfile MergeObject)
posix_arch_x86_sources = files([
    'fenv.c',
    'sigsetjmp.S',
    'siglongjmp.S',
    # Generic sources (from Jamfile SEARCH on genericSources)
    '../generic/setjmp_save_sigs.c',  # From SEARCH = [ FDirName $(SUBDIR) $(DOTDOT) generic ]
    '../generic/longjmp_return.c',
])

# Build arch x86 object library
posix_arch_x86_lib = static_library('posix_arch_x86',
    posix_arch_x86_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: UsePrivateSystemHeaders
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
posix_arch_x86_dep = declare_dependency(
    link_with: posix_arch_x86_lib
)

message('libroot POSIX arch x86 configured successfully')