# Haiku libroot POSIX musl crypt
# Corresponds to src/system/libroot/posix/musl/crypt/Jamfile

message('Building libroot POSIX musl crypt...')

# Architecture detection
target_arch = get_option('target_arch')

# Musl crypt sources (from Jamfile MergeObject)
posix_musl_crypt_sources = files([
    'crypt_des.c',
    'encrypt.c',
])

# Build musl crypt object library
posix_musl_crypt_lib = static_library('posix_musl_crypt',
    posix_musl_crypt_sources,
    
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
posix_musl_crypt_dep = declare_dependency(
    link_with: posix_musl_crypt_lib
)

message('libroot POSIX musl crypt configured successfully')