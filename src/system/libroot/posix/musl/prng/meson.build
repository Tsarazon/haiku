# Haiku libroot POSIX musl prng
# Corresponds to src/system/libroot/posix/musl/prng/Jamfile

message('Building libroot POSIX musl prng...')

target_arch = get_option('target_arch')

posix_musl_prng_sources = files([
    'rand.c',
    'rand_r.c',
])

posix_musl_prng_lib = static_library('posix_musl_prng',
    posix_musl_prng_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        include_directories('../include'),
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

posix_musl_prng_dep = declare_dependency(
    link_with: posix_musl_prng_lib
)

message('libroot POSIX musl prng configured successfully')