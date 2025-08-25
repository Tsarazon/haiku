# Haiku System Glue - Runtime Startup Objects
# These are essential startup objects needed for dynamic libraries and executables

# Architecture detection
target_arch = get_option('target_arch')

# Common glue sources
glue_common_sources = files([
    'init_term_dyn.c',
    'start_dyn.c',
    'haiku_version_glue.c',
])

# Build init_term_dyn.o
init_term_dyn_obj = static_library('init_term_dyn',
    'init_term_dyn.c',
    include_directories: haiku_config.get('get_all_include_dirs'),
    c_args: ['-fno-pic', '-fno-pie'],
    pic: false,
    install: false
)

init_term_dyn_extracted = custom_target('init_term_dyn.o',
    input: init_term_dyn_obj,
    output: 'init_term_dyn.o',
    command: ['cp', '@INPUT@.p/init_term_dyn.c.o', '@OUTPUT@'],
    build_by_default: true,
    install: false
)

# Build crti.o (C runtime initialization) - architecture specific
if target_arch == 'x86_64'
    crti_source = 'arch/x86_64/crti.S'
    crtn_source = 'arch/x86_64/crtn.S'
elif target_arch == 'x86'
    crti_source = 'arch/x86/crti.S'
    crtn_source = 'arch/x86/crtn.S'
else
    error('Unsupported architecture for glue objects: ' + target_arch)
endif

crti_obj = static_library('crti',
    crti_source,
    include_directories: haiku_config.get('get_all_include_dirs'),
    c_args: ['-fno-pic', '-fno-pie'],
    pic: false,
    install: false
)

crti_extracted = custom_target('crti.o',
    input: crti_obj,
    output: 'crti.o',
    command: ['cp', '@INPUT@.p/arch_x86_64_crti.S.o', '@OUTPUT@'],
    build_by_default: true,
    install: false
)

# Build crtn.o (C runtime termination)
crtn_obj = static_library('crtn',
    crtn_source,
    include_directories: haiku_config.get('get_all_include_dirs'),
    c_args: ['-fno-pic', '-fno-pie'],
    pic: false,
    install: false
)

crtn_extracted = custom_target('crtn.o',
    input: crtn_obj,
    output: 'crtn.o',
    command: ['cp', '@INPUT@.p/arch_x86_64_crtn.S.o', '@OUTPUT@'],
    build_by_default: true,
    install: false
)

# Build start_dyn.o
start_dyn_obj = static_library('start_dyn',
    'start_dyn.c',
    include_directories: haiku_config.get('get_all_include_dirs'),
    c_args: ['-fno-pic', '-fno-pie'],
    pic: false,
    install: false
)

start_dyn_extracted = custom_target('start_dyn.o',
    input: start_dyn_obj,
    output: 'start_dyn.o',
    command: ['cp', '@INPUT@.p/start_dyn.c.o', '@OUTPUT@'],
    build_by_default: true,
    install: false
)

# Build haiku_version_glue.o
haiku_version_glue_obj = static_library('haiku_version_glue',
    'haiku_version_glue.c',
    include_directories: haiku_config.get('get_all_include_dirs'),
    c_args: ['-fno-pic', '-fno-pie'],
    pic: false,
    install: false
)

haiku_version_glue_extracted = custom_target('haiku_version_glue.o',
    input: haiku_version_glue_obj,
    output: 'haiku_version_glue.o',
    command: ['cp', '@INPUT@.p/haiku_version_glue.c.o', '@OUTPUT@'],
    build_by_default: true,
    install: false
)

# Export the glue objects for use by other targets
glue_objects_dep = declare_dependency(
    link_args: [
        meson.current_build_dir() + '/crti.o',
        meson.current_build_dir() + '/init_term_dyn.o',
    ]
)

# Also export individual objects for specific use cases
glue_crti = crti_extracted
glue_crtn = crtn_extracted
glue_init_term_dyn = init_term_dyn_extracted
glue_start_dyn = start_dyn_extracted
glue_haiku_version = haiku_version_glue_extracted

message('System glue objects configured for ' + target_arch)