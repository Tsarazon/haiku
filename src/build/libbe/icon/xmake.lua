--[[
    icon_kit_build - Icon Kit for host tools
    Mirrors: src/build/libbe/icon/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

local libs_icon = path.join(haiku_top, "src", "libs", "icon")
local libs_agg = path.join(haiku_top, "src", "libs", "agg", "src")

target("icon_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

    add_rules("HostBeAPI", {
        private_build_headers = {"shared"},
        library_headers = {"agg", "icon"},
        source_dirs = {
            libs_icon,
            path.join(libs_icon, "flat_icon"),
            path.join(libs_icon, "generic"),
            path.join(libs_icon, "message"),
            path.join(libs_icon, "shape"),
            path.join(libs_icon, "style"),
            path.join(libs_icon, "transformable"),
            path.join(libs_icon, "transformer")
        }
    })

    -- flat_icon
    add_files(path.join(libs_icon, "flat_icon", "FlatIconFormat.cpp"))
    add_files(path.join(libs_icon, "flat_icon", "FlatIconImporter.cpp"))
    add_files(path.join(libs_icon, "flat_icon", "LittleEndianBuffer.cpp"))
    add_files(path.join(libs_icon, "flat_icon", "PathCommandQueue.cpp"))

    -- message
    add_files(path.join(libs_icon, "message", "Defines.cpp"))
    add_files(path.join(libs_icon, "message", "MessageImporter.cpp"))

    -- shape
    add_files(path.join(libs_icon, "shape", "PathSourceShape.cpp"))
    add_files(path.join(libs_icon, "shape", "ReferenceImage.cpp"))
    add_files(path.join(libs_icon, "shape", "Shape.cpp"))
    add_files(path.join(libs_icon, "shape", "VectorPath.cpp"))

    -- style
    add_files(path.join(libs_icon, "style", "GradientTransformable.cpp"))
    add_files(path.join(libs_icon, "style", "Style.cpp"))

    -- transformable
    add_files(path.join(libs_icon, "transformable", "Transformable.cpp"))

    -- transformer
    add_files(path.join(libs_icon, "transformer", "AffineTransformer.cpp"))
    add_files(path.join(libs_icon, "transformer", "CompoundStyleTransformer.cpp"))
    add_files(path.join(libs_icon, "transformer", "ContourTransformer.cpp"))
    add_files(path.join(libs_icon, "transformer", "PathSource.cpp"))
    add_files(path.join(libs_icon, "transformer", "PerspectiveTransformer.cpp"))
    add_files(path.join(libs_icon, "transformer", "StrokeTransformer.cpp"))
    add_files(path.join(libs_icon, "transformer", "StyleTransformer.cpp"))
    add_files(path.join(libs_icon, "transformer", "TransformerFactory.cpp"))

    -- Main icon
    add_files(path.join(libs_icon, "Icon.cpp"))
    add_files(path.join(libs_icon, "IconRenderer.cpp"))
    add_files(path.join(libs_icon, "IconUtils.cpp"))

    -- AGG
    add_files(path.join(libs_agg, "agg_arc.cpp"))
    add_files(path.join(libs_agg, "agg_arrowhead.cpp"))
    add_files(path.join(libs_agg, "agg_bezier_arc.cpp"))
    add_files(path.join(libs_agg, "agg_bspline.cpp"))
    add_files(path.join(libs_agg, "agg_curves.cpp"))
    add_files(path.join(libs_agg, "agg_embedded_raster_fonts.cpp"))
    add_files(path.join(libs_agg, "agg_gsv_text.cpp"))
    add_files(path.join(libs_agg, "agg_image_filters.cpp"))
    add_files(path.join(libs_agg, "agg_line_aa_basics.cpp"))
    add_files(path.join(libs_agg, "agg_line_profile_aa.cpp"))
    add_files(path.join(libs_agg, "agg_rounded_rect.cpp"))
    add_files(path.join(libs_agg, "agg_sqrt_tables.cpp"))
    add_files(path.join(libs_agg, "agg_trans_affine.cpp"))
    add_files(path.join(libs_agg, "agg_trans_double_path.cpp"))
    add_files(path.join(libs_agg, "agg_trans_single_path.cpp"))
    add_files(path.join(libs_agg, "agg_trans_warp_magnifier.cpp"))
    add_files(path.join(libs_agg, "agg_vcgen_bspline.cpp"))
    add_files(path.join(libs_agg, "agg_vcgen_contour.cpp"))
    add_files(path.join(libs_agg, "agg_vcgen_dash.cpp"))
    add_files(path.join(libs_agg, "agg_vcgen_markers_term.cpp"))
    add_files(path.join(libs_agg, "agg_vcgen_smooth_poly1.cpp"))
    add_files(path.join(libs_agg, "agg_vcgen_stroke.cpp"))
    add_files(path.join(libs_agg, "agg_vpgen_clip_polygon.cpp"))
    add_files(path.join(libs_agg, "agg_vpgen_clip_polyline.cpp"))
    add_files(path.join(libs_agg, "agg_vpgen_segmentator.cpp"))
target_end()
