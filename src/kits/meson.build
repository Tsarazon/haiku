# Haiku Kits - Meson Build Configuration
# This builds all Haiku kits (app, storage, support, interface, etc.)

message('Building Haiku kits...')

# App Kit - Core application framework
subdir('app')

# Storage Kit - File system and storage APIs  
subdir('storage')

# Support Kit - Utility classes and support functionality
subdir('support')

# Interface Kit - GUI components and user interface
subdir('interface')

# Locale Kit - Internationalization and localization
subdir('locale')
# subdir('media')      # Media Kit - Audio/video processing
# subdir('network')    # Network Kit - Networking APIs
# subdir('game')       # Game Kit - Game development support
# subdir('device')     # Device Kit - Hardware device access
# subdir('midi')       # MIDI Kit - MIDI support
# subdir('opengl')     # OpenGL Kit - 3D graphics
# subdir('translation') # Translation Kit - File format conversion

message('All requested Haiku kits configured successfully')

# Build libbe.so shared library from all kit objects and libraries
message('Building libbe.so shared library...')

# Additional static libraries required for libbe.so
# The libraries are built via the main meson.build -> src/libs
# We reference them by declaring the targets as dependencies

# Reference actual library targets instead of file paths
additional_libs = [
    libicon,
    libagg,
    libstorage_kit_mime
]

# ICU libraries are now handled via system_deps
message('Attempting libbe.so build with ' + additional_libs.length().to_string() + ' additional libraries')

# System dependencies will be defined later after external deps

# Create libbe.so shared library
# Use HaikuCommon for unified resource handling
python_prog = find_program('python3')
haiku_common_script = meson.current_source_dir() + '/../../build/meson/modules/HaikuCommon.py'

# Get rc compiler path using HaikuCommon
rc_path_result = run_command(
    python_prog,
    '-c',
    'from sys import path; path.insert(0, "' + meson.current_source_dir() + '/../../build/meson/modules"); from HaikuCommon import get_rc_compiler_path; rc = get_rc_compiler_path(); print(rc if rc else "")',
    capture: true,
    check: true
)

rc_compiler = rc_path_result.stdout().strip()
if rc_compiler == ''
    error('❌ RC compiler not found - ensure rc compiler is built')
else
    message('✅ RC compiler found: ' + rc_compiler)
endif

# Create standard libbe.so resources using ResourceHandler-detected rc compiler
version_resource = custom_target('libbe_version_rsrc',
    input: 'libbe_version.rdef',
    output: 'libbe_version.rsrc',
    command: [rc_compiler, '-o', '@OUTPUT@', '@INPUT@'],
    build_by_default: true
)

icons_resource = custom_target('icons_rsrc',
    input: 'Icons.rdef',
    output: 'Icons.rsrc', 
    command: [rc_compiler, '-o', '@OUTPUT@', '@INPUT@'],
    build_by_default: true
)

country_flags_resource = custom_target('country_flags_rsrc',
    input: 'CountryFlags.rdef',
    output: 'CountryFlags.rsrc',
    command: [rc_compiler, '-o', '@OUTPUT@', '@INPUT@'],
    build_by_default: true
)

language_flags_resource = custom_target('language_flags_rsrc',
    input: 'LanguageFlags.rdef',
    output: 'LanguageFlags.rsrc',
    command: [rc_compiler, '-o', '@OUTPUT@', '@INPUT@'],
    build_by_default: true
)

# External dependencies using BuildFeature detection (like Jam BuildFeatureAttribute)
# This mimics the Jam build system's dynamic package detection

# Auto-detect build packages (equivalent to IsPackageAvailable in Jam)
build_features_detect = run_command(
    python_prog, 
    meson.current_source_dir() + '/../../build/meson/modules/detect_build_features.py',
    capture: true,
    check: true
)

