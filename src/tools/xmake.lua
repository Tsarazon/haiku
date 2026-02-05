--[[
    Haiku Build Tools - xmake configuration
    Mirrors: src/tools/Jamfile

    These are host platform tools used during the build process.
    They run on the build machine (Linux/macOS), not on Haiku.
]]

-- ============================================================================
-- Common Settings for Host Tools
-- ============================================================================

-- Use global paths from root xmake.lua
local script_dir = os.scriptdir()
local haiku_top = HAIKU_TOP or path.directory(path.directory(script_dir))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local tools_output = path.join(output_dir, "tools")

-- Common include paths for build compatibility
local build_headers = path.join(haiku_top, "headers", "build")
local private_headers = path.join(haiku_top, "headers", "private")
local os_headers = path.join(build_headers, "os")

-- ============================================================================
-- Simple Tools (single source file)
-- ============================================================================

-- create_image - creates disk images
target("create_image")
    set_kind("binary")
    set_targetdir(tools_output)
    add_files("create_image.cpp")
    add_includedirs(build_headers, {public = false})
target_end()

-- generate_attribute_stores - generates attribute stores for CD images
target("generate_attribute_stores")
    set_kind("binary")
    set_targetdir(tools_output)
    add_files("generate_attribute_stores.cpp")
    add_includedirs(build_headers, os_headers, {public = false})
    -- Needs HOST_LIBBE - will be added when libbe_build is ready
target_end()

-- data_to_source - converts binary data to C source
target("data_to_source")
    set_kind("binary")
    set_targetdir(tools_output)
    add_files("data_to_source.cpp")
    add_includedirs(build_headers, {public = false})
target_end()

-- elf2aout - converts ELF to a.out format
target("elf2aout")
    set_kind("binary")
    set_targetdir(tools_output)
    add_files("elf2aout.c")
target_end()

-- ============================================================================
-- Tools with HOST_LIBBE dependency (placeholder - need libbe_build first)
-- ============================================================================

--[[
These tools need HOST_LIBBE (host build of libbe):
- copyattr
- mimeset
- catattr
- listattr
- mkindex
- rmattr
- settype
- setversion
- xres

They will be enabled once src/build/libbe/ is ported to xmake.
]]

-- ============================================================================
-- Subdirectory Includes
-- ============================================================================

-- Include subdirectories that have their own xmake.lua
-- Each mirrors the corresponding Jamfile

if os.isfile(path.join(script_dir, "addattr/xmake.lua")) then
    includes("addattr")
end

if os.isfile(path.join(script_dir, "bfs_shell/xmake.lua")) then
    includes("bfs_shell")
end

if os.isfile(path.join(script_dir, "fs_shell/xmake.lua")) then
    includes("fs_shell")
end

if os.isfile(path.join(script_dir, "get_package_dependencies/xmake.lua")) then
    includes("get_package_dependencies")
end

if os.isfile(path.join(script_dir, "makebootable/xmake.lua")) then
    includes("makebootable")
end

if os.isfile(path.join(script_dir, "package/xmake.lua")) then
    includes("package")
end

if os.isfile(path.join(script_dir, "rc/xmake.lua")) then
    includes("rc")
end

if os.isfile(path.join(script_dir, "resattr/xmake.lua")) then
    includes("resattr")
end

if os.isfile(path.join(script_dir, "vmdkimage/xmake.lua")) then
    includes("vmdkimage")
end

-- ============================================================================
-- Convenience target to build all essential tools
-- ============================================================================

target("build-tools")
    set_kind("phony")
    -- Simple tools (no external dependencies)
    add_deps("create_image")
    add_deps("vmdkimage")
    add_deps("fs_shell_command")
    add_deps("bfs_shell")
    -- Tools requiring HOST_LIBBE (will fail until libbe_build is ready):
    -- add_deps("addattr")
    -- add_deps("rc")
    -- add_deps("resattr")
    -- add_deps("package")
    -- add_deps("get_package_dependencies")
    -- add_deps("makebootable")
target_end()
