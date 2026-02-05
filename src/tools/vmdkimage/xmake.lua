--[[
    vmdkimage - VMware disk image creator
    Mirrors: src/tools/vmdkimage/Jamfile
]]

-- Use global paths from root xmake.lua
local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local tools_output = path.join(output_dir, "tools")

target("vmdkimage")
    set_kind("binary")
    set_targetdir(tools_output)
    add_files("vmdkimage.cpp")

    -- UsePrivateHeaders kernel vmdk
    add_includedirs(
        path.join(haiku_top, "headers", "private", "kernel"),
        path.join(haiku_top, "headers", "private", "vmdk"),
        path.join(haiku_top, "headers", "build")
    )
target_end()
