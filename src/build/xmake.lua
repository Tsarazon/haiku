--[[
    Haiku Build Libraries - xmake configuration
    Mirrors: src/build/Jamfile

    Host platform libraries used during cross-compilation:
    - libshared_build.a
    - libbe_build.so (HOST_LIBBE)
    - libpackage_build.so
]]

-- Use global paths from root xmake.lua
local haiku_top = HAIKU_TOP or path.directory(path.directory(os.scriptdir()))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local lib_output = path.join(output_dir, "lib")

-- Include rules from build/xmake/rules/
includes(path.join(haiku_top, "build", "xmake", "rules", "HeadersRules.lua"))

-- Include subdirectories
includes("libshared")
includes("libbe")
-- includes("libpackage")  -- TODO: port later