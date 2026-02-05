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

    -- Headers (mirrors Jamfile: UsePrivateBuildHeaders app interface shared)
    on_load(function(target)
        import("core.project.config")
        local top = config.get("haiku_top")
        local build_private = path.join(top, "headers", "build", "private")
        local private_headers = path.join(top, "headers", "private")
        local os_headers = path.join(top, "headers", "os")

        -- Build private headers
        target:add("sysincludedirs",
            path.join(build_private, "app"),
            path.join(build_private, "interface"),
            path.join(build_private, "shared")
        )

        -- Regular private headers (ApplicationPrivate.h, etc.)
        target:add("sysincludedirs",
            path.join(private_headers, "app"),
            path.join(private_headers, "interface"),
            path.join(private_headers, "shared")
        )

        -- AffineTransform.h is in headers/os/interface/ but not in headers/build/os/interface/
        target:add("sysincludedirs", path.join(os_headers, "interface"))
    end)
target_end()
