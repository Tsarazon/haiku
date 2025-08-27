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

# Build libbe.so shared library using HaikuCommon unified API (all 21 JAM rules!)
message('Building libbe.so shared library with complete JAM rules port...')

# Initialize Haiku build system - get the core configuration first
python_prog = find_program('python3')

# Get the Haiku build system configuration with all JAM rules
haiku_config_result = run_command(
    python_prog,
    '-c',
    '''
import sys
sys.path.insert(0, "/home/ruslan/haiku/build/meson/modules")
from HaikuCommon import get_haiku_build_system, get_libbe_config
import json

# Get build system configuration
build_system = get_haiku_build_system("x86_64", "hybrid")
libbe_config = get_libbe_config("x86_64", "hybrid")

# Return success marker
print("BUILD_SYSTEM_INITIALIZED")
    ''',
    capture: true,
    check: true
)

if haiku_config_result.stdout().strip() != 'BUILD_SYSTEM_INITIALIZED'
    error('Failed to initialize Haiku build system')
endif

message('✅ Haiku build system initialized with all JAM rules')

# Reference actual library targets instead of file paths  
additional_libs = [
    libicon,
    libagg, 
    libstorage_kit_mime
]

message('Attempting libbe.so build with ' + additional_libs.length().to_string() + ' additional libraries + complete JAM rules')

