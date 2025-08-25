# Haiku libroot POSIX glibc regex
# Corresponds to src/system/libroot/posix/glibc/regex/Jamfile

message('Building libroot POSIX glibc regex...')

# Architecture detection
target_arch = get_option('target_arch')

# Glibc regex sources (from Jamfile MergeObject)
glibc_regex_sources = files([
    'regex.c',  # Only regex.c in Jamfile, not the full list from meson.build
])

# Build glibc regex object library
glibc_regex_lib = static_library('glibc_regex',
    glibc_regex_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # From Jamfile: SubDirHdrs $(HAIKU_TOP) headers (included in haiku_config)
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_REGEX_RE_COMP',   # From Jamfile: SubDirCcFlags -D_REGEX_RE_COMP -D_DEFAULT_SOURCE
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_REGEX_RE_COMP',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
glibc_regex_dep = declare_dependency(
    link_with: glibc_regex_lib
)

message('glibc regex configured successfully')