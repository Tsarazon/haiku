# Haiku gensyscalls tool - Meson build
# This tool generates syscall-related files from syscalls.h

# First, we need to build gensyscallinfos tool
gensyscallinfos_sources = files([
    'gensyscallinfos.cpp'
])

# Build gensyscallinfos for the build machine (not cross-compiled)
gensyscallinfos = executable('gensyscallinfos',
    gensyscallinfos_sources,
    native: true,
    install: false
)

# Preprocess syscalls.h header
syscalls_h = files('../../../headers/private/system/syscalls.h')

# Get the cross-compiler for preprocessing
haiku_cpp = find_program(get_option('haiku_cross_cpp'), required: false)
if not haiku_cpp.found()
    haiku_cpp = find_program('/home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-gcc')
endif

# Preprocess syscalls.h to make it parsable
syscalls_pp_parsable = custom_target('syscalls_pp_parsable',
    input: syscalls_h,
    output: 'syscalls.h.pp.parsable',
    command: [
        haiku_cpp, '-xc++', '-E',
        '-DGEN_SYSCALL_INFOS_PROCESSING',
        '-I/home/ruslan/haiku/headers/private/system',
        '-I/home/ruslan/haiku/headers/private/system/arch/x86_64',
        '-I/home/ruslan/haiku/headers/private/kernel',
        '-I/home/ruslan/haiku/headers/posix',
        '@INPUT@', '-o', '@OUTPUT@'
    ]
)

# Generate gensyscalls_infos.cpp and syscall_types_sizes.h.cpp
gensyscalls_infos_gen = custom_target('gensyscalls_infos',
    input: syscalls_pp_parsable,
    output: ['gensyscalls_infos.cpp', 'syscall_types_sizes.h.cpp'],
    command: [gensyscallinfos, '@INPUT@', '@OUTPUT@']
)

# Generate syscall_types_sizes.h from syscall_types_sizes.h.cpp
# This would need the CreateAsmStructOffsetsHeader rule equivalent
# For now, use the pre-generated one from JAM build
syscall_types_sizes_h = custom_target('syscall_types_sizes_h',
    input: gensyscalls_infos_gen[1],  # syscall_types_sizes.h.cpp
    output: 'syscall_types_sizes.h',
    command: [
        haiku_cpp,
        '-I/home/ruslan/haiku/src/tools/gensyscalls/arch/x86_64',
        '-I/home/ruslan/haiku/headers/private/system',
        '-I/home/ruslan/haiku/headers/private/system/arch/x86_64',
        '-DARCH_x86_64',
        '-S', '@INPUT@', '-o', '@OUTPUT@.s',
        '&&', 'python3', meson.project_source_root() + '/build/meson/scripts/extract_asm_offsets.py',
        '@OUTPUT@.s', '@OUTPUT@'
    ],
    command_is_external: true
)

# Build the final gensyscalls tool
gensyscalls_sources = files([
    'gensyscalls.cpp'
])

gensyscalls = executable('gensyscalls_meson',
    [gensyscalls_sources, gensyscalls_infos_gen[0]],  # gensyscalls.cpp + generated gensyscalls_infos.cpp
    dependencies: [
        declare_dependency(sources: syscall_types_sizes_h)
    ],
    native: true,
    install: false,
    include_directories: [
        include_directories('.'),
        include_directories('arch/x86_64'),
    ]
)

# Export the tool for use by other parts of the build
meson.override_find_program('gensyscalls', gensyscalls)