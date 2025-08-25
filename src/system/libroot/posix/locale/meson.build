# Haiku libroot POSIX locale implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX locale layer...')

# locale implementation sources
posix_locale_sources = files([
    'LocaleBackend.cpp',
    'LocaleData.cpp',
    'LocaleDataBridge.cpp',
    'LocaleInternal.cpp',
    'ThreadLocale.cpp',
    'ctype.cpp',
    'ctype_l.cpp',
    'ctype_loc.cpp',
    'empty_placeholder.c',
    'locale_t.cpp',
    'localeconv.cpp',
    'nl_langinfo.cpp',
    'setlocale.cpp',
    'wctype.cpp',
    'wctype_l.cpp',
])

# Build locale object library
posix_locale_lib = static_library('posix_locale',
    posix_locale_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        include_directories('../../../../../headers/private/libroot/locale'),
        include_directories('../../../../../headers/private/libroot/time'),
    ],
    
    c_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    cpp_args: [
        '-fno-builtin',
        '-fno-strict-aliasing',
        '-D_GNU_SOURCE',
        '-D_DEFAULT_SOURCE',
        '-DBUILDING_LIBROOT=1',
        '-DARCH_' + target_arch,
    ],
    
    install: false
)

# Export for parent
posix_locale_dep = declare_dependency(
    link_with: posix_locale_lib
)

message('libroot POSIX locale layer configured successfully')