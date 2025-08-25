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

# Export the static library directly (no custom external directory)
# This matches standard Meson behavior - files stay in builddir
mime_merged = libstorage_kit_mime

message('Storage Kit MIME Library configured successfully - output: libstorage_kit_mime.a')