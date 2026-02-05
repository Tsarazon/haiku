--[[
    bfs_shell - BFS filesystem shell
    Mirrors: src/tools/bfs_shell/Jamfile

    Interactive shell for accessing BFS disk images.
    Depends on fs_shell.a from ../fs_shell/
]]

-- Use global paths from root xmake.lua
local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local tools_output = path.join(output_dir, "tools")
local objects_output = path.join(output_dir, "objects", "bfs_shell")

-- BFS source directory
local bfs_src = path.join(haiku_top, "src", "add-ons", "kernel", "file_systems", "bfs")
local shared_fs_src = path.join(haiku_top, "src", "add-ons", "kernel", "file_systems", "shared")

-- Include directories
local build_headers = path.join(haiku_top, "headers", "build")
local build_os_headers = path.join(build_headers, "os")

local common_includes = {
    build_headers,
    build_os_headers,
    path.join(build_os_headers, "support"),
    path.join(haiku_top, "headers", "private", "shared"),
    path.join(haiku_top, "headers", "private", "storage"),
    path.join(haiku_top, "headers", "private", "fs_shell"),
    path.join(haiku_top, "headers", "private"),
    path.join(haiku_top, "src", "tools", "fs_shell"),
    bfs_src,
}

-- Common defines
local common_defines = {
    "HAIKU_BUILD_COMPATIBILITY_H",
    "__STRICT_ANSI__",
    "FS_SHELL",
}

-- ============================================================================
-- bfs.o - BFS filesystem merge object
-- ============================================================================

target("bfs_obj")
    set_kind("object")
    set_targetdir(objects_output)

    -- BFS sources from src/add-ons/kernel/file_systems/bfs/
    add_files(path.join(bfs_src, "bfs_disk_system.cpp"))
    add_files(path.join(bfs_src, "BlockAllocator.cpp"))
    add_files(path.join(bfs_src, "BPlusTree.cpp"))
    add_files(path.join(bfs_src, "Attribute.cpp"))
    add_files(path.join(bfs_src, "CheckVisitor.cpp"))
    add_files(path.join(bfs_src, "Debug.cpp"))
    add_files(path.join(bfs_src, "FileSystemVisitor.cpp"))
    add_files(path.join(bfs_src, "Index.cpp"))
    add_files(path.join(bfs_src, "Inode.cpp"))
    add_files(path.join(bfs_src, "Journal.cpp"))
    add_files(path.join(bfs_src, "Query.cpp"))
    add_files(path.join(bfs_src, "ResizeVisitor.cpp"))
    add_files(path.join(bfs_src, "Volume.cpp"))
    add_files(path.join(bfs_src, "kernel_interface.cpp"))

    -- Shared sources
    add_files(path.join(shared_fs_src, "DeviceOpener.cpp"))
    add_files(path.join(shared_fs_src, "QueryParserUtils.cpp"))

    add_includedirs(common_includes)
    add_defines(common_defines)

    add_cxxflags("-std=c++11", "-Wno-multichar", "-fno-rtti")
target_end()

-- ============================================================================
-- bfs_shell - Main executable
-- ============================================================================

target("bfs_shell")
    set_kind("binary")
    set_targetdir(tools_output)

    add_files("additional_commands.cpp")
    add_files("command_checkfs.cpp")
    add_files("command_resizefs.cpp")

    add_deps("bfs_obj")
    add_deps("fs_shell")

    add_includedirs(common_includes)
    add_defines(common_defines)

    add_cxxflags("-std=c++11", "-Wno-multichar", "-fno-rtti")
target_end()

-- ============================================================================
-- bfs_fuse - FUSE version (optional, requires libfuse)
-- ============================================================================

--[[
target("bfs_fuse")
    set_kind("binary")
    set_targetdir(tools_output)

    add_deps("bfs_obj")
    add_deps("fuse_module")

    add_includedirs(common_includes)
    add_defines(common_defines)

    add_links("fuse")
target_end()
]]
