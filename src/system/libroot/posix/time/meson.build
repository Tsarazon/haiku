# Haiku libroot POSIX time implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX time layer...')

# Build features integration for time
build_features_available = [
    'icu',  # /home/ruslan/haiku/generated/build_packages/icu74-74.1-3-x86_64/develop/headers/unicode
]
message('time using build features:', build_features_available)

# time implementation sources
posix_time_sources = files([
    'clock.cpp',
    'clock_support.cpp',
    'ctime.c',
    'empty_placeholder.c',
    'localtime.cpp',
    'localtime_fallback.c',
    'nanosleep.c',
    'stime.c',
    'time.c',
    'timer_support.cpp',
    'timespec_get.cpp',
])

# Build time object library
posix_time_lib = static_library('posix_time',
    posix_time_sources,
    
    include_directories: [
        # Additional musl headers for 'hidden' macro MUST come first
        include_directories('../musl/include'),
        include_directories('../musl/internal'),
        haiku_config['get_all_include_dirs'],
        # LocaleBackend.h location
        include_directories('../../../../../headers/private/libroot/locale'),
        include_directories('../../../../../src/kits/locale'), 
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DHAVE_ICU=1',
        '-I/home/ruslan/haiku/generated/build_packages/icu74-74.1-3-x86_64/develop/headers/unicode',
        '-DARCH_' + target_arch,
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DHAVE_ICU=1',
        '-I/home/ruslan/haiku/generated/build_packages/icu74-74.1-3-x86_64/develop/headers/unicode',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
posix_time_dep = declare_dependency(
    link_with: posix_time_lib
)

message('libroot POSIX time layer configured successfully')