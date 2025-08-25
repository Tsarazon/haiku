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
        haiku_config['get_all_include_dirs'],
        # From Jamfile: SubDirSysHdrs include/arch/TARGET_ARCH
        include_directories('../include/arch/' + target_arch),
        # From Jamfile: SubDirSysHdrs include/arch/generic
        include_directories('../include/arch/generic'),
        # From Jamfile: SubDirSysHdrs include
        include_directories('../include'),
        # From Jamfile: SubDirSysHdrs libio
        include_directories('../libio'),
        # From Jamfile: SubDirSysHdrs ctype (directory doesn't exist, removed)
        # From Jamfile: SubDirSysHdrs iconv
        include_directories('.'),
        # From Jamfile: SubDirSysHdrs locale
        include_directories('../locale'),
        # From Jamfile: SubDirSysHdrs glibc root
        include_directories('..'),
        # From Jamfile: UsePrivateHeaders libroot (included in haiku_config)
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',  # From Jamfile: SubDirCcFlags -D_GNU_SOURCE -DUSE_IN_LIBIO
        '-DUSE_IN_LIBIO',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
        '-D__BEGIN_DECLS=',
        '-D__END_DECLS=',
        '-D_LIBC=1',
        '-DHAVE_CONFIG_H=1',
        '-DSHARED=1',
        '-DDO_VERSIONING=1',
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