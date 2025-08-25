# Haiku libroot POSIX musl complex
# Corresponds to src/system/libroot/posix/musl/complex/Jamfile

message('Building libroot POSIX musl complex...')

# Architecture detection
target_arch = get_option('target_arch')

# Musl complex sources (from Jamfile MergeObject)
posix_musl_complex_sources = files([
    '__cexp.c', '__cexpf.c',
    'cabs.c', 'cabsf.c', 'cabsl.c',
    'cacosh.c', 'cacoshf.c',
    'carg.c', 'cargf.c', 'cargl.c',
    'catan.c', 'catanf.c', 'catanl.c',
    'catanh.c', 'catanhf.c', 'catanhl.c',
    'ccos.c', 'ccosf.c', 'ccosl.c',
    'ccosh.c', 'ccoshf.c',
    'cexp.c', 'cexpf.c',
    'cimag.c', 'cimagf.c', 'cimagl.c',
    'conj.c', 'conjf.c', 'conjl.c',
    'cproj.c', 'cprojf.c', 'cprojl.c',
    'creal.c', 'crealf.c', 'creall.c',
    'csin.c', 'csinf.c', 'csinl.c',
    'csinh.c', 'csinhf.c',
    'csqrt.c', 'csqrtf.c',
    'ctan.c', 'ctanf.c', 'ctanl.c',
    'ctanh.c', 'ctanhf.c',
])

# Build musl complex object library
posix_musl_complex_lib = static_library('posix_musl_complex',
    posix_musl_complex_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: SubDirSysHdrs [ FDirName $(SUBDIR) .. include ]
        include_directories('../include'),
        # From Jamfile: UseHeaders [ FDirName $(SUBDIR) .. internal ]
        include_directories('../internal'),
        # From Jamfile: UseHeaders [ FDirName $(SUBDIR) .. arch $(TARGET_ARCH) ]
        include_directories('../arch/' + target_arch),
        # From Jamfile: UseHeaders [ FDirName $(SUBDIR) .. arch generic ]
        include_directories('../arch/generic'),
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
posix_musl_complex_dep = declare_dependency(
    link_with: posix_musl_complex_lib
)

message('libroot POSIX musl complex configured successfully')