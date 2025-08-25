# Haiku Locale Kit - Meson Build Configuration
# This builds locale_kit.o matching the exact Jam build system output

# Locale Kit sources (exactly from locale/Jamfile)
locale_kit_sources = [
    'Catalog.cpp',
    'CatalogData.cpp',
    'Collator.cpp',
    'Country.cpp',
    'DefaultCatalog.cpp',
    'EditableCatalog.cpp',
    'FormattingConventions.cpp',
    'HashMapCatalog.cpp',
    'InitLocaleKit.cpp',
    'Language.cpp',
    'Locale.cpp',
    'LocaleRoster.cpp',
    'LocaleRosterData.cpp',
    'MutableLocaleRoster.cpp',
    'TextEncoding.cpp',
    'TimeZone.cpp',
    
    # In progress formatting classes
    'DateFormat.cpp',
    'DateTimeFormat.cpp',
    'DurationFormat.cpp',
    'NumberFormat.cpp',
    'StringFormat.cpp',
    'TimeFormat.cpp',
    'TimeUnitFormat.cpp',
    'Format.cpp',                    # Used by some of the above
    'UnicodeChar.cpp',              # Already used in FirstBootPrompt
    'RelativeDateTimeFormat.cpp',
]

# Locale kit defines (matching Jamfile conditions)
locale_kit_defines = [
    '-DZSTD_ENABLED',
    '-DHAIKU_TARGET_PLATFORM_HAIKU',
]

# Build packages headers as compiler args (highest precedence)
target_arch = get_option('target_arch')
build_packages_args = []
build_packages_abs = meson.project_source_root() + '/generated/build_packages'
build_pkg_abs_paths = [
    build_packages_abs + '/zlib-1.3.1-3-' + target_arch + '/develop/headers',
    build_packages_abs + '/zstd-1.5.6-1-' + target_arch + '/develop/headers', 
    build_packages_abs + '/icu74-74.1-3-' + target_arch + '/develop/headers',
]

foreach pkg_path : build_pkg_abs_paths
    if fs.is_dir(pkg_path)
        build_packages_args += ['-I' + pkg_path]
    endif
endforeach

message('Building Locale Kit with ' + locale_kit_sources.length().to_string() + ' source files')

# Create locale kit object library
locale_kit_obj = static_library('locale_kit_obj',
    locale_kit_sources,
    include_directories: haiku_config['get_all_include_dirs'],
    cpp_args: build_packages_args + [
        '-O2',                           # Optimization (matching Jam)
        '-fno-strict-aliasing',          # Standard Haiku flag
        # '-fno-implicit-templates',       # KEY: Prevent template instantiation bloat - REMOVED for libbe.so linking
        '-fno-implicit-inline-templates' # KEY: Prevent inline template bloat
    ] + locale_kit_defines,
    pic: true,                           # Use Meson's pic instead of manual -fPIC
    dependencies: [
        # Build packages provide headers via global include paths
        # ICU is critical for locale functionality
    ],
    install: false
)

# Create merged object file locale_kit.o (matching Jam MergeObject)
locale_kit_merged = custom_target('locale_kit',
    input: locale_kit_obj,
    output: 'locale_kit.o',
    command: [
        'sh', '-c',
        '/home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-ld -r -o "$1" "$2".p/*.o',
        'sh', '@OUTPUT@', '@INPUT@'
    ],
    build_by_default: true,
    install: false
)

message('Locale Kit configured successfully - output: locale_kit.o')