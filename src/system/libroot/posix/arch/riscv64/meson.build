# Haiku libroot POSIX arch riscv64
# Corresponds to src/system/libroot/posix/arch/riscv64/Jamfile

message('Building libroot POSIX arch riscv64...')

# Architecture detection - must be riscv64
target_arch = get_option('target_arch')
if target_arch != 'riscv64'
    error('This meson.build is only for riscv64 architecture')
endif

# Arch riscv64 sources (from Jamfile MergeObject)
posix_arch_riscv64_sources = files([
    'fenv.c',
    'sigsetjmp.S',
    'siglongjmp.S',
    # Generic sources (from Jamfile SEARCH on genericSources)
    '../generic/setjmp_save_sigs.c',  # From SEARCH = [ FDirName $(SUBDIR) $(DOTDOT) generic ]
    '../generic/longjmp_return.c',
])

# Build arch riscv64 object library
posix_arch_riscv64_lib = static_library('posix_arch_riscv64',
    posix_arch_riscv64_sources,
    
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
posix_arch_riscv64_dep = declare_dependency(
    link_with: posix_arch_riscv64_lib
)

message('libroot POSIX arch riscv64 configured successfully')