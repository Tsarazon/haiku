--[[
    interface_kit_build - Interface Kit for host tools
    Mirrors: src/build/libbe/interface/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

-- Local stubs
local build_interface = os.scriptdir()
-- Fallback
local kits_interface = path.join(haiku_top, "src", "kits", "interface")

target("interface_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

    add_rules("HostBeAPI", {
        private_build_headers = {"app", "interface", "shared"}
    })

    -- Local stub files
    add_files(path.join(build_interface, "Bitmap.cpp"))
    add_files(path.join(build_interface, "GraphicsDefs.cpp"))
    add_files(path.join(build_interface, "SystemPalette.cpp"))

    -- From src/kits/interface/
    add_files(path.join(kits_interface, "ColorConversion.cpp"))
    add_files(path.join(kits_interface, "Gradient.cpp"))
    add_files(path.join(kits_interface, "GradientLinear.cpp"))
    add_files(path.join(kits_interface, "GradientRadial.cpp"))
    add_files(path.join(kits_interface, "GradientRadialFocus.cpp"))
    add_files(path.join(kits_interface, "GradientDiamond.cpp"))
    add_files(path.join(kits_interface, "GradientConic.cpp"))
    add_files(path.join(kits_interface, "Point.cpp"))
    add_files(path.join(kits_interface, "Rect.cpp"))
    add_files(path.join(kits_interface, "Region.cpp"))
    add_files(path.join(kits_interface, "RegionSupport.cpp"))
target_end()
