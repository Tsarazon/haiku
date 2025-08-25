# Haiku libroot POSIX glibc extensions
# Corresponds to src/system/libroot/posix/glibc/extensions/Jamfile

message('Building libroot POSIX glibc extensions...')

# Architecture detection
target_arch = get_option('target_arch')

# Extensions sources (from Jamfile MergeObject)
glibc_extensions_sources = files([
    'getopt.c',
    'getopt1.c',
    'obstack.c',
])

# Build extensions static library
glibc_extensions_lib = static_library('glibc_extensions',
    glibc_extensions_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: SubDirSysHdrs include/arch/TARGET_ARCH
        include_directories('../include/arch/' + target_arch),
        # From Jamfile: SubDirSysHdrs include/arch/generic  
        include_directories('../include/arch/generic'),
        # From Jamfile: SubDirSysHdrs include
        include_directories('../include'),
        # From Jamfile: SubDirSysHdrs ctype (directory doesn't exist, removed)
        # From Jamfile: SubDirSysHdrs libio
        include_directories('../libio'),
        # From Jamfile: SubDirSysHdrs locale
        include_directories('../locale'),
        # From Jamfile: SubDirSysHdrs glibc root
        include_directories('..'),
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
        '-D__BEGIN_DECLS=',
        '-D__END_DECLS=',
        '-DNOT_IN_libc=1',
    ],
    
    install: false
)

# Export for parent
glibc_extensions_dep = declare_dependency(
    link_with: glibc_extensions_lib
)

message('glibc extensions configured successfully')