--[[
    makebootable - Make disk bootable
    Mirrors: src/tools/makebootable/Jamfile

    Platform-specific tool that writes boot code to disk.
    Note: Requires HOST_LIBBE.
]]

-- Use global paths from root xmake.lua
local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local tools_output = path.join(output_dir, "tools")

-- Include directories
local build_headers = path.join(haiku_top, "headers", "build")
local build_os_headers = path.join(build_headers, "os")
local private_headers = path.join(haiku_top, "headers", "private")

-- Source directories
local bfs_src = path.join(haiku_top, "src", "add-ons", "kernel", "file_systems", "bfs")
local intel_part_src = path.join(haiku_top, "src", "add-ons", "kernel", "partitioning_systems", "intel")
local gpt_part_src = path.join(haiku_top, "src", "add-ons", "kernel", "partitioning_systems", "gpt")
local bin_makebootable_src = path.join(haiku_top, "src", "bin", "makebootable", "platform", "bios_ia32")

target("makebootable")
    set_kind("binary")
    set_targetdir(tools_output)

    -- Main source
    add_files(path.join(bin_makebootable_src, "makebootable.cpp"))

    -- Platform sources (Linux/Unix)
    add_files(path.join(intel_part_src, "PartitionMap.cpp"))
    add_files(path.join(intel_part_src, "PartitionMapParser.cpp"))
    add_files(path.join(intel_part_src, "PartitionMapWriter.cpp"))
    add_files(path.join(gpt_part_src, "crc32.cpp"))
    add_files(path.join(gpt_part_src, "Header.cpp"))
    add_files(path.join(gpt_part_src, "utility.cpp"))

    add_includedirs(
        build_headers,
        build_os_headers,
        path.join(build_os_headers, "support"),
        path.join(build_os_headers, "storage"),
        path.join(private_headers, "shared"),
        path.join(private_headers, "storage"),
        path.join(private_headers, "interface"),
        bfs_src
    )

    add_defines("_USER_MODE")

    -- TODO: add_deps("libbe_build") when available
target_end()
