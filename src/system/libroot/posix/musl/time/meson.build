# Haiku libroot POSIX musl time - Meson Build Configuration
# Mirrors the Jamfile: MergeObject posix_musl_time.o

musl_time_sources = [
    '__month_to_secs.c',
    '__secs_to_tm.c',
    '__tm_to_secs.c', 
    '__year_to_secs.c',
    'asctime.c',
    'asctime_r.c',
    'difftime.c',
    'strftime.c',
    'strptime.c',
    'wcsftime.c',
]

musl_time = static_library('posix_musl_time',
    musl_time_sources,
    include_directories: [
        haiku_config['get_all_include_dirs'],
        include_directories('../include'),
        include_directories('../internal'),
        include_directories('../arch/x86_64'),
    ],
    c_args: [
        '-O2',
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-DARCH_x86_64', 
        '-DBUILDING_LIBROOT=1'
    ],
    pic: true,
    install: false
)

# Export dependency
musl_time_dep = declare_dependency(
    link_with: musl_time,
    include_directories: [
        include_directories('../include'),
        include_directories('../internal'),
        include_directories('../arch/x86_64'),
    ]
)