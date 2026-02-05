--[[
    rc - Resource compiler
    Mirrors: src/tools/rc/Jamfile

    Compiles .rdef resource definition files.
    Uses flex/bison for lexer and parser.

    Note: Requires HOST_LIBBE (libbe built for host platform).
]]

-- Use global paths from root xmake.lua
local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(os.scriptdir())))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local tools_output = path.join(output_dir, "tools")
local objects_output = path.join(output_dir, "objects", "rc")

-- Source from src/bin/rc/
local bin_rc_src = path.join(haiku_top, "src", "bin", "rc")

-- Include directories
local build_headers = path.join(haiku_top, "headers", "build")
local build_os_headers = path.join(build_headers, "os")

-- ============================================================================
-- librdef.a - Resource definition library
-- ============================================================================

target("librdef")
    set_kind("static")
    set_targetdir(objects_output)

    add_files(path.join(bin_rc_src, "compile.cpp"))
    add_files(path.join(bin_rc_src, "decompile.cpp"))
    add_files(path.join(bin_rc_src, "rdef.cpp"))

    -- Flex/Bison generated files
    -- Note: xmake can auto-generate from .l and .y files
    add_files(path.join(bin_rc_src, "lexer.l"))
    add_files(path.join(bin_rc_src, "parser.y"))

    add_includedirs(
        build_headers,
        build_os_headers,
        path.join(build_os_headers, "support"),
        path.join(build_os_headers, "storage"),
        bin_rc_src  -- for generated headers
    )

    add_cxxflags("-Wno-sign-compare", "-Wno-unused")

    -- Enable C++ output for flex/bison
    add_rules("lex", "yacc")
target_end()

-- ============================================================================
-- rc - Resource compiler executable
-- ============================================================================

target("rc")
    set_kind("binary")
    set_targetdir(tools_output)

    add_files(path.join(bin_rc_src, "rc.cpp"))

    add_deps("librdef")

    add_includedirs(
        build_headers,
        build_os_headers,
        path.join(build_os_headers, "support"),
        path.join(build_os_headers, "storage"),
        bin_rc_src
    )

    -- TODO: add_deps("libbe_build") when available
target_end()
