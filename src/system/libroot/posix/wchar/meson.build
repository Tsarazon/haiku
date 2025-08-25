# Haiku libroot POSIX wchar implementation
# Auto-generated from directory structure analysis with custom modules integration

message('Building libroot POSIX wchar layer...')

# wchar implementation sources
posix_wchar_sources = files([
    'btowc.c',
    'empty_placeholder.c',
    'mblen.c',
    'mbrlen.c',
    'mbrtowc.cpp',
    'mbsinit.c',
    'mbsrtowcs.cpp',
    'mbstowcs.c',
    'mbtowc.c',
    'wcpcpy.c',
    'wcpncpy.c',
    'wcrtomb.cpp',
    'wcscasecmp.c',
    'wcscat.c',
    'wcschr.c',
    'wcschrnul.c',
    'wcscmp.c',
    'wcscoll.cpp',
    'wcscpy.c',
    'wcscspn.c',
    'wcsdup.c',
    # 'wcsftime.cpp',  # Keep disabled - contains #error "not implemented"
    'wcslcat.c',
    'wcslcpy.c',
    'wcslen.c',
    'wcsncasecmp.c',
    'wcsncat.c',
    'wcsncmp.c',
    'wcsncpy.c',
    'wcsnlen.c',
    'wcspbrk.c',
    'wcsrchr.c',
    'wcsrtombs.cpp',
    'wcsspn.c',
    'wcsstr.c',
    'wcstok.c',
    'wcstombs.c',
    'wcswidth.c',
    'wcsxfrm.cpp',
    'wctob.c',
    'wctomb.c',
    'wcwidth.c',
    'wmemchr.c',
    'wmemcmp.c',
    'wmemcpy.c',
    'wmemmove.c',
    'wmempcpy.c',
    'wmemset.c',
])

# Build wchar object library
posix_wchar_lib = static_library('posix_wchar',
    posix_wchar_sources,
    
    include_directories: [
        haiku_config['get_all_include_dirs'],
        # LocaleBackend.h location
        include_directories('../../../../../headers/private/libroot/locale'),
        include_directories('../../../../../src/kits/locale'),
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
posix_wchar_dep = declare_dependency(
    link_with: posix_wchar_lib
)

message('libroot POSIX wchar layer configured successfully')