# Haiku libroot POSIX glibc implementation
# Interconnected modular system using subdirectories

message('Building libroot POSIX glibc layer...')

# Architecture detection
target_arch = get_option('target_arch')

# Build features integration for glibc
build_features_available = [
    'icu',  # /home/ruslan/haiku/generated/build_packages/icu74-74.1-3-x86_64/develop/headers/unicode
]
message('glibc using build features:', build_features_available)

# Include all subdirectories with meson.build files
subdir('extensions')
subdir('iconv')  
subdir('libio')
subdir('regex')
subdir('stdio-common')
subdir('stdlib')
subdir('wcsmbs')

# Include architecture-specific subdirectories
if target_arch == 'x86_64'
    subdir('arch/x86_64')
elif target_arch == 'x86'
    subdir('arch/x86')
elif target_arch == 'arm'
    subdir('arch/arm')
elif target_arch == 'arm64'
    subdir('arch/arm64')
elif target_arch == 'riscv64'
    subdir('arch/riscv64')
endif

# Collect all dependencies
glibc_deps = [
    glibc_extensions_dep,
    glibc_iconv_dep,
    glibc_libio_dep,
    glibc_regex_dep,
    glibc_stdio_common_dep,
    glibc_stdlib_dep,
    glibc_wcsmbs_dep,
]

# Add architecture-specific dependency
if target_arch == 'x86_64'
    glibc_deps += [glibc_arch_x86_64_dep]
elif target_arch == 'x86'
    glibc_deps += [glibc_arch_x86_dep]
elif target_arch == 'arm'
    glibc_deps += [glibc_arch_arm_dep]  
elif target_arch == 'arm64'
    glibc_deps += [glibc_arch_arm64_dep]
elif target_arch == 'riscv64'
    glibc_deps += [glibc_arch_riscv64_dep]
endif

# Build final glibc static library combining all modules
posix_glibc_lib = static_library('posix_glibc',
    # No source files - everything comes from dependencies
    
    include_directories: [
        include_directories('include/arch/' + target_arch),
        include_directories('include/arch/generic'),
        include_directories('include'),
        include_directories('libio'),
        include_directories('extensions'),
        include_directories('iconv'),
        include_directories('regex'),
        include_directories('stdio-common'),
        include_directories('stdlib'),
        include_directories('wcsmbs'),
        include_directories('locale'),
        include_directories('math'),
        include_directories('.'),
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DHAVE_ICU=1',
        '-DARCH_' + target_arch,
        '-D__BEGIN_DECLS=',
        '-D__END_DECLS=',
        '-D_LIBC=1',
        '-DHAVE_CONFIG_H=1',
        '-DSHARED=1',
        '-DDO_VERSIONING=1',
    ],
    
    dependencies: glibc_deps,
    install: false
)

# Export for parent
posix_glibc_dep = declare_dependency(
    link_with: posix_glibc_lib,
    dependencies: glibc_deps
)

message('libroot POSIX glibc layer configured successfully with interconnected modules')