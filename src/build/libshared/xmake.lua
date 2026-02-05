--[[
    libshared_build.a - Shared utility library for host tools
    Mirrors: src/build/libshared/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local lib_output = path.join(output_dir, "lib")

-- Source directory
local kits_shared = path.join(haiku_top, "src", "kits", "shared")

-- Headers
local build_headers = path.join(haiku_top, "headers", "build")
local private_headers = path.join(haiku_top, "headers", "private")

target("libshared_build")
    set_kind("static")
    set_targetdir(lib_output)
    set_basename("shared_build")

    -- Use HostBeAPI rule for build headers and -include BeOSBuildCompatibility.h
    add_rules("HostBeAPI")

    add_files(path.join(kits_shared, "Keymap.cpp"))
    add_files(path.join(kits_shared, "NaturalCompare.cpp"))
    add_files(path.join(kits_shared, "RegExp.cpp"))
    add_files(path.join(kits_shared, "StringForSize.cpp"))

    -- Kit-specific private headers (HostBeAPI provides build_headers/*)
    add_includedirs(
        path.join(private_headers, "interface"),
        path.join(private_headers, "shared")
    )
target_end()
