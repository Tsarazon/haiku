--[[
    Haiku OS Build System - xmake configuration

    This is the main xmake configuration file that includes all Haiku-specific
    build rules. It serves as an equivalent to build/jam/Jamfile and related
    Jam configuration files.

    Usage:
        xmake f -p haiku -a x86_64   # Configure for Haiku x86_64
        xmake                         # Build
        xmake -v                      # Build with verbose output
]]

-- Set minimum xmake version
set_xmakever("2.7.0")

-- Project configuration
set_project("haiku")
set_version("1.0.0")
set_languages("c11", "c++17")

-- ============================================================================
-- Path Configuration
-- ============================================================================
-- os.projectdir() returns /home/ruslan/haiku/build/xmake (where xmake.lua is)
-- We need HAIKU_TOP to be /home/ruslan/haiku (2 levels up)

-- HAIKU_TOP: Root of the Haiku source tree
-- This is the parent of build/xmake, equivalent to $(HAIKU_TOP) in Jam
HAIKU_TOP = path.directory(path.directory(os.projectdir()))

-- HAIKU_OUTPUT_DIR: Build output directory for xmake
-- Uses "spawned" to keep separate from Jam's "generated"
HAIKU_OUTPUT_DIR = path.join(HAIKU_TOP, "spawned")

-- Set these as xmake config values so rule files can use config.get()
-- This allows rules to use: config.get("haiku_top") instead of os.projectdir()
set_config("haiku_top", HAIKU_TOP)
set_config("haiku_output_dir", HAIKU_OUTPUT_DIR)
set_config("build_output_dir", HAIKU_OUTPUT_DIR)  -- alias used by some rules
set_config("buildir", HAIKU_OUTPUT_DIR)

-- Module search paths for import() in script scope
add_moduledirs(".")

-- ============================================================================
-- Path Helper Functions
-- ============================================================================
-- These functions provide access to the path variables for imported modules

-- Get the Haiku source tree root directory
function get_haiku_top()
    return HAIKU_TOP
end

-- Get the xmake build output directory (spawned/)
function get_haiku_output_dir()
    return HAIKU_OUTPUT_DIR
end

-- Get the tools directory within the output directory
function get_haiku_tools_dir()
    return path.join(HAIKU_OUTPUT_DIR, "tools")
end

-- Get the objects directory within the output directory
function get_haiku_objects_dir()
    return path.join(HAIKU_OUTPUT_DIR, "objects")
end

-- ============================================================================
-- Rule Includes
-- ============================================================================

-- Include Haiku-specific build rules
includes("rules/HelperRules.lua")       -- Utility functions (must be first)
includes("rules/BuildFeatureRules.lua") -- Build feature management (optional components)
includes("rules/ConfigRules.lua")       -- Directory-based configuration system
includes("rules/FileRules.lua")         -- File operations (copy, symlink, extract, download)
includes("rules/ArchitectureRules.lua") -- Architecture setup (flags, warnings, glue code)
includes("rules/HeadersRules.lua")      -- Header directory management
includes("rules/MainBuildRules.lua")    -- Application, Addon, Library rules
includes("rules/BeOSRules.lua")         -- BeOS rules: resources, MIME types, versions
includes("rules/KernelRules.lua")       -- Kernel and driver build rules
includes("rules/BootRules.lua")         -- Boot loader build rules
includes("rules/PackageRules.lua")      -- Package and container rules (includes Container base)
includes("rules/ImageRules.lua")        -- Disk image and boot image rules (uses Container)
includes("rules/CDRules.lua")           -- CD/ISO image creation
includes("rules/LocaleRules.lua")       -- Localization/catalog rules
includes("rules/MathRules.lua")         -- Basic arithmetic operations
includes("rules/MiscRules.lua")         -- Miscellaneous build utilities
includes("rules/RepositoryRules.lua")   -- Package repository management
includes("rules/TestsRules.lua")        -- Unit test build rules
includes("rules/SystemLibraryRules.lua") -- System library management (libstdc++, libgcc)

-- ============================================================================
-- Configuration Includes
-- ============================================================================

-- Include configuration and data definitions
includes("config/BuildSetup.lua")           -- Build system configuration and setup
includes("config/BuildFeatures.lua")        -- External package dependencies
includes("config/DefaultBuildProfiles.lua") -- Default build profile definitions
includes("config/HaikuPackages.lua")        -- System package definitions
includes("config/OptionalPackages.lua")     -- Optional package definitions
includes("config/CommandLineArguments.lua") -- Command line processing, build profiles
includes("config/CrossToolsConfig.lua")     -- Cross-compiler toolchain configuration

-- User-specific build configuration (optional, not in version control)
-- $(projectdir) is already /home/ruslan/haiku/build/xmake
if os.isfile(path.join(os.projectdir(), "config", "UserBuildConfig.lua")) then
    includes("config/UserBuildConfig.lua")
end

-- ============================================================================
-- Image Build Targets
-- ============================================================================

-- Include image build targets (these define xmake targets like haiku-image)
includes("images/HaikuImage.lua")      -- Main Haiku disk image (xmake haiku-image)
includes("images/HaikuCD.lua")         -- CD/ISO installation image (xmake haiku-cd)
includes("images/CDBootImage.lua")     -- CD boot image with EFI/BIOS support
includes("images/AnybootImage.lua")    -- Anyboot image (USB + CD + EFI)
includes("images/MMCImage.lua")        -- SD/MMC card images for ARM/RISC-V
includes("images/NetBootArchive.lua")  -- Network boot archive (PXE/TFTP)

-- Note: images/definitions/ contains data modules (minimum.lua, regular.lua, etc.)
-- These are imported on-demand by HaikuImage.lua, not included here.

-- ============================================================================
-- Package and Repository Definitions (import-based)
-- ============================================================================

-- packages/ and repositories/ contain data definitions that are imported
-- on-demand by the build rules. They don't need to be included here.
--
-- Usage examples:
--   local Haiku = import("packages.Haiku")
--   local HaikuPorts = import("repositories.HaikuPorts.HaikuPorts")
--
-- Available packages:
--   Haiku, HaikuBootstrap, HaikuCrossDevel, HaikuDevel, HaikuDevelSecondary,
--   HaikuExtras, HaikuLoader, HaikuSecondary, HaikuSecondaryBootstrap,
--   HaikuSource, HaikuDataTranslators, MakefileEngine, NetFS, UserlandFS,
--   WebPositive
--
-- Available repositories:
--   Haiku (main repo), HaikuPorts/* (per-arch), HaikuPortsCross/* (cross-compile)

-- ============================================================================
-- Platform Configuration
-- ============================================================================

-- Haiku platform definition
-- Note: Architecture-specific flags, defines, and includes are handled by
-- ArchitectureRules.lua via the ArchitectureAware rule. Targets should use
-- add_rules("ArchitectureAware") to get proper architecture configuration.
if is_plat("haiku") then

    -- Set toolchain for Haiku target
    set_toolchains("gcc")

    -- Basic Haiku platform defines (arch-specific defines come from ArchitectureAware rule)
    add_cxflags("-D_GNU_SOURCE")
    add_cxflags("-D__HAIKU__")

elseif is_plat("host") or is_host("linux", "macosx", "bsd") then

    -- Host platform for building tools
    set_toolchains("gcc")

    add_defines("HAIKU_HOST_PLATFORM")

end

-- ============================================================================
-- Build Modes
-- ============================================================================

-- Debug mode
add_rules("mode.debug")

-- Release mode
add_rules("mode.release")

-- ============================================================================
-- Common Options
-- ============================================================================

-- Target architecture
option("target_arch")
    set_default("x86_64")
    set_showmenu(true)
    set_description("Target architecture (x86_64, x86, arm64, riscv64)")
    set_values("x86_64", "x86", "arm64", "riscv64")
option_end()

-- Packaging architecture (for hybrid builds)
option("packaging_arch")
    set_default("")
    set_showmenu(true)
    set_description("Packaging architecture for hybrid builds")
option_end()

-- Secondary architecture
option("secondary_arch")
    set_default("")
    set_showmenu(true)
    set_description("Secondary architecture (e.g., x86 on x86_64)")
option_end()

-- Debug level
option("debug_level")
    set_default(0)
    set_showmenu(true)
    set_description("Debug level (0=release, 1=debug)")
    set_values("0", "1")
option_end()

-- Warnings level
option("warnings")
    set_default(1)
    set_showmenu(true)
    set_description("Warning level (0=off, 1=on, 2=treat as errors)")
    set_values("0", "1", "2")
option_end()

-- ============================================================================
-- Output Directories
-- ============================================================================

-- Path variables defined at the top of this file:
--   HAIKU_TOP        = /home/ruslan/haiku (source root)
--   HAIKU_OUTPUT_DIR = /home/ruslan/haiku/spawned (build output)
--
-- Helper functions for imported modules:
--   get_haiku_top()        - returns HAIKU_TOP
--   get_haiku_output_dir() - returns HAIKU_OUTPUT_DIR
--   get_haiku_tools_dir()  - returns HAIKU_OUTPUT_DIR/tools
--   get_haiku_objects_dir() - returns HAIKU_OUTPUT_DIR/objects
--
-- Individual targets can use set_objectdir/set_targetdir to customize paths

-- ============================================================================
-- Helper Functions for Jamfile Translation
-- ============================================================================

-- Make these available globally for subprojects
function haiku_application(name, config)
    target(name)
        add_rules("Application")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
        if config.resources then
            set_values("resources", config.resources)
        end
    target_end()
end

function haiku_addon(name, config)
    target(name)
        add_rules("Addon")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
        if config.is_executable then
            set_values("is_executable", config.is_executable)
        end
    target_end()
end

function haiku_translator(name, config)
    target(name)
        add_rules("Translator")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
    target_end()
end

function haiku_screensaver(name, config)
    target(name)
        add_rules("ScreenSaver")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
    target_end()
end

function haiku_static_library(name, config)
    target(name)
        add_rules("StaticLibrary")
        if config.sources then
            add_files(config.sources)
        end
    target_end()
end

function haiku_shared_library(name, config)
    target(name)
        add_rules("SharedLibrary")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
        if config.abi_version then
            set_values("abi_version", config.abi_version)
        end
    target_end()
end

function haiku_merge_object(name, config)
    target(name)
        add_rules("MergeObject")
        if config.sources then
            add_files(config.sources)
        end
        if config.other_objects then
            set_values("other_objects", config.other_objects)
        end
    target_end()
end

-- Custom linker target helper
function haiku_ld(name, config)
    target(name)
        add_rules("Ld")
        if config.objects then
            set_values("objects", config.objects)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
        if config.ldflags then
            add_ldflags(config.ldflags)
        end
    target_end()
end

-- Build platform (host) helpers
function build_platform_main(name, config)
    target(name)
        add_rules("BuildPlatformMain")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
    target_end()
end

function build_platform_static_library(name, config)
    target(name)
        add_rules("BuildPlatformStaticLibrary")
        if config.sources then
            add_files(config.sources)
        end
    target_end()
end

function build_platform_shared_library(name, config)
    target(name)
        add_rules("BuildPlatformSharedLibrary")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
    target_end()
end

-- ============================================================================
-- Kernel Build Helpers (from KernelRules.lua)
-- ============================================================================

function kernel_addon(name, config)
    target(name)
        add_rules("KernelAddon")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
    target_end()
end

function kernel_ld(name, config)
    target(name)
        add_rules("KernelLd")
        if config.objects then
            set_values("objects", config.objects)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
        if config.ldflags then
            add_ldflags(config.ldflags)
        end
    target_end()
end

function kernel_merge_object(name, config)
    target(name)
        add_rules("KernelMergeObject")
        if config.sources then
            add_files(config.sources)
        end
        if config.other_objects then
            set_values("other_objects", config.other_objects)
        end
    target_end()
end

function kernel_static_library(name, config)
    target(name)
        add_rules("KernelStaticLibrary")
        if config.sources then
            add_files(config.sources)
        end
    target_end()
end

-- ============================================================================
-- Boot Loader Helpers (from BootRules.lua)
-- ============================================================================

function boot_ld(name, config)
    target(name)
        add_rules("BootLd")
        if config.objects then
            set_values("objects", config.objects)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
        if config.ldflags then
            add_ldflags(config.ldflags)
        end
    target_end()
end

function boot_merge_object(name, config)
    target(name)
        add_rules("BootMergeObject")
        if config.sources then
            add_files(config.sources)
        end
        if config.other_objects then
            set_values("other_objects", config.other_objects)
        end
    target_end()
end

function boot_static_library(name, config)
    target(name)
        add_rules("BootStaticLibrary")
        if config.sources then
            add_files(config.sources)
        end
    target_end()
end

-- ============================================================================
-- Test Helpers (from TestsRules.lua)
-- ============================================================================

function unit_test_lib(name, config)
    target(name)
        add_rules("UnitTestLib")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
    target_end()
end

function unit_test(name, config)
    target(name)
        add_rules("UnitTest")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
    target_end()
end

function simple_test(name, config)
    target(name)
        add_rules("SimpleTest")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
    target_end()
end

function build_platform_test(name, config)
    target(name)
        add_rules("BuildPlatformTest")
        if config.sources then
            add_files(config.sources)
        end
        if config.libraries then
            for _, lib in ipairs(config.libraries) do
                add_deps(lib)
            end
        end
    target_end()
end
