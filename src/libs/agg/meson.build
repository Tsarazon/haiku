# Haiku AGG Library - Meson Build Configuration  
# This builds libagg.a matching the Jam build system

# AGG library sources (from agg/Jamfile src directory)
agg_sources = [
    'src/agg_arc.cpp',
    'src/agg_arrowhead.cpp',
    'src/agg_bezier_arc.cpp',
    'src/agg_bspline.cpp',
    'src/agg_curves.cpp',
    'src/agg_embedded_raster_fonts.cpp',
    'src/agg_gsv_text.cpp',
    'src/agg_image_filters.cpp',
    'src/agg_line_aa_basics.cpp',
    'src/agg_line_profile_aa.cpp',
    'src/agg_rounded_rect.cpp',
    'src/agg_sqrt_tables.cpp',
    'src/agg_trans_affine.cpp',
    'src/agg_trans_double_path.cpp',
    'src/agg_trans_single_path.cpp',
    'src/agg_trans_warp_magnifier.cpp',
    'src/agg_vcgen_bspline.cpp',
    'src/agg_vcgen_contour.cpp',
    'src/agg_vcgen_dash.cpp',
    'src/agg_vcgen_markers_term.cpp',
    'src/agg_vcgen_smooth_poly1.cpp',
    'src/agg_vcgen_stroke.cpp',
    'src/agg_vpgen_clip_polygon.cpp',
    'src/agg_vpgen_clip_polyline.cpp',
    'src/agg_vpgen_segmentator.cpp',
]

message('Building AGG Library with ' + agg_sources.length().to_string() + ' source files')

# AGG library object
libagg = static_library('agg',
    agg_sources,
    include_directories: haiku_config['get_all_include_dirs'],
    cpp_args: [
        '-O2',                           # Release optimization
        '-fno-strict-aliasing',          # Standard Haiku flag
        # Removed template control flags to allow instantiation for libbe.so
        # '-fno-implicit-templates',       # Prevent template bloat  
        # '-fno-implicit-inline-templates', # Prevent inline template bloat
        '-w',                            # Disable warnings (as in Jamfile)
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

# Create merged static library libagg.a (proper archive, not thin archive)
# Use simpler copy approach to avoid conflicts
agg_merged = custom_target('agg_output',
    input: libagg,
    output: 'libagg_final.a',
    command: [
        'cp', '@INPUT@', '@OUTPUT@'
    ],
    build_by_default: true,
    install: false
)

message('AGG Library configured successfully - output: libagg.a')