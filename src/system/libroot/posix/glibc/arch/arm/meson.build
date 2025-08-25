# Haiku libroot POSIX glibc arm architecture implementation
# Converted from Jamfile

# Only build if target architecture is arm
if target_arch == 'arm'

    # Generic sources from arch/generic (ARM uses more C sources, fewer ASM)
    generic_sources = files([
        '../generic/cmp.c',
        '../generic/dbl2mpn.c', 
        '../generic/divrem.c',
        '../generic/mpn2dbl.c',
        '../generic/mpn2flt.c',
        '../generic/mpn2ldbl.c',
        '../generic/mul.c',
        '../generic/mul_n.c',
        '../generic/add_n.c',
        '../generic/addmul_1.c',
        '../generic/ldbl2mpn.c',
        '../generic/mul_1.c',
        '../generic/lshift.c',
        '../generic/rshift.c',
        '../generic/sub_n.c',
        '../generic/submul_1.c',
        '../generic/s_cacos.c',
        '../generic/s_cacosf.c',
        # s_cacosl.c commented out in Jamfile
        # s_cacoshl.c commented out in Jamfile
        '../generic/s_casin.c',
        '../generic/s_casinf.c',
        # s_casinl.c commented out in Jamfile
        '../generic/s_casinh.c',
        '../generic/s_casinhf.c',
        # s_casinhl.c commented out in Jamfile
        # s_ccoshl.c commented out in Jamfile
        '../generic/s_cexp.c',
        '../generic/s_cexpf.c',
        # s_cexpl.c commented out in Jamfile
        '../generic/s_clog.c',
        '../generic/s_clogf.c',
        # s_clogl.c commented out in Jamfile
        '../generic/s_clog10.c',
        '../generic/s_clog10f.c',
        # s_clog10l.c commented out in Jamfile
        '../generic/s_cpow.c',
        '../generic/s_cpowf.c',
        # s_cpowl.c commented out in Jamfile
        # s_csinhl.c commented out in Jamfile
        # s_csqrtl.c commented out in Jamfile
        # s_ctanhl.c commented out in Jamfile
    ])

    # ARM architecture object library for glibc
    posix_glibc_arch_arm_lib = static_library('posix_glibc_arch_arm',
        generic_sources,
        
        include_directories: [
            include_directories('../../include/arch/arm'),
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
            '-DPIC',
            '-O',  # Force -O optimization as per Jamfile
            '-D_DEFAULT_SOURCE',
            '-DBUILDING_LIBROOT=1',
            '-DARCH_arm',
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
    posix_glibc_arch_arm_dep = declare_dependency(
        link_with: posix_glibc_arch_arm_lib
    )

else
    # Create empty dependency for non-arm builds
    posix_glibc_arch_arm_dep = declare_dependency()
endif