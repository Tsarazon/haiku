# Haiku OS - Meson Build System
# Root build configuration integrating with existing Jam build system

project('haiku', ['c', 'cpp'],
    version: '1.0.0-alpha',
    default_options: [
        'cpp_std=c++17',
        'c_std=c11',
        'warning_level=1',
        'optimization=2',
        'default_library=static'
    ],
    meson_version: '>=0.63.0'
)

message('=== Haiku OS Meson Build System ===')

# Import Python modules for Haiku configuration
python_mod = import('python')
python = python_mod.find_installation('python3', required: true)

# Execute HaikuCommon.py to get configuration
haiku_config_result = run_command(
    python, 
    meson.current_build_dir() / 'modules' / 'HaikuCommon.py',
    check: true,
    capture: true
)

if haiku_config_result.returncode() != 0
    error('Failed to initialize Haiku configuration')
endif

message('Haiku configuration initialized successfully')

# File system module
fs = import('fs')

# Haiku root directory
haiku_root = meson.current_source_dir()

# Configuration variables
target_arch = get_option('target_arch')
run_without_registrar = get_option('run_without_registrar')  
run_without_app_server = get_option('run_without_app_server')

# Proper Jam-style output directories
objects_base = haiku_root + '/generated_meson/objects/haiku/' + target_arch + '/release'
kits_output_dir = objects_base + '/kits'
libs_output_dir = objects_base + '/libs'

message('Target architecture: ' + target_arch)
message('Objects base directory: ' + objects_base)
message('Kits output directory: ' + kits_output_dir)
message('Libs output directory: ' + libs_output_dir)

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

# Build all Haiku kits
subdir('src/kits')

# Build support libraries
subdir('src/libs')

# Future: Add other major Haiku components
# subdir('src/servers')      # System servers
# subdir('src/add-ons')      # Add-ons and plugins
# subdir('src/bin')          # System binaries
# subdir('src/preferences')  # Preference applications

message('Haiku Meson build configuration complete')