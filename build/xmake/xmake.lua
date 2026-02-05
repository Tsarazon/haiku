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

-- Output directory setup - use "spawned" instead of default "build"
-- to keep xmake output separate from Jam's "generated"
set_config("buildir", "$(projectdir)/spawned")

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
if is_plat("haiku") then

    -- Target architecture setup
    local target_arch = get_config("arch") or "x86_64"

    -- Toolchain prefix
    local toolchain_prefix = target_arch .. "-unknown-haiku-"

    -- Set toolchain
    set_toolchains("gcc")

    -- Global compilation flags for Haiku target
    add_cxflags("-D_GNU_SOURCE")
    add_cxflags("-D__HAIKU__")

    -- Architecture-specific defines
    if target_arch == "x86_64" then
        add_defines("__x86_64__")
        add_defines("__HAIKU_ARCH_64_BIT")
    elseif target_arch == "x86" then
        add_defines("__i386__")
        add_defines("__HAIKU_ARCH_32_BIT")
    elseif target_arch == "arm64" then
        add_defines("__aarch64__")
        add_defines("__HAIKU_ARCH_64_BIT")
    elseif target_arch == "riscv64" then
        add_defines("__riscv")
        add_defines("__riscv_xlen=64")
        add_defines("__HAIKU_ARCH_64_BIT")
    end

    -- Standard include paths
    add_includedirs(
        "$(projectdir)/headers/os",
        "$(projectdir)/headers/os/app",
        "$(projectdir)/headers/os/interface",
        "$(projectdir)/headers/os/storage",
        "$(projectdir)/headers/os/support",
        "$(projectdir)/headers/os/kernel",
        "$(projectdir)/headers/posix",
        "$(projectdir)/headers/private"
    )

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

-- Packaging architecture (for hybrid builds)
option("packaging_arch")
    set_default("")
    set_showmenu(true)
    set_description("Packaging architecture for hybrid builds")

-- Secondary architecture
option("secondary_arch")
    set_default("")
    set_showmenu(true)
    set_description("Secondary architecture (e.g., x86 on x86_64)")

-- Debug level
option("debug_level")
    set_default(0)
    set_showmenu(true)
    set_description("Debug level (0=release, 1=debug)")
    set_values("0", "1")

-- Warnings level
option("warnings")
    set_default(1)
    set_showmenu(true)
    set_description("Warning level (0=off, 1=on, 2=treat as errors)")
    set_values("0", "1", "2")

-- ============================================================================
-- Output Directories
-- ============================================================================

-- Note: buildir is set at the top of the file via set_config("buildir", ...)
-- Individual targets can use set_objectdir/set_targetdir to customize their output paths

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
end

function haiku_static_library(name, config)
    target(name)
        add_rules("StaticLibrary")
        if config.sources then
            add_files(config.sources)
        end
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
end

function build_platform_static_library(name, config)
    target(name)
        add_rules("BuildPlatformStaticLibrary")
        if config.sources then
            add_files(config.sources)
        end
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
end
