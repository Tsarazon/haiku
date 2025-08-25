# Haiku libroot POSIX crypt implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX crypt layer...')

# crypt implementation sources (from Jamfile MergeObject)
posix_crypt_sources = files([
    'crypto_scrypt_smix.cpp',
    'crypto_scrypt.cpp', 
    'crypt.cpp',
    'pbkdf2.cpp',
    'SHA256.cpp',
    # NO empty_placeholder.c - not in Jamfile!
])

# Build crypt object library
posix_crypt_lib = static_library('posix_crypt',
    posix_crypt_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: UsePrivateHeaders libroot, UsePrivateSystemHeaders
        # From Jamfile: SubDirSysHdrs compatibility/bsd and gnu
        include_directories('../../../../../headers/compatibility/bsd'),
        include_directories('../../../../../headers/compatibility/gnu'),
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
posix_crypt_dep = declare_dependency(
    link_with: posix_crypt_lib
)

message('libroot POSIX crypt layer configured successfully')