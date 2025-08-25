# Haiku libroot OS layer - Meson build
# This corresponds to src/system/libroot/os/Jamfile

message('Building libroot OS layer...')

# Architecture detection
target_arch = get_option('target_arch')

# Generate syscalls.S.inc from syscalls.h
python_prog = find_program('python3')
syscalls_generator = custom_target('syscalls_inc',
    input: '../../../../headers/private/system/syscalls.h',
    output: 'syscalls.S.inc',
    command: [python_prog, meson.project_source_root() + '/build/meson/scripts/generate_syscalls.py', '@INPUT@', '@OUTPUT@'],
    build_by_default: true
)

# OS layer sources (from Jamfile MergeObject os_main.o)
os_sources = files([
    'Architecture.cpp',
    'area.c',
    'atomic.c', 
    'debug.c',
    'driver_settings.cpp',
    'extended_system_info.cpp',
    'find_directory.cpp',
    'find_paths.cpp',
    'fs_attr.cpp',
    'fs_index.c',
    'fs_info.cpp',
    'fs_query.cpp',
    'fs_volume.c',
    'image.cpp',
    'launch.cpp',
    'memory.cpp',
    'parsedate.cpp',
    'port.c',
    'scheduler.c',
    'sem.c',
    'stack_protector.cpp',
    'syscalls.S',
    'system_info.cpp',
    'system_revision.c',
    'team.c',
    'thread.c',
    'time.cpp',
    'wait_for_objects.cpp',
])

# Architecture-specific subdirectory
subdir('arch/' + target_arch)

# Build OS layer object library
os_main_lib = static_library('os_main',
    os_sources,
    
    # Add generated syscalls as dependency
    dependencies: [declare_dependency(sources: syscalls_generator)],
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # Add build directory for generated syscalls.S.inc
        include_directories('.'),
        # Ensure runtime_loader headers are found
        include_directories('../../../../headers/private/runtime_loader'),
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-D__HAIKU_PRIMARY_PACKAGING_ARCH="x86_64"',
        '-DARCH_' + target_arch,
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing', 
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-D__HAIKU_PRIMARY_PACKAGING_ARCH="x86_64"',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export the library for use by parent
os_main_dep = declare_dependency(
    link_with: os_main_lib,
    include_directories: include_directories('.')
)

message('libroot OS layer configured successfully')