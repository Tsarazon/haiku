--[[
    fs_shell - Filesystem shell support library
    Mirrors: src/tools/fs_shell/Jamfile

    Provides kernel emulation for filesystem tools like bfs_shell.
    Builds:
    - fs_shell_kernel.o (merge object)
    - fs_shell.a (static library)
    - fuse_module.a (static library for FUSE)
    - fs_shell_command (executable)
]]

-- Use global paths from root xmake.lua
local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local tools_output = path.join(output_dir, "tools")
local objects_output = path.join(output_dir, "objects", "fs_shell")

-- Common include directories
local build_headers = path.join(haiku_top, "headers", "build")
local build_os_headers = path.join(build_headers, "os")

local common_includes = {
    build_headers,
    build_os_headers,
    path.join(build_os_headers, "kernel"),
    path.join(build_os_headers, "storage"),
    path.join(build_os_headers, "support"),
    path.join(haiku_top, "headers", "private", "fs_shell"),
    path.join(haiku_top, "headers", "private", "shared"),
    path.join(haiku_top, "headers", "private", "file_systems"),
}

-- Common defines
local common_defines = {
    "FS_SHELL=1",
    "HAIKU_BUILD_COMPATIBILITY_H",
    "__STRICT_ANSI__",
}

-- Kernel emulation sources
local kernel_sources = {
    "atomic.cpp",
    "block_cache.cpp",
    "byte_order.cpp",
    "command_cp.cpp",
    "disk_device_manager.cpp",
    "driver_settings.cpp",
    "errno.cpp",
    "fcntl.cpp",
    "fd.cpp",
    "file_cache.cpp",
    "kernel_export.cpp",
    "KPath.cpp",
    "hash.cpp",
    "list.cpp",
    "lock.cpp",
    "module.cpp",
    "node_monitor.cpp",
    "partition_support.cpp",
    "path_util.cpp",
    "sem.cpp",
    "stat.cpp",
    "stat_util.cpp",
    "stdio.cpp",
    "string.cpp",
    "thread.cpp",
    "time.cpp",
    "uio.cpp",
    "unistd.cpp",
    "vfs.cpp",
    -- Files from other directories (added with full path below)
}

-- ============================================================================
-- fs_shell_kernel.o - Kernel emulation merge object
-- ============================================================================

target("fs_shell_kernel")
    set_kind("object")
    set_targetdir(objects_output)

    -- Local sources
    for _, src in ipairs(kernel_sources) do
        add_files(src)
    end

    -- Sources from other directories
    add_files(path.join(haiku_top, "src", "system", "kernel", "fs", "rootfs.cpp"))
    add_files(path.join(haiku_top, "src", "system", "kernel", "cache", "file_map.cpp"))

    add_includedirs(common_includes)
    add_defines(common_defines)
target_end()

-- ============================================================================
-- fs_shell.a - Static library for filesystem shells
-- ============================================================================

target("fs_shell")
    set_kind("static")
    set_targetdir(objects_output)

    add_files("external_commands_unix.cpp")  -- Unix platform
    add_files("fssh.cpp")
    add_files("fssh_additional_commands.cpp")

    add_deps("fs_shell_kernel")

    add_includedirs(common_includes)
    add_defines(common_defines)
target_end()

-- ============================================================================
-- fuse_module.a - Static library for FUSE support
-- ============================================================================

target("fuse_module")
    set_kind("static")
    set_targetdir(objects_output)

    add_files("external_commands_unix.cpp")  -- Unix platform
    add_files("fuse.cpp")

    add_deps("fs_shell_kernel")

    add_includedirs(common_includes)
    add_defines(common_defines)
target_end()

-- ============================================================================
-- fs_shell_command - Command line tool
-- ============================================================================

target("fs_shell_command")
    set_kind("binary")
    set_targetdir(tools_output)

    add_files("fs_shell_command.cpp")
    add_files("fs_shell_command_unix.cpp")  -- Unix platform

    add_includedirs(common_includes)
    add_defines(common_defines)
target_end()
