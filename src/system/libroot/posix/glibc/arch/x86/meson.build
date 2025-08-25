# Haiku libroot POSIX glibc x86 architecture implementation
# Converted from Jamfile

# Only build if target architecture is x86
if target_arch == 'x86'

    # Generic sources from arch/generic
    generic_sources = files([
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

    # x86-specific sources
    x86_sources = files([
        'add_n.S',
        'addmul_1.S',
        'ldbl2mpn.c',
        'mul_1.S',
        'lshift.S',
        'rshift.S',
        'sub_n.S',
        'submul_1.S',
    ])

    # x86 architecture object library for glibc
    posix_glibc_arch_x86_lib = static_library('posix_glibc_arch_x86',
        [x86_sources, generic_sources],
        
        include_directories: [
            include_directories('../../include/arch/x86'),
            include_directories('../../include/arch/generic'),
            include_directories('../../include'),
            include_directories('../../libio'),
            include_directories('../../stdlib'),
            include_directories('../../math'),
            include_directories('../..'),
            include_directories('../generic'),
        ],
        
        c_args: [
            '-fno-builtin',
            '-fno-strict-aliasing',
            '-D_GNU_SOURCE',
            '-D_IEEE_LIBM',
            '-O',  # Force -O optimization as per Jamfile
            '-D_DEFAULT_SOURCE',
            '-DBUILDING_LIBROOT=1',
            '-DARCH_x86',
            '-D__BEGIN_DECLS=',
            '-D__END_DECLS=',
            '-D_LIBC=1',
            '-DHAVE_CONFIG_H=1',
            '-DSHARED=1',
            '-DDO_VERSIONING=1',
        ],
        
        install: false
    )

    # Export for parent
    posix_glibc_arch_x86_dep = declare_dependency(
        link_with: posix_glibc_arch_x86_lib
    )

else
    # Create empty dependency for non-x86 builds
    posix_glibc_arch_x86_dep = declare_dependency()
endif