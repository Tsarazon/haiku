# Haiku libroot POSIX glibc riscv64 architecture implementation
# Converted from Jamfile

# Only build if target architecture is riscv64
if target_arch == 'riscv64'

    # Generic sources from arch/generic (RISC-V has full support)
    generic_sources = files([
        '../generic/lshift.c',
        '../generic/rshift.c',
        '../generic/submul_1.c',
        '../generic/add_n.c',
        '../generic/sub_n.c',
        '../generic/addmul_1.c',
        '../generic/mul_1.c',
        '../generic/cmp.c',
        '../generic/dbl2mpn.c', 
        '../generic/divrem.c',
        '../generic/mpn2dbl.c',
        '../generic/mpn2flt.c',
        '../generic/mpn2ldbl.c',
        '../generic/mul.c',
        '../generic/mul_n.c',
        '../generic/ldbl2mpn.c',
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

    # RISCV64 architecture object library for glibc
    posix_glibc_arch_riscv64_lib = static_library('posix_glibc_arch_riscv64',
        generic_sources,
        
        include_directories: [
            include_directories('../../include/arch/riscv64'),
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
            '-DARCH_riscv64',
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
    posix_glibc_arch_riscv64_dep = declare_dependency(
        link_with: posix_glibc_arch_riscv64_lib
    )

else
    # Create empty dependency for non-riscv64 builds
    posix_glibc_arch_riscv64_dep = declare_dependency()
endif