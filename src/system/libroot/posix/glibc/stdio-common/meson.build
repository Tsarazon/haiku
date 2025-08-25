# Haiku libroot POSIX glibc stdio-common
# Corresponds to src/system/libroot/posix/glibc/stdio-common/Jamfile

message('Building libroot POSIX glibc stdio-common...')

# Architecture detection
target_arch = get_option('target_arch')

# Glibc stdio-common sources (from Jamfile MergeObject - complete list)
glibc_stdio_common_sources = files([
    '_itoa.c',
    '_itowa.c',
    'asprintf.c',
    'dprintf.c',
    'fprintf.c',
    'fxprintf.c',
    'fscanf.c',
    'getline.c',
    'getw.c',
    'itoa-digits.c',
    'itoa-udigits.c',
    'itowa-digits.c',
    'perror.c',
    'printf-prs.c',
    'printf-parsemb.c',
    'printf-parsewc.c',
    'printf.c',
    'printf_fp.c',
    'printf_fphex.c',
    'printf_size.c',
    'putw.c',
    'reg-printf.c',
    'scanf.c',
    'snprintf.c',
    'sprintf.c',
    'sscanf.c',
    'vfprintf.c',
    'vfscanf.c',
    'vfwprintf.c',
    'vfwscanf.c',
    'vprintf.c',
    # Note: vfprintf_stub.c and vfscanf_stub.c are commented out in Jamfile
])

# Build glibc stdio-common object library
glibc_stdio_common_lib = static_library('glibc_stdio_common',
    glibc_stdio_common_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: SubDirSysHdrs libio
        include_directories('../libio'),
        # From Jamfile: SubDirSysHdrs include/arch/TARGET_ARCH
        include_directories('../include/arch/' + target_arch),
        # From Jamfile: SubDirSysHdrs include/arch/generic
        include_directories('../include/arch/generic'),
        # From Jamfile: SubDirSysHdrs include
        include_directories('../include'),
        # From Jamfile: SubDirSysHdrs stdio-common
        include_directories('.'),
        # From Jamfile: SubDirSysHdrs ctype (directory doesn't exist, removed)
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
glibc_stdio_common_dep = declare_dependency(
    link_with: glibc_stdio_common_lib
)

message('glibc stdio-common configured successfully')