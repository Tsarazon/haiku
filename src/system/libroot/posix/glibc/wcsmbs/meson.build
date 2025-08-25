# Haiku libroot POSIX glibc wcsmbs
# Corresponds to src/system/libroot/posix/glibc/wcsmbs/Jamfile

message('Building libroot POSIX glibc wcsmbs...')

# Architecture detection
target_arch = get_option('target_arch')

# Glibc wcsmbs sources (from Jamfile MergeObject)
glibc_wcsmbs_sources = files([
    'wcsmbsload.c',
    'wcstod.c',
    'wcstof.c',
    'wcstol.c',
    'wcstoll.c',
    'wcstold.c',
    'wcstoul.c',
    'wcstoull.c',
])

# Build glibc wcsmbs object library
glibc_wcsmbs_lib = static_library('glibc_wcsmbs',
    glibc_wcsmbs_sources,
    
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
        # From Jamfile: SubDirSysHdrs wcsmbs
        include_directories('.'),
        # From Jamfile: SubDirSysHdrs iconv
        include_directories('../iconv'),
        # From Jamfile: SubDirSysHdrs locale
        include_directories('../locale'),
        # From Jamfile: SubDirSysHdrs stdlib
        include_directories('../stdlib'),
        # From Jamfile: SubDirSysHdrs ctype (directory doesn't exist, removed)
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
glibc_wcsmbs_dep = declare_dependency(
    link_with: glibc_wcsmbs_lib
)

message('glibc wcsmbs configured successfully')