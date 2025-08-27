# Haiku libroot stubbed library - Meson Build Configuration
# Mirrors the Jamfile: builds stubbed libroot.so containing only empty symbols

# Determine sources based on architecture
stubbed_sources = ['libroot_stubs.c']
if target_machine.cpu() == 'x86'
    stubbed_sources += ['libroot_stubs_x86.c']
endif

stubbed_libroot = shared_library('root',
    stubbed_sources,
    include_directories: [
        haiku_config['get_all_include_dirs'],
    ],
    c_args: [
        '-Wno-missing-prototypes',
        '-fno-builtin',
        '-O2',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-DARCH_x86_64',
        '-DBUILDING_LIBROOT=1'
    ],
    link_args: [
        '-shared',
        '-soname=libroot.so',
        '--version-script=' + meson.current_source_dir() + '/../libroot_versions'
    ],
    pic: true,
    name_suffix: 'so',
    install: false
)

# Export dependency for stubbed libroot
stubbed_libroot_dep = declare_dependency(
    link_with: stubbed_libroot
)