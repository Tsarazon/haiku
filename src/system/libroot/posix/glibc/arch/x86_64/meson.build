# Haiku libroot POSIX glibc arch x86_64
# Corresponds to src/system/libroot/posix/glibc/arch/x86_64/Jamfile

message('Building libroot POSIX glibc arch x86_64...')

# Architecture detection - must be x86_64
target_arch = get_option('target_arch')
if target_arch != 'x86_64'
    error('This meson.build is only for x86_64 architecture')
endif

# Glibc arch x86_64 sources (from Jamfile MergeObject)
glibc_arch_x86_64_sources = files([
    'add_n.S',
    'addmul_1.S',
    'ldbl2mpn.c',
    'mul_1.S',
    'lshift.S', 
    'rshift.S',
    'sub_n.S',
    'submul_1.S',
    # Generic sources (from Jamfile SEARCH on genericSources)
    '../generic/cmp.c', 
    '../generic/dbl2mpn.c', 
    '../generic/divrem.c',
    '../generic/mpn2dbl.c', 
    '../generic/mpn2flt.c', 
    '../generic/mpn2ldbl.c',
    '../generic/mul.c', 
    '../generic/mul_n.c',
    '../generic/s_cacos.c', 
    '../generic/s_cacosf.c', 
    '../generic/s_cacosl.c',
    '../generic/s_cacoshl.c',
    '../generic/s_casin.c', 
    '../generic/s_casinf.c', 
    '../generic/s_casinl.c',
    '../generic/s_casinh.c', 
    '../generic/s_casinhf.c', 
    '../generic/s_casinhl.c',
    '../generic/s_ccoshl.c',
    '../generic/s_cexp.c', 
    '../generic/s_cexpf.c', 
    '../generic/s_cexpl.c',
    '../generic/s_clog.c', 
    '../generic/s_clogf.c', 
    '../generic/s_clogl.c',
    '../generic/s_clog10.c', 
    '../generic/s_clog10f.c', 
    '../generic/s_clog10l.c',
    '../generic/s_cpow.c', 
    '../generic/s_cpowf.c', 
    '../generic/s_cpowl.c',
    '../generic/s_csinhl.c',
    '../generic/s_csqrtl.c',
    '../generic/s_ctanhl.c',
])

# Build glibc arch x86_64 object library
glibc_arch_x86_64_lib = static_library('glibc_arch_x86_64',
    glibc_arch_x86_64_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: SubDirSysHdrs include/arch/x86_64
        include_directories('../../include/arch/x86_64'),
        # From Jamfile: SubDirSysHdrs include/arch/generic
        include_directories('../../include/arch/generic'),
        # From Jamfile: SubDirSysHdrs include
        include_directories('../../include'),
        # From Jamfile: SubDirSysHdrs libio
        include_directories('../../libio'),
        # From Jamfile: SubDirSysHdrs stdlib
        include_directories('../../stdlib'),
        # From Jamfile: SubDirSysHdrs math
        include_directories('../../math'),
        # From Jamfile: SubDirSysHdrs glibc root
        include_directories('../..'),
        # From Jamfile: SubDirHdrs arch/generic
        include_directories('../generic'),
        # From Jamfile: UsePrivateHeaders libroot (included in haiku_config)
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        # From Jamfile: SubDirCcFlags -D_GNU_SOURCE -D_IEEE_LIBM -DPIC
        '-D_GNU_SOURCE',
        '-D_IEEE_LIBM',
        '-DPIC',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
        # From Jamfile: if $(OPTIM) = -O0 { OPTIM = -O ; } - ensure optimization
        '-O',
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_IEEE_LIBM',
        '-DPIC',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
        '-O',
    ],
    
    # From Jamfile: SubDirAsFlags -DPIC (added to c_args and cpp_args instead)
    # Note: Meson doesn't support asm_args for static_library, so -DPIC is in c_args above
    
    install: false
)

# Export for parent
glibc_arch_x86_64_dep = declare_dependency(
    link_with: glibc_arch_x86_64_lib
)

message('glibc arch x86_64 configured successfully')