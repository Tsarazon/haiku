# Haiku libroot POSIX musl stdlib - Meson Build Configuration
# Mirrors the Jamfile: MergeObject posix_musl_stdlib.o

musl_stdlib_sources = [
    'ecvt.c',
    'fcvt.c', 
    'gcvt.c',
]

musl_stdlib = static_library('posix_musl_stdlib',
    musl_stdlib_sources,
    include_directories: [
        haiku_config['get_all_include_dirs'],
        include_directories('../include'),
        include_directories('../internal'),
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
musl_stdlib_dep = declare_dependency(
    link_with: musl_stdlib,
    include_directories: [
        include_directories('../include'),
        include_directories('../internal'),
    ]
)