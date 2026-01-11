--[[
    UserBuildConfig.lua - User customization for Haiku build

    This file can be used to customize the build according to your needs.
    It is included by the build system but ignored by Git.

    See UserBuildConfig.sample.lua for examples.

    WARNING: This is an active configuration file. Edit carefully!
]]

-- ============================================================================
-- Build Variables
-- ============================================================================

-- Build in release mode for better performance
-- This sets DEBUG to 0 globally, which means release builds
SetConfigVar("DEBUG", { "HAIKU_TOP" }, 0, "global")