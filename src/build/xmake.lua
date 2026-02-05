--[[
    Haiku Build Libraries - xmake configuration
    Mirrors: src/build/Jamfile

    Host platform libraries used during cross-compilation:
    - libshared_build.a
    - libbe_build.so (HOST_LIBBE)
    - libpackage_build.so
]]

-- Set global HAIKU_TOP if not already set
if not HAIKU_TOP then
    HAIKU_TOP = path.directory(path.directory(os.scriptdir()))
end
local haiku_top = HAIKU_TOP
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local lib_output = path.join(output_dir, "lib")

-- Store haiku_top in config for use in on_load callbacks
set_config("haiku_top", haiku_top)

-- Include rules from build/xmake/rules/
includes(path.join(haiku_top, "build", "xmake", "rules", "HeadersRules.lua"))

-- Include subdirectories
includes("libshared")
includes("libbe")
-- includes("libpackage")  -- TODO: port later