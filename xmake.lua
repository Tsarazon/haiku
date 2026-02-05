--[[
    Haiku OS Build System - xmake entry point

    This is the root xmake configuration, equivalent to the root Jamfile.
    It includes the main build system from build/xmake/ and optionally
    source subdirectories.

    Usage:
        xmake f -p haiku -a x86_64   # Configure for Haiku x86_64
        xmake f -p host              # Configure for host tools
        xmake                        # Build
        xmake build-tools            # Build host tools only
        xmake haiku-image            # Build disk image
]]

-- Set minimum xmake version
set_xmakever("2.7.0")

-- Project configuration
set_project("haiku")
set_version("1.0.0")

-- ============================================================================
-- Path Configuration (defined here for all included files)
-- ============================================================================

-- HAIKU_TOP: Root of the Haiku source tree (this directory)
HAIKU_TOP = os.projectdir()

-- HAIKU_OUTPUT_DIR: Build output directory
-- Uses "spawned" to keep separate from Jam's "generated"
HAIKU_OUTPUT_DIR = path.join(HAIKU_TOP, "spawned")

-- Set buildir EARLY - before any includes - to ensure xmake uses spawned/
-- for all cache, dependencies, and build artifacts
set_config("buildir", HAIKU_OUTPUT_DIR)

-- ============================================================================
-- Include Main Build System
-- ============================================================================

-- The main build rules and configuration are in build/xmake/
-- This mirrors how Jamfile includes build/jam rules
includes("build/xmake/xmake.lua")

-- ============================================================================
-- Source Subdirectories
-- ============================================================================

-- Include source subdirectories (similar to Jam's SubInclude)
-- These will be enabled as xmake.lua files are added to src/

-- Build libraries (host platform) - libbe_build.so, libshared_build.a
if os.isfile("src/build/xmake.lua") then
    includes("src/build/xmake.lua")
end

-- Build tools (host platform) - required for image creation
if os.isfile("src/tools/xmake.lua") then
    includes("src/tools/xmake.lua")
end

-- Main source tree
if os.isfile("src/xmake.lua") then
    includes("src/xmake.lua")
end

-- Third-party components (optional)
if os.isfile("3rdparty/xmake.lua") then
    includes("3rdparty/xmake.lua")
end
