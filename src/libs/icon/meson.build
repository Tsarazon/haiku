# Haiku Icon Library - Meson Build Configuration
# This builds libicon.a matching the Jam build system

# Icon library sources (from icon/Jamfile)
icon_sources = [
    # flat_icon
    'flat_icon/FlatIconFormat.cpp',
    'flat_icon/FlatIconImporter.cpp', 
    'flat_icon/LittleEndianBuffer.cpp',
    'flat_icon/PathCommandQueue.cpp',
    
    # message
    'message/Defines.cpp',
    'message/MessageImporter.cpp',
    
    # shape
    'shape/PathSourceShape.cpp',
    'shape/ReferenceImage.cpp',
    'shape/Shape.cpp',
    'shape/VectorPath.cpp',
    
    # style
    'style/GradientTransformable.cpp',
    'style/Style.cpp',
    
    # transformable
    'transformable/Transformable.cpp',
    
    # transformer
    'transformer/AffineTransformer.cpp',
    'transformer/CompoundStyleTransformer.cpp',
    'transformer/ContourTransformer.cpp',
    'transformer/PathSource.cpp',
    'transformer/PerspectiveTransformer.cpp',
    'transformer/StrokeTransformer.cpp',
    'transformer/StyleTransformer.cpp',
    'transformer/TransformerFactory.cpp',
    
    # root
    'Icon.cpp',
    'IconRenderer.cpp',
    'IconUtils.cpp',
]

message('Building Icon Library with ' + icon_sources.length().to_string() + ' source files')

# Icon library object (matching interface kit pattern exactly)
libicon = static_library('icon',
    icon_sources,
    include_directories: [
        # LOCAL icon headers FIRST to avoid conflict with system BShape
        include_directories('flat_icon'),
        include_directories('generic'), 
        include_directories('message'),
        include_directories('shape'),
        include_directories('style'),
        include_directories('transformable'),
        include_directories('transformer'),
        # System headers SECOND (contains conflicting BShape)
        haiku_config['get_all_include_dirs']
    ],
    cpp_args: [
        '-O2',                           # Release optimization
        '-fno-strict-aliasing',          # Standard Haiku flag
        '-fno-implicit-templates',       # Prevent template bloat
        '-fno-implicit-inline-templates', # Prevent inline template bloat
        '-w',                            # Disable warnings (as in Jamfile)
        '-DZSTD_ENABLED',                # Critical: Match interface kit pattern
        '-DHAIKU_TARGET_PLATFORM_HAIKU'
    ],
    c_args: [
        '-O2', 
        '-fno-strict-aliasing',
        '-w'
    ],
    pic: true,
    install: false
)

# Create merged static library libicon.a (proper archive, not thin archive)
# Simply use a different output name to avoid conflicts
icon_merged = custom_target('icon_output',
    input: libicon,
    output: 'libicon_final.a',
    command: [
        'cp', '@INPUT@', '@OUTPUT@'
    ],
    build_by_default: true,
    install: false
)

message('Icon Library configured successfully - output: libicon.a')