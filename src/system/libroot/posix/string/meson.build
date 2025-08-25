# Haiku libroot POSIX string implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX string layer...')

# Architecture detection
target_arch = get_option('target_arch')

# string implementation sources
posix_string_sources = files([
    'bcmp.c',
    'bcopy.c',
    'bzero.c',
    'memcmp.c',
    'strcasecmp.c',
    'strcasestr.c',
    'strchr.c',
    'strcmp.c',
    'strcoll.cpp',
    'strcpy.c',
    'strdup.cpp',
    'strerror.c',
    'strlwr.c',
    'strncmp.c',
    'strncpy.cpp',
    'strndup.cpp',
    'strrchr.c',
    'strtok.c',
    'strupr.c',
    'strxfrm.cpp',
])

# Architecture-specific string sources
if target_arch == 'x86_64'
    posix_string_arch_sources = files([
        'arch/x86_64/memcpy.cpp',
        'arch/x86_64/memset.cpp',
    ])
else
    posix_string_arch_sources = []
endif

# Build string object library
posix_string_lib = static_library('posix_string',
    posix_string_sources,
    posix_string_arch_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: UsePrivateHeaders [ FDirName libroot ] and [ FDirName libroot locale ]
        include_directories('../../../../../headers/private/libroot'),
        include_directories('../../../../../headers/private/libroot/locale'),
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
        # From Jamfile: C++FLAGS += -fno-builtin-strdup -fno-builtin-strndup (BeOS compatibility)
        '-fno-builtin-strdup',
        '-fno-builtin-strndup',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
posix_string_dep = declare_dependency(
    link_with: posix_string_lib
)

message('libroot POSIX string layer configured successfully')