if build_features_detect.returncode() == 0
    message('Build package detection successful')
    # Packages are available - create dependencies with detected paths
    
    # ZLIB dependency (BuildFeatureAttribute zlib : library)
    zlib_dep_manual = declare_dependency(
        link_args: [
            '-L/home/ruslan/haiku/generated/build_packages/zlib-1.3.1-3-x86_64/develop/lib',
            '-lz'
        ]
    )
    
    # ZSTD dependency (BuildFeatureAttribute zstd : library)  
    zstd_dep_manual = declare_dependency(
        link_args: [
            '-L/home/ruslan/haiku/generated/build_packages/zstd-1.5.6-1-x86_64/develop/lib',
            '-lzstd'
        ]
    )
    
    # ICU dependency (BuildFeatureAttribute icu : libraries)
    # Match exact Jam build linking: libicudata, libicui18n, libicuio, libicuuc
    icu_lib_path = '/home/ruslan/haiku/generated/build_packages/icu74-74.1-3-x86_64/develop/lib'
    icu_dep_manual = declare_dependency(
        link_args: [
            '-L' + icu_lib_path,
            # Link each library explicitly to match Jam output exactly
            icu_lib_path + '/libicudata.so',
            icu_lib_path + '/libicui18n.so',
            icu_lib_path + '/libicuio.so', 
            icu_lib_path + '/libicuuc.so',
            '-lstdc++'
        ]
    )
    
    message('External dependencies configured: zlib, zstd, icu (from build packages)')
else
    error('Failed to detect build packages - ensure packages are extracted')
endif

# System dependencies (matching Jam BuildFeatureAttribute pattern)
system_deps = [
    dependency('threads'),
    zlib_dep_manual,   # [ BuildFeatureAttribute zlib : library ]
    zstd_dep_manual,   # [ BuildFeatureAttribute zstd : library ]
    icu_dep_manual     # [ BuildFeatureAttribute icu : libraries ]
]

# Shared source files (from Jamfile SEARCH_SOURCE)
shared_sources = files([
    'shared/DragTrackingFilter.cpp',
    'shared/HSL.cpp',
])

libbe = shared_library('be',
    # Include shared source files directly
    shared_sources,
    
    # Include compiled resource files (like Jam AddResources)
    version_resource,
    icons_resource, 
    country_flags_resource,
    language_flags_resource,
    
    # Include kit object files and glue objects as objects parameter
    objects: [
        app_kit_merged,
        storage_kit_merged,
        support_kit_merged,
        interface_kit_merged,
        locale_kit_merged,
        # Add glue objects as additional objects
        glue_crti,
        glue_init_term_dyn,
    ],
    
    link_language: 'cpp',  # Use C++ linking for proper Haiku integration
    link_with: [
        additional_libs,     # Library .a files go in link_with
        # Note: libroot is not linked - it's expected to be available at runtime
    ],
    cpp_args: [
        '-O2',                    # Optimization level 2 (matching Jam build)
        '-fno-strict-aliasing',   # Required for proper C++ semantics
    ],
    
    link_args: [
        # Disable automatic startup files and use our own
        '-nostartfiles',
        # Add version script (like Jamfile SetVersionScript) 
        '-Wl,--version-script=' + meson.current_source_dir() + '/libbe_versions',
        # Strip debug symbols for release build (matching Jam) - DISABLED TO TEST UNSTRIPPED SIZE
        # '-s',
        # Haiku-specific linking flags
        '-Xlinker', '-soname=libbe.so.1',
        # Enable symbol versioning
        '-Wl,--default-symver',
    ],
    include_directories: haiku_config['get_all_include_dirs'],
    dependencies: system_deps,
    version: '1.0.0',
    soversion: '1',
    install: false  # Keep libbe.so in builddir for testing
)

# Generate pkg-config file for libbe.so
pkg = import('pkgconfig')
pkg.generate(
    libraries: libbe,
    version: '1.0.0',
    name: 'libbe',
    filebase: 'libbe',
    description: 'Haiku Application Framework Library',
    subdirs: ['haiku'],
    requires: ['threads']
    # Note: requires_private would be ideal but these deps aren't system pkg-config packages
)

message('libbe.so configured successfully with pkg-config generation')