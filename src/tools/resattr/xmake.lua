--[[
    resattr - Resource to attribute converter
    Mirrors: src/tools/resattr/Jamfile

    Note: Requires HOST_LIBBE (libbe built for host platform).
]]

-- Use global paths from root xmake.lua
local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local tools_output = path.join(output_dir, "tools")

-- Include directories
local build_headers = path.join(haiku_top, "headers", "build")
local build_os_headers = path.join(build_headers, "os")

target("resattr")
    set_kind("binary")
    set_targetdir(tools_output)

    -- Source from src/bin/
    add_files(path.join(haiku_top, "src", "bin", "resattr.cpp"))

    add_includedirs(
        build_headers,
        build_os_headers,
        path.join(build_os_headers, "support"),
        path.join(build_os_headers, "storage")
    )

    -- TODO: add_deps("libbe_build") when available
target_end()
