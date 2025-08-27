# Haiku libroot POSIX glibc iconv
# Corresponds to src/system/libroot/posix/glibc/iconv/Jamfile

message('Building libroot POSIX glibc iconv...')

# Architecture detection
target_arch = get_option('target_arch')

# Glibc iconv sources (from Jamfile MergeObject)
glibc_iconv_sources = files([
    'gconv_builtin.c',
    'gconv_simple.c',
])

# Build glibc iconv object library
glibc_iconv_lib = static_library('glibc_iconv',
    glibc_iconv_sources,
    
    include_directories: [
        # From Jamfile: SubDirSysHdrs - glibc directories FIRST
        include_directories('../include/arch/' + target_arch),
        include_directories('../include/arch/generic'),
        include_directories('../include'),
        include_directories('../libio'),
        include_directories('.'),  # iconv
        include_directories('../locale'),
        include_directories('..'),  # glibc root
        # From Jamfile: UsePrivateHeaders libroot - use haiku_config includes
        haiku_config['get_all_include_dirs'],
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',  # From Jamfile: SubDirCcFlags -D_GNU_SOURCE -DUSE_IN_LIBIO
        '-DUSE_IN_LIBIO',
        '-D__mbstate_t=mbstate_t',  # Map glibc internal type to Haiku type
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-DUSE_IN_LIBIO',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
glibc_iconv_dep = declare_dependency(
    link_with: glibc_iconv_lib
)

message('glibc iconv configured successfully')