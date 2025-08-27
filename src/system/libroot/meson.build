# Haiku libroot.so - Core C Library
# Modular Meson build using subdirectories (proper structure)

message('Building libroot.so with Meson (modular approach)...')

# Architecture detection
target_arch = get_option('target_arch')

# Build subdirectories (like Jamfile SubInclude)
subdir('os')
subdir('posix')

# Core libroot sources
libroot_core_sources = files([
    'libroot_init.c',
    'empty.c',
])

# Build libroot.so using dependencies from subdirectories
libroot = shared_library('root',
    libroot_core_sources,
    
    # Link with the OS and POSIX layers built in subdirectories
    dependencies: [
        os_main_dep,
        os_arch_dep, 
        posix_main_dep,
        posix_arch_dep,
        # All POSIX modules (from subdir calls in posix/meson.build)
        posix_crypt_dep,
        posix_glibc_dep,  # Re-enabled to provide stdio functions
        posix_locale_dep,
        posix_malloc_dep,
        posix_musl_dep,
        posix_pthread_dep,
        posix_signal_dep,
        posix_stdio_dep,
        posix_stdlib_dep,
        posix_string_dep,
        posix_sys_dep,
        posix_time_dep,
        posix_unistd_dep,
        posix_wchar_dep,
        posix_libstdthreads_dep,
    ],
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # Add specific private headers for libroot (from Jamfile UsePrivateHeaders)
        include_directories('../../../headers/private/runtime_loader'),
        include_directories('../../../headers/private/libroot'),
    ],
    
    c_args: [
        '-fno-builtin',                 # Don't use builtin functions
        '-fno-strict-aliasing',        # Required for proper C semantics
        '-D_GNU_SOURCE',               # Enable GNU extensions
        '-D_DEFAULT_SOURCE',           # Enable default feature set
        '-DBUILDING_LIBROOT=1',        # Mark that we're building libroot
        '-D__HAIKU_PRIMARY_PACKAGING_ARCH="x86_64"',  # Primary architecture
        '-DARCH_' + target_arch,       # Architecture define (ARCH_x86_64)
    ],
    
    cpp_args: [
        '-fno-builtin',                
        '-fno-strict-aliasing',        
        '-D_GNU_SOURCE',               
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-D__HAIKU_PRIMARY_PACKAGING_ARCH="x86_64"',  # Primary architecture  
        '-DARCH_' + target_arch,       # Architecture define (ARCH_x86_64)
    ],
    
    # Include glue objects since libroot needs them too
    objects: [
        glue_crti,
        glue_init_term_dyn,
    ],
    
    link_args: [
        # Disable automatic startup files - we provide our own via objects
        '-nostartfiles',
        # Don't auto-link system libraries (including -lroot) - we ARE libroot
        '-nodefaultlibs',
        # Set soname  
        '-Wl,-soname,libroot.so',
        # Add version script if it exists
        fs.exists('libroot_versions') ? '-Wl,--version-script=' + meson.current_source_dir() + '/libroot_versions' : '',
        # Enable symbol versioning
        '-Wl,--default-symver',
    ],
    
    # libroot doesn't link against anything - it IS the base library
    install: false,  # Keep in builddir for testing
    
    version: '1.0.0',
    soversion: '1',
)

message('libroot.so configured successfully (built from source with ' + (libroot_core_sources.length() + os_sources.length() + posix_sources.length() + arch_sources.length()).to_string() + ' source files)')