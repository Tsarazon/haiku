# Haiku Storage Kit MIME Library - Meson Build Configuration
# This builds libstorage_kit_mime.a matching the Jam build system

# Storage kit MIME sources (from storage/mime/Jamfile)
storage_kit_mime_sources = [
    'AppMetaMimeCreator.cpp',
    'AssociatedTypes.cpp', 
    'Database.cpp',
    'DatabaseDirectory.cpp',
    'DatabaseLocation.cpp',
    'database_support.cpp',
    'InstalledTypes.cpp',
    'MimeEntryProcessor.cpp',
    'MimeInfoUpdater.cpp',
    'MimeSniffer.cpp',
    'MimeSnifferAddon.cpp',
    'MimeSnifferAddonManager.cpp',
    'SnifferRules.cpp',
    'Supertype.cpp',
    'SupportingApps.cpp',
    'TextSnifferAddon.cpp',
]

message('Building Storage Kit MIME Library with ' + storage_kit_mime_sources.length().to_string() + ' source files')

# Storage kit MIME library object
libstorage_kit_mime = static_library('storage_kit_mime',
    storage_kit_mime_sources,
    include_directories: haiku_config['get_all_include_dirs'],
    cpp_args: [
        '-O2',                           # Release optimization
        '-fno-strict-aliasing',          # Standard Haiku flag
        '-fno-implicit-templates',       # Prevent template bloat
        '-fno-implicit-inline-templates', # Prevent inline template bloat
        '-DHAIKU_TARGET_PLATFORM_HAIKU'
    ],
    pic: true,
    install: false
)

# Create merged static library libstorage_kit_mime.a (matching Jam output)
mime_merged = custom_target('storage_kit_mime_output',
    input: libstorage_kit_mime,
    output: 'libstorage_kit_mime_output.a',
    command: [
        'sh', '-c',
        'mkdir -p ' + haiku_config['libs_output_dir'] + ' && cp @INPUT@ ' + haiku_config['libs_output_dir'] + '/libstorage_kit_mime.a'
    ],
    build_by_default: true,
    install: false
)

message('Storage Kit MIME Library configured successfully - output: libstorage_kit_mime.a')