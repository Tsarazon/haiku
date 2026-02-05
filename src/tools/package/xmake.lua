--[[
    package - Haiku package manager tool
    Mirrors: src/tools/package/Jamfile

    Note: Requires:
    - libpackage_build.so
    - HOST_LIBBE (libbe built for host platform)
]]

-- Use global paths from root xmake.lua
local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local tools_output = path.join(output_dir, "tools")

-- Source from src/bin/package/
local bin_package_src = path.join(haiku_top, "src", "bin", "package")

-- Include directories
local build_headers = path.join(haiku_top, "headers", "build")
local build_os_headers = path.join(build_headers, "os")
local private_headers = path.join(haiku_top, "headers", "private")

target("package")
    set_kind("binary")
    set_targetdir(tools_output)

    add_files(path.join(bin_package_src, "command_add.cpp"))
    add_files(path.join(bin_package_src, "command_checksum.cpp"))
    add_files(path.join(bin_package_src, "command_create.cpp"))
    add_files(path.join(bin_package_src, "command_dump.cpp"))
    add_files(path.join(bin_package_src, "command_extract.cpp"))
    add_files(path.join(bin_package_src, "command_info.cpp"))
    add_files(path.join(bin_package_src, "command_list.cpp"))
    add_files(path.join(bin_package_src, "command_recompress.cpp"))
    add_files(path.join(bin_package_src, "package.cpp"))
    add_files(path.join(bin_package_src, "PackageWriterListener.cpp"))
    add_files(path.join(bin_package_src, "PackageWritingUtils.cpp"))

    add_includedirs(
        build_headers,
        build_os_headers,
        path.join(build_os_headers, "support"),
        path.join(build_os_headers, "storage"),
        path.join(private_headers, "libroot"),
        path.join(private_headers, "shared"),
        path.join(private_headers, "kernel"),
        path.join(private_headers, "storage"),
        path.join(private_headers, "support")
    )

    -- TODO: add_deps("libpackage_build", "libbe_build") when available
target_end()
