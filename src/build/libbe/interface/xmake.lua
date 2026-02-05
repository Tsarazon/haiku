--[[
    interface_kit_build - Interface Kit for host tools
    Mirrors: src/build/libbe/interface/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

-- Source directories
local kits_interface = path.join(haiku_top, "src", "kits", "interface")
local local_interface = os.scriptdir()  -- src/build/libbe/interface/

target("interface_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

    -- Use HostBeAPI rule for build headers and -include BeOSBuildCompatibility.h
    add_rules("HostBeAPI")

    add_files(path.join(kits_interface, "Bitmap.cpp"))
    add_files(path.join(kits_interface, "ColorConversion.cpp"))
    add_files(path.join(kits_interface, "Gradient.cpp"))
    add_files(path.join(kits_interface, "GradientLinear.cpp"))
    add_files(path.join(kits_interface, "GradientRadial.cpp"))
    add_files(path.join(kits_interface, "GradientRadialFocus.cpp"))
    add_files(path.join(kits_interface, "GradientDiamond.cpp"))
    add_files(path.join(kits_interface, "GradientConic.cpp"))
    add_files(path.join(kits_interface, "GraphicsDefs.cpp"))
    add_files(path.join(kits_interface, "Point.cpp"))
    add_files(path.join(kits_interface, "Rect.cpp"))
    add_files(path.join(kits_interface, "Region.cpp"))
    add_files(path.join(kits_interface, "RegionSupport.cpp"))
    -- SystemPalette.cpp is local to src/build/libbe/interface/
    add_files(path.join(local_interface, "SystemPalette.cpp"))

    -- Use HeadersRules for include paths (mirrors Jamfile)
    on_load(function(target)
        import("rules.HeadersRules")

        -- UsePrivateBuildHeaders app interface shared (from Jamfile line 3)
        HeadersRules.UsePrivateBuildHeaders(target, {"app", "interface", "shared"})

        -- AffineTransform.h is in headers/os/interface/ but not in headers/build/os/interface/
        HeadersRules.UsePublicHeaders(target, {"interface"})
    end)
target_end()