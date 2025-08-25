# Haiku libroot POSIX arch arm
# Corresponds to src/system/libroot/posix/arch/arm/Jamfile

message('Building libroot POSIX arch arm...')

# Architecture detection - must be arm
target_arch = get_option('target_arch')
if target_arch != 'arm'
    error('This meson.build is only for arm architecture')
endif

# Arch arm sources (from Jamfile MergeObject)
posix_arch_arm_sources = files([
    'fenv.c',
    'sigsetjmp.S',
    'siglongjmp.S',
    # Generic sources (from Jamfile SEARCH on genericSources)
    '../generic/setjmp_save_sigs.c',  # From SEARCH = [ FDirName $(SUBDIR) $(DOTDOT) generic ]
    '../generic/longjmp_return.c',
])

# Build arch arm object library
posix_arch_arm_lib = static_library('posix_arch_arm',
    posix_arch_arm_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: UsePrivateHeaders [ FDirName system arch $(TARGET_ARCH) ]
        include_directories('../../../../../headers/private/system/arch/' + target_arch),
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
posix_arch_arm_dep = declare_dependency(
    link_with: posix_arch_arm_lib
)

message('libroot POSIX arch arm configured successfully')