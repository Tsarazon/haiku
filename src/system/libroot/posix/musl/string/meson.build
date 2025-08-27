# Haiku libroot POSIX musl string - Meson Build Configuration  
# Mirrors the Jamfile: MergeObject posix_musl_string.o

musl_string_sources = [
    'memccpy.c',
    'memchr.c', 
    'memmem.c',
    'memmove.c',
    'memrchr.c',
    'stpcpy.c',
    'stpncpy.c',
    'strcat.c',
    'strchrnul.c',
    'strcspn.c',
    'strlcat.c',
    'strlcpy.c',
    'strlen.c', 
    'strncat.c',
    'strnlen.c',
    'strpbrk.c',
    'strspn.c',
    'strstr.c',
    'swab.c',
]

musl_string = static_library('posix_musl_string',
    musl_string_sources,
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
musl_string_dep = declare_dependency(
    link_with: musl_string,
    include_directories: [
        include_directories('../include'),
        include_directories('../internal'),
    ]
)