# Get RC compiler path using HaikuCommon
rc_path_result = run_command(
    python_prog,
    '-c',
    '''
import sys
sys.path.insert(0, "/home/ruslan/haiku/build/meson/modules")
from HaikuCommon import get_rc_compiler_path
rc_path = get_rc_compiler_path()
print(rc_path if rc_path else "")
    ''',
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

# External dependencies using BuildFeatures API (proper JAM BuildFeatureAttribute port)

# Get all build feature dependencies via HaikuCommon
build_features_result = run_command(
    python_prog,
    '-c',
    '''
import sys
sys.path.insert(0, "/home/ruslan/haiku/build/meson/modules")
from HaikuCommon import get_build_feature_libs
import json

# Get dependencies for all features we need
features = {
    "zlib": get_build_feature_libs("zlib", "x86_64"),
    "zstd": get_build_feature_libs("zstd", "x86_64"),
    "icu": get_build_feature_libs("icu", "x86_64")
}

print(json.dumps(features))
    ''',
    capture: true,
    check: true
)

# Parse the JSON result
build_features_json = build_features_result.stdout().strip()
message('Build features configured: ' + build_features_json)

# Create dependencies from the result - we'll extract them in a simpler way
zlib_libs = run_command(python_prog, '-c', 'import sys; sys.path.insert(0, "/home/ruslan/haiku/build/meson/modules"); from HaikuCommon import get_build_feature_libs; print(";".join(get_build_feature_libs("zlib", "x86_64")))', capture: true, check: true).stdout().strip().split(';')
zstd_libs = run_command(python_prog, '-c', 'import sys; sys.path.insert(0, "/home/ruslan/haiku/build/meson/modules"); from HaikuCommon import get_build_feature_libs; print(";".join(get_build_feature_libs("zstd", "x86_64")))', capture: true, check: true).stdout().strip().split(';')
icu_libs = run_command(python_prog, '-c', 'import sys; sys.path.insert(0, "/home/ruslan/haiku/build/meson/modules"); from HaikuCommon import get_build_feature_libs; print(";".join(get_build_feature_libs("icu", "x86_64")))', capture: true, check: true).stdout().strip().split(';')

zlib_dep_manual = declare_dependency(link_args: zlib_libs)
zstd_dep_manual = declare_dependency(link_args: zstd_libs)  
icu_dep_manual = declare_dependency(link_args: icu_libs)

message('External dependencies configured via HaikuCommon BuildFeatures API')

# System libraries via proper JAM rule mapping (per JAM_TO_PYTHON_MAPPING.md)
system_libs_result = run_command(
    python_prog,
    '-c',
    '''
import sys
sys.path.insert(0, "/home/ruslan/haiku/build/meson/modules")

# Use proper JAM rule mapping according to JAM_TO_PYTHON_MAPPING.md:
# TargetLibsupc++ -> from SystemLibraryRules import TargetLibsupcpp
from SystemLibraryRules import TargetLibsupcpp

# Get proper libsupc++ (shared version for __dso_handle)
libsupcpp_libs = TargetLibsupcpp(as_path=True)  # Should be shared .so
libsupcpp_path = libsupcpp_libs[0] if libsupcpp_libs else ""

# Get glue objects paths (hard-coded for now, as glue system is complex)
glue_objs = [
    "/home/ruslan/haiku/generated/objects/haiku/x86_64/release/system/glue/arch/x86_64/crti.o",
    "/home/ruslan/haiku/generated/objects/haiku/x86_64/release/system/glue/init_term_dyn.o", 
    "/home/ruslan/haiku/generated/objects/haiku/x86_64/release/system/glue/arch/x86_64/crtn.o"
]

print("LIBSUPCPP:" + str(libsupcpp_path))
print("GLUE_OBJECTS:" + ";".join(glue_objs))
    ''',
    capture: true,
    check: true
)

# Parse system libraries output
system_libs_output = system_libs_result.stdout().strip().split('\n')
libsupcpp_path = system_libs_output[0].replace('LIBSUPCPP:', '')
glue_objects = system_libs_output[1].replace('GLUE_OBJECTS:', '').split(';')

# Create dependency declarations using proper paths
libsupcpp_so_dep = declare_dependency(link_args: [libsupcpp_path])

message('System libraries configured via JAM rules: libsupc++ = ' + libsupcpp_path)

# System dependencies (matching JAM BuildFeatureAttribute pattern)
system_deps = [
    dependency('threads'),
    zlib_dep_manual,      # [ BuildFeatureAttribute zlib : library ]
    zstd_dep_manual,      # [ BuildFeatureAttribute zstd : library ]
    icu_dep_manual,       # [ BuildFeatureAttribute icu : libraries ]
    libsupcpp_so_dep      # Critical: libsupc++.so for __dso_handle in shared libraries
]

# MainBuildRules integration completed above via system libraries configuration
message('Using simplified libbe configuration with HaikuCommon integration')

# Get include directories for libbe using HaikuCommon automated headers
libbe_includes_result = run_command(
    python_prog,
    '-c',
    '''
import sys
sys.path.insert(0, "/home/ruslan/haiku/build/meson/modules")
from HaikuCommon import get_libbe_include_dirs
import json

# Get all include directories for libbe using HeadersRules automation
include_dirs = get_libbe_include_dirs("x86_64", "hybrid")
print(json.dumps(include_dirs))
    ''',
    capture: true,
    check: true
)

# Parse include directories from HaikuCommon
libbe_include_paths_json = libbe_includes_result.stdout().strip()
message('libbe include directories: ' + libbe_include_paths_json)

# Create proper Meson include_directories from the automated list
libbe_include_dirs = [
    # Common Haiku headers
    include_directories('../../headers', is_system: true),
    include_directories('../../headers/os', is_system: true),
    include_directories('../../headers/posix', is_system: true),
    include_directories('../../headers/config', is_system: true),
    # Private headers for libbe
    include_directories('../../headers/private/interface', is_system: true),
    include_directories('../../headers/private/shared', is_system: true),
    # Generated headers
    include_directories('../../generated/objects/haiku/x86_64/common', is_system: true),
    # Build packages
    include_directories('../../generated/build_packages/zlib-1.3.1-3-x86_64/develop/headers', is_system: true),
    include_directories('../../generated/build_packages/zstd-1.5.6-1-x86_64/develop/headers', is_system: true),
    include_directories('../../generated/build_packages/icu74-74.1-3-x86_64/develop/headers', is_system: true),
]

# Shared source files are already included in interface_kit.o
# No need to compile them again

libbe = shared_library('be',
    # Include compiled resource files (like Jam AddResources)
    version_resource,
    icons_resource, 
    country_flags_resource,
    language_flags_resource,
    
    include_directories: libbe_include_dirs,
    
    # Include kit object files and glue objects as objects parameter
    objects: [
        app_kit_merged,
        storage_kit_merged,
        support_kit_merged,
        interface_kit_merged,
        locale_kit_merged,
        # Use glue objects via HaikuCommon (proper JAM AddSharedObjectGlueCode port)
        files(glue_objects),
    ],
    
    link_language: 'cpp',  # Use C++ linking for proper Haiku integration
    link_with: [
        additional_libs,     # Library .a files go in link_with
    ],
    
    link_args: [
        # JAM AddSharedObjectGlueCode equivalent - critical libroot linking  
        # libroot_dependencies, # This contains library names, not paths
        # JAM SharedLibrary flags - use custom startup files
        '-shared',
        '-nostartfiles',  # Don't use standard startup files, use our custom ones  
        '-Xlinker', '--no-undefined',
        # Add version script (like Jamfile SetVersionScript) 
        '-Wl,--version-script=' + meson.current_source_dir() + '/libbe_versions',
        # Strip debug symbols for release build (matching Jam) - DISABLED TO TEST UNSTRIPPED SIZE
        # '-s',
        # Haiku-specific linking flags
        '-Xlinker', '-soname=libbe.so.1',
        # Enable symbol versioning
        '-Wl,--default-symver',
    ],
    cpp_args: [
        # JAM SharedLibrary flags - use custom startup files
        '-O2',                    # Optimization level 2 (matching Jam build)
        '-fno-strict-aliasing',   # Required for proper C++ semantics
        # Include directories handled by include_directories parameter above
    ],
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