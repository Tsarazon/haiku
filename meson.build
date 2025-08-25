# Haiku OS - Meson Build System
# Root build configuration integrating with existing Jam build system

project('haiku', 'c',
    version: '1.0.0-alpha',
    default_options: [
        'c_std=c11',
        'warning_level=1',
        'optimization=2',
        'default_library=static'
    ],
    meson_version: '>=0.63.0'
)

# Subproject stdlib override removed - causes include path conflicts
# Using direct cross-compiler configuration instead

# Add C++ support after stdlib override
add_languages('cpp', required: false, native: false)

message('=== Haiku OS Meson Build System ===')

# Import Python modules for Haiku configuration
python_mod = import('python')
python = python_mod.find_installation('python3', required: true)

# Execute HaikuCommon.py to get configuration
haiku_config_result = run_command(
    python, 
    meson.current_source_dir() / 'build' / 'meson' / 'modules' / 'HaikuCommon.py',
    check: true,
    capture: true
)

if haiku_config_result.returncode() != 0
    error('Failed to initialize Haiku configuration')
endif

message('Haiku configuration initialized successfully')

# File system module  
fs = import('fs')

# Add custom modules directory
add_languages('cpp', required: false)

# Haiku root directory
haiku_root = meson.current_source_dir()

# Configuration variables
target_arch = get_option('target_arch')
run_without_registrar = get_option('run_without_registrar')  
run_without_app_server = get_option('run_without_app_server')

# Cross-compilation toolchain options
use_jam_cross = get_option('use_jam_cross')
jam_cross_tools_dir = get_option('jam_cross_tools_dir')
external_cross_tools_dir = get_option('external_cross_tools_dir')
auto_fix_cross_tools = get_option('auto_fix_cross_tools')

# Proper Jam-style output directories
objects_base = haiku_root + '/generated_meson/objects/haiku/' + target_arch + '/release'
kits_output_dir = objects_base + '/kits'
libs_output_dir = objects_base + '/libs'

message('Target architecture: ' + target_arch)
message('Objects base directory: ' + objects_base)
message('Kits output directory: ' + kits_output_dir)
message('Libs output directory: ' + libs_output_dir)

# Cross-tools configuration and compatibility
message('Cross-tools configuration:')
message('  Use Jam cross-tools: ' + use_jam_cross.to_string())
if jam_cross_tools_dir != ''
    message('  Jam cross-tools directory: ' + jam_cross_tools_dir)
endif
if external_cross_tools_dir != ''
    message('  External cross-tools directory: ' + external_cross_tools_dir)
endif
message('  Auto-fix compatibility: ' + auto_fix_cross_tools.to_string())

# Auto-fix cross-tools compatibility if enabled
if auto_fix_cross_tools
    cross_tools_fix = run_command(python, 
        files('build/meson/modules/detect_build_features.py'),
        '--fix-cross-tools', 
        '--architecture', target_arch,
        check: false)
    if cross_tools_fix.returncode() == 0
        message('✅ Cross-tools compatibility ensured')
    else
        warning('⚠️ Cross-tools compatibility check failed: ' + cross_tools_fix.stderr())
    endif
endif

# Create a Python-based configuration object (simulated)
# In a real implementation, this would use the Python module
class_haiku_config = {
    'haiku_root': haiku_root,
    'target_arch': target_arch,
    'generated_meson_dir': haiku_root + '/generated_meson',
}

# Header paths based on CMakeLists.txt analysis
# Order matters: private headers first, then public (to avoid conflicts)

# Private headers (highest precedence)
private_header_paths = [
    'headers/private/app',
    'headers/private/interface',
    'headers/private/input',
    'headers/private/kernel',
    'headers/private/libroot',
    'headers/private/locale',
    'headers/private/media',
    'headers/private/print',
    'headers/private/shared',
    'headers/private/support',
    'headers/private/storage',
    'headers/private/tracker',
    'headers/private',
    'headers/private/system',
    'headers/private/system/arch/' + target_arch,
    # Build headers (include redirectors)
    'headers/build/private/support',
]

# Compatibility headers
compatibility_header_paths = [
    'headers/glibc',
    'headers/posix',
]

# Public OS headers (last to avoid conflicts)
public_header_paths = [
    'headers',
    'headers/os',
    'headers/os/add-ons',
    'headers/os/add-ons/graphics',
    'headers/os/add-ons/input_server',
    'headers/os/add-ons/registrar',
    'headers/os/add-ons/screen_saver',
    'headers/os/add-ons/tracker',
    'headers/os/app',
    'headers/os/device',
    'headers/os/drivers',
    'headers/os/game',
    'headers/os/interface',
    'headers/os/kernel',
    'headers/os/locale',
    'headers/os/media',
    'headers/os/mail',
    'headers/os/midi',
    'headers/os/midi2',
    'headers/os/net',
    'headers/os/storage',
    'headers/os/support',
    'headers/os/translation',
]

# Library headers
library_header_paths = [
    'headers/libs/agg',
    'headers/libs/icon',
]

# Build configuration headers
build_config_paths = [
    'build/config_headers',
    'src/kits/tracker',
]

# Complete header configuration matching CMakeLists.txt order
all_header_paths = private_header_paths + compatibility_header_paths + public_header_paths + library_header_paths + build_config_paths

# Pre-create include directories (at root level where paths are correct)
all_include_dirs = []
foreach header_path : all_header_paths
    if fs.is_dir(header_path)
        all_include_dirs += include_directories(header_path)
    else
        message('Info: Header directory not found: ' + header_path)
    endif
endforeach

message('Total include directories configured: ' + all_include_dirs.length().to_string())

# Simple haiku_config object (preserves CMakeLists.txt header order)
haiku_config = {
    'get_library_headers': 'headers/libs/icon',
    'get_private_headers': private_header_paths,
    'get_public_headers': public_header_paths,
    'get_all_headers': all_header_paths,
    'get_all_include_dirs': all_include_dirs,
    'objects_base': objects_base,
    'kits_output_dir': kits_output_dir,
    'libs_output_dir': libs_output_dir,
}

# Build subdirectories
message('Building Haiku OS components...')

# Build system glue objects FIRST (required for linking)
subdir('src/system/glue')

# Build libroot SECOND (required by everything else)
subdir('src/system/libroot')

# Build support libraries (required by kits)
subdir('src/libs')

# Build all Haiku kits (depends on libraries)
subdir('src/kits')

# Future: Add other major Haiku components
# subdir('src/servers')      # System servers
# subdir('src/add-ons')      # Add-ons and plugins
# subdir('src/bin')          # System binaries
# subdir('src/preferences')  # Preference applications

message('Haiku Meson build configuration complete')