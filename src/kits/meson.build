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

# Collect all kit object files (these are already built)
kit_objects_dir = haiku_config['kits_output_dir']

# Reference the actual kit object files in generated_meson
required_kit_objects = [
    files('../../generated_meson/objects/haiku/x86_64/release/kits/app/app_kit.o'),
    files('../../generated_meson/objects/haiku/x86_64/release/kits/storage/storage_kit.o'),
    files('../../generated_meson/objects/haiku/x86_64/release/kits/support/support_kit.o'),
    files('../../generated_meson/objects/haiku/x86_64/release/kits/interface/interface_kit.o'),
    files('../../generated_meson/objects/haiku/x86_64/release/kits/locale/locale_kit.o')
]

message('Using ' + required_kit_objects.length().to_string() + ' kit object files for libbe.so')

# Additional static libraries required for libbe.so
libs_output_dir = haiku_config['libs_output_dir']

# Add all required additional libraries as in Jamfile
additional_libs = [
    files('../../generated_meson/objects/haiku/x86_64/release/libs/libicon.a'),
    files('../../generated_meson/objects/haiku/x86_64/release/libs/libagg.a'), 
    files('../../generated_meson/objects/haiku/x86_64/release/libs/libstorage_kit_mime.a')
]
message('Attempting libbe.so build with ' + additional_libs.length().to_string() + ' additional libraries')

# System dependencies
system_deps = [
    dependency('threads'),
    # ICU, zlib, zstd are provided by build packages via cross-compilation
]

# Create libbe.so shared library
# TODO: Use haiku_resources module when properly configured
# For now, temporarily skip resources to test core linking

# External dependencies (like Jam BuildFeatureAttribute)
icu_dep = dependency('icu-uc', required: false)
zlib_dep = dependency('zlib', required: false)
zstd_dep = dependency('libzstd', required: false)

# System dependencies
system_deps = [
    dependency('threads'),
]

# Add external deps if found
if icu_dep.found()
    system_deps += [icu_dep]
endif
if zlib_dep.found()
    system_deps += [zlib_dep]
endif
if zstd_dep.found()
    system_deps += [zstd_dep]
endif

libbe = shared_library('be',
    # TODO: Include compiled resource files when module is working
    # version_resource,
    # icons_resource, 
    # country_flags_resource,
    # language_flags_resource,
    
    link_language: 'cpp',  # Use C++ linking for proper Haiku integration
    objects: required_kit_objects,  # Kit .o files go in objects
    link_with: additional_libs,     # Library .a files go in link_with
    link_args: [
        # Add version script (like Jamfile SetVersionScript) 
        '-Wl,--version-script=' + meson.current_source_dir() + '/libbe_versions',
        # Use static libstdc++ for cross-compilation
        '-static-libstdc++',
        '-static-libgcc',
        # Haiku-specific linking flags
        '-Xlinker', '-soname=libbe.so.1',
        # Enable symbol versioning
        '-Wl,--default-symver',
    ],
    include_directories: haiku_config['get_all_include_dirs'],
    dependencies: system_deps,
    version: '1.0.0',
    soversion: '1',
    install: true,
    install_dir: 'generated_meson/lib'
)

message('libbe.so configured successfully with ' + required_kit_objects.length().to_string() + ' kit objects')