--[[
    libbe_build.so - BeAPI library for host tools (HOST_LIBBE)
    Mirrors: src/build/libbe/Jamfile

    This is the host-platform version of libbe used by build tools.
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local lib_output = path.join(output_dir, "lib")

-- Include kit subdirectories (they define object targets)
includes("support")
includes("storage")
includes("app")
includes("interface")
includes("icon")

-- ============================================================================
-- libbe_build.so - Main shared library
-- ============================================================================

target("libbe_build")
    set_kind("shared")
    set_targetdir(lib_output)
    set_basename("be_build")

    -- Host tools need symbols exported (not hidden) and not stripped
    set_strip("none")
    add_cxflags("-fvisibility=default", {force = true})
    add_cxxflags("-fvisibility=default", {force = true})
    add_shflags("-fvisibility=default", {force = true})

    -- Link all kit objects
    add_deps("support_kit_build")
    add_deps("storage_kit_build")
    add_deps("app_kit_build")
    add_deps("interface_kit_build")
    add_deps("icon_kit_build")

    -- Link libshared_build
    add_deps("libshared_build")

    -- System libraries
    add_links("z", "zstd")
    add_syslinks("pthread")
target_end()
