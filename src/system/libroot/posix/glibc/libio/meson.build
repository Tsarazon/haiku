# Haiku libroot POSIX glibc libio
# Corresponds to src/system/libroot/posix/glibc/libio/Jamfile

message('Building libroot POSIX glibc libio...')

# Architecture detection
target_arch = get_option('target_arch')

# Glibc libio sources (from Jamfile MergeObject - complete list from Jamfile)
glibc_libio_sources = files([
    '__fbufsize.c',
    '__flbf.c',
    '__fpending.c',
    '__fpurge.c',
    '__freadable.c',
    '__freadahead.c',
    '__freading.c',
    '__freadptr.c',
    '__freadptrinc.c',
    '__fseterr.c',
    '__fsetlocking.c',
    '__fwritable.c',
    '__fwriting.c',
    'clearerr.c',
    'clearerr_u.c',
    'fcloseall.c',
    'feof.c',
    'feof_u.c',
    'ferror.c',
    'ferror_u.c',
    'fileno.c',
    'filedoalloc.c',
    'fileops.c',
    'flockfile.c',
    'fmemopen.c',
    'fputc.c',
    'fputc_u.c',
    'freopen.c',
    'fseek.c',
    'fseeko.c',
    'ftello.c',
    'fwide.c',
    'genops.c',
    'getc.c',
    'getc_u.c',
    'getchar.c',
    'getchar_u.c',
    'iofclose.c',
    'iofdopen.c',
    'iofflush.c',
    'iofgetpos.c',
    'iofgets.c',
    'iofgets_u.c',
    'iofopen.c',
    'iofopncook.c',
    'iofputs.c',
    'iofputs_u.c',
    'iofread.c',
    'iofread_u.c',
    'iofsetpos.c',
    'ioftell.c',
    'iofwrite.c',
    'iofwrite_u.c',
    'iofwide.c',
    'iogetdelim.c',
    'iogetline.c',
    'iogets.c',
    'iopadn.c',
    'iopopen.c',
    'ioputs.c',
    'ioseekoff.c',
    'ioseekpos.c',
    'iosetbuffer.c',
    'iosetvbuf.c',
    'ioungetc.c',
    'iovdprintf.c',
    'iovsprintf.c',
    'iovsscanf.c',
    'memstream.c',
    'pclose.c',
    'peekc.c',
    'putc.c',
    'putc_u.c',
    'putchar.c',
    'putchar_u.c',
    'rewind.c',
    'setbuf.c',
    'setlinebuf.c',
    'stdfiles.c',
    'stdio.c',
    'strops.c',
    'vasprintf.c',
    'vscanf.c',
    'vsnprintf.c',
    # Wide character support
    'fputwc.c',
    'fputwc_u.c',
    'fwprintf.c',
    'fwscanf.c',
    'getwc.c',
    'getwc_u.c',
    'getwchar.c',
    'iofgetws.c',
    'iofputws.c',
    'iofputws_u.c',
    'iogetwline.c',
    'ioungetwc.c',
    'iovswscanf.c',
    'iowpadn.c',
    'putwc.c',
    'putwc_u.c',
    'putwchar.c',
    'putwchar_u.c',
    'swprintf.c',
    'swscanf.c',
    'vswprintf.c',
    'vwprintf.c',
    'vwscanf.c',
    'wfiledoalloc.c',
    'wfileops.c',
    'wgenops.c',
    'wprintf.c',
    'wscanf.c',
    'wstrops.c',
])

# Build glibc libio object library
glibc_libio_lib = static_library('glibc_libio',
    glibc_libio_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: SubDirSysHdrs include/arch/TARGET_ARCH
        include_directories('../include/arch/' + target_arch),
        # From Jamfile: SubDirSysHdrs include/arch/generic
        include_directories('../include/arch/generic'),
        # From Jamfile: SubDirSysHdrs stdio-common
        include_directories('../stdio-common'),
        # From Jamfile: SubDirSysHdrs libio
        include_directories('.'),
        # From Jamfile: SubDirSysHdrs include
        include_directories('../include'),
        # From Jamfile: SubDirSysHdrs locale
        include_directories('../locale'),
        # From Jamfile: SubDirSysHdrs glibc root
        include_directories('..'),
        # From Jamfile: SubDirSysHdrs iconv
        include_directories('../iconv'),
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
glibc_libio_dep = declare_dependency(
    link_with: glibc_libio_lib
)

message('glibc libio configured successfully')