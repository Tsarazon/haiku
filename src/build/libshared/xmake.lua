--[[
    libshared_build.a - Shared utility library for host tools
    Mirrors: src/build/libshared/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local lib_output = path.join(output_dir, "lib")

-- Local stubs
local build_shared = os.scriptdir()
-- Fallback
local kits_shared = path.join(haiku_top, "src", "kits", "shared")

target("libshared_build")
    set_kind("static")
    set_targetdir(lib_output)
    set_basename("shared_build")

    add_rules("HostBeAPI", {
        private_build_headers = {"shared"},
        private_headers = {"interface", "shared"}
    })

    -- From src/kits/shared/
    add_files(path.join(kits_shared, "Keymap.cpp"))
    add_files(path.join(kits_shared, "RegExp.cpp"))

    -- Local stub files
    add_files(path.join(build_shared, "NaturalCompare.cpp"))
    add_files(path.join(build_shared, "StringForSize.cpp"))
target_end()
