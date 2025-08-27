# Haiku libroot POSIX glibc wcsmbs
# Corresponds to src/system/libroot/posix/glibc/wcsmbs/Jamfile

message('Building libroot POSIX glibc wcsmbs...')

# Architecture detection and get proper flags from HaikuCommon
target_arch = get_option('target_arch')

# Find Python3 program
python_prog = find_program('python3')

# Get architecture-specific flags using HaikuCommon ArchitectureRules
arch_flags_result = run_command(
    python_prog,
    '-c',
    '''import sys; sys.path.insert(0, "/home/ruslan/haiku/build/meson/modules");
from ArchitectureRules import ArchitectureConfig;
arch = ArchitectureConfig("''' + target_arch + '''");
flags = arch.get_c_flags() + arch.get_defines() + ["-std=gnu11", "-D_GNU_SOURCE", "-DUSE_IN_LIBIO", "-D_DEFAULT_SOURCE", "-DBUILDING_LIBROOT=1"];
print(";".join(flags))''',
    capture: true,
    check: true
)

arch_c_flags = arch_flags_result.stdout().strip().split(';')

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
        # From Jamfile: SubDirSysHdrs include/arch/TARGET_ARCH (must come first)
        include_directories('../include/arch/' + target_arch),
        # From Jamfile: SubDirSysHdrs include/arch/generic
        include_directories('../include/arch/generic'),
        # From Jamfile: SubDirSysHdrs include (glibc headers must come before system)
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
        # From Jamfile: SubDirSysHdrs glibc root
        include_directories('..'),
        # Haiku system headers come after glibc
        haiku_config['get_all_include_dirs'],
    ],
    
    c_args: arch_c_flags + [
        '-fno-builtin',                  # Override built-ins (from Jamfile)
        '-Wno-unused-function',          # Suppress unused function warnings
        '-Wno-missing-prototypes',       # Suppress missing prototypes warnings
        # Add compiler built-in headers for float.h, stdarg.h, etc.
        '-isystem/home/ruslan/haiku/generated/cross-tools-x86_64/lib/gcc/x86_64-unknown-haiku/13.3.0/include',
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