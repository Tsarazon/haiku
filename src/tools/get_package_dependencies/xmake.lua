--[[
    get_package_dependencies - Package dependency resolver
    Mirrors: src/tools/get_package_dependencies/Jamfile

    Note: Requires:
    - libpackage-add-on-libsolv_build.so
    - libpackage_build.so
    - HOST_LIBBE
]]

-- Use global paths from root xmake.lua
local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local tools_output = path.join(output_dir, "tools")

-- Include directories
local build_headers = path.join(haiku_top, "headers", "build")
local build_os_headers = path.join(build_headers, "os")
local private_headers = path.join(haiku_top, "headers", "private")

target("get_package_dependencies")
    set_kind("binary")
    set_targetdir(tools_output)

    add_files("get_package_dependencies.cpp")

    add_includedirs(
        build_headers,
        build_os_headers,
        path.join(build_os_headers, "support"),
        path.join(build_os_headers, "storage"),
        path.join(private_headers, "shared")
    )

    -- TODO: add_deps("libpackage_build", "libpackage_libsolv_build", "libbe_build") when available
target_end()
