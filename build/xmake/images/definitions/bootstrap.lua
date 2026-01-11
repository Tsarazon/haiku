-- Bootstrap Haiku image definition
-- Ported from build/jam/images/definitions/bootstrap
--
-- This file defines the content of the bootstrap Haiku image.
-- It imports everything from minimum and adds bootstrap-specific content.

import("rules.ImageRules")
import("rules.BuildFeatureRules")

-- Import minimum image definitions (bootstrap is based on minimum)
local minimum = import("images.definitions.minimum")

-- ============================================================================
-- Inherit everything from Minimum
-- ============================================================================

SYSTEM_BIN = minimum.SYSTEM_BIN
SYSTEM_APPS = minimum.SYSTEM_APPS
DESKBAR_APPLICATIONS = minimum.DESKBAR_APPLICATIONS
DESKBAR_DESKTOP_APPLETS = minimum.DESKBAR_DESKTOP_APPLETS
SYSTEM_PREFERENCES = minimum.SYSTEM_PREFERENCES
SYSTEM_DEMOS = minimum.SYSTEM_DEMOS
SYSTEM_SERVERS = minimum.SYSTEM_SERVERS
SYSTEM_NETWORK_DEVICES = minimum.SYSTEM_NETWORK_DEVICES
SYSTEM_NETWORK_DATALINK_PROTOCOLS = minimum.SYSTEM_NETWORK_DATALINK_PROTOCOLS
SYSTEM_NETWORK_PROTOCOLS = minimum.SYSTEM_NETWORK_PROTOCOLS
SYSTEM_ADD_ONS_ACCELERANTS = minimum.SYSTEM_ADD_ONS_ACCELERANTS
SYSTEM_ADD_ONS_TRANSLATORS = minimum.SYSTEM_ADD_ONS_TRANSLATORS
SYSTEM_ADD_ONS_LOCALE_CATALOGS = minimum.SYSTEM_ADD_ONS_LOCALE_CATALOGS
SYSTEM_ADD_ONS_MEDIA = minimum.SYSTEM_ADD_ONS_MEDIA
SYSTEM_ADD_ONS_MEDIA_PLUGINS = minimum.SYSTEM_ADD_ONS_MEDIA_PLUGINS
SYSTEM_ADD_ONS_PRINT = minimum.SYSTEM_ADD_ONS_PRINT
SYSTEM_ADD_ONS_PRINT_TRANSPORT = minimum.SYSTEM_ADD_ONS_PRINT_TRANSPORT
SYSTEM_ADD_ONS_SCREENSAVERS = minimum.SYSTEM_ADD_ONS_SCREENSAVERS
SYSTEM_ADD_ONS_DRIVERS_AUDIO = minimum.SYSTEM_ADD_ONS_DRIVERS_AUDIO
SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD = minimum.SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD
SYSTEM_ADD_ONS_DRIVERS_GRAPHICS = minimum.SYSTEM_ADD_ONS_DRIVERS_GRAPHICS
SYSTEM_ADD_ONS_DRIVERS_MIDI = minimum.SYSTEM_ADD_ONS_DRIVERS_MIDI
SYSTEM_ADD_ONS_DRIVERS_NET = minimum.SYSTEM_ADD_ONS_DRIVERS_NET
SYSTEM_ADD_ONS_DRIVERS_POWER = minimum.SYSTEM_ADD_ONS_DRIVERS_POWER
SYSTEM_ADD_ONS_DRIVERS_SENSOR = minimum.SYSTEM_ADD_ONS_DRIVERS_SENSOR
SYSTEM_ADD_ONS_BUS_MANAGERS = minimum.SYSTEM_ADD_ONS_BUS_MANAGERS
SYSTEM_ADD_ONS_FILE_SYSTEMS = minimum.SYSTEM_ADD_ONS_FILE_SYSTEMS

-- Use minimum's library functions
HaikuImageGetSystemLibs = minimum.HaikuImageGetSystemLibs
HaikuImageGetPrivateSystemLibs = minimum.HaikuImageGetPrivateSystemLibs

-- ============================================================================
-- Bootstrap-Specific Functions
-- ============================================================================

--[[
    BuildHaikuPortsSourcePackageDirectory

    Builds the HaikuPorts source package directory for bootstrap.
]]
local function BuildHaikuPortsSourcePackageDirectory()
    local haiku_ports = get_config("haiku_ports") or os.getenv("HAIKU_PORTS")
    if not haiku_ports then
        return nil
    end
    return path.join(haiku_ports, "input-source-packages")
end

--[[
    BuildHaikuPortsRepositoryConfig

    Builds the haikuports.config file for the specified directory.
]]
local function BuildHaikuPortsRepositoryConfig(target_dir)
    local config_content = [[
# HaikuPorts repository configuration for bootstrap
PACKAGER="Your Name <your@email.com>"
TREE_PATH=]] .. target_dir .. [[

]]
    return config_content
end

-- ============================================================================
-- Image Content Setup (extends minimum)
-- ============================================================================

function SetupBootstrapImageContent()
    -- First setup minimum content
    minimum.SetupMinimumImageContent()

    local haiku_top = os.projectdir()

    -- Build and add the source package directory and a haikuports.config file
    local source_pkg_dir = BuildHaikuPortsSourcePackageDirectory()
    if source_pkg_dir then
        CopyDirectoryToHaikuImage(
            {"home", "haikuports"},
            source_pkg_dir,
            "input-source-packages",
            {"*_source-*.hpkg"},  -- exclude patterns
            {isTarget = true}
        )
    end

    -- Add haikuports.config file
    local haikuports_config = BuildHaikuPortsRepositoryConfig("/boot/home/haikuports")
    if haikuports_config then
        -- Write config to a temp file and add it
        -- In real implementation would use AddFilesToHaikuImage
    end

    -- Copy the haikuports format versions file
    local haiku_ports = get_config("haiku_ports") or os.getenv("HAIKU_PORTS")
    if haiku_ports then
        local formatVersionsFile = path.join(haiku_ports, "FormatVersions")
        if os.isfile(formatVersionsFile) then
            AddFilesToHaikuImage({"home", "haikuports"}, {formatVersionsFile}, "FormatVersions")
        end
    end

    -- Bootstrap daemon
    local bootstrapDaemon = path.join(haiku_top, "build", "scripts", "bootstrap_daemon.py")
    if os.isfile(bootstrapDaemon) then
        AddFilesToHaikuImage(
            {"home", "config", "settings", "boot", "launch"},
            {bootstrapDaemon},
            "bootstrap_daemon.py"
        )
    end
end

-- ============================================================================
-- Module Exports
-- ============================================================================

return {
    SYSTEM_BIN = SYSTEM_BIN,
    SYSTEM_APPS = SYSTEM_APPS,
    DESKBAR_APPLICATIONS = DESKBAR_APPLICATIONS,
    DESKBAR_DESKTOP_APPLETS = DESKBAR_DESKTOP_APPLETS,
    SYSTEM_PREFERENCES = SYSTEM_PREFERENCES,
    SYSTEM_DEMOS = SYSTEM_DEMOS,
    SYSTEM_SERVERS = SYSTEM_SERVERS,
    SYSTEM_NETWORK_DEVICES = SYSTEM_NETWORK_DEVICES,
    SYSTEM_NETWORK_DATALINK_PROTOCOLS = SYSTEM_NETWORK_DATALINK_PROTOCOLS,
    SYSTEM_NETWORK_PROTOCOLS = SYSTEM_NETWORK_PROTOCOLS,
    SYSTEM_ADD_ONS_ACCELERANTS = SYSTEM_ADD_ONS_ACCELERANTS,
    SYSTEM_ADD_ONS_TRANSLATORS = SYSTEM_ADD_ONS_TRANSLATORS,
    SYSTEM_ADD_ONS_LOCALE_CATALOGS = SYSTEM_ADD_ONS_LOCALE_CATALOGS,
    SYSTEM_ADD_ONS_MEDIA = SYSTEM_ADD_ONS_MEDIA,
    SYSTEM_ADD_ONS_MEDIA_PLUGINS = SYSTEM_ADD_ONS_MEDIA_PLUGINS,
    SYSTEM_ADD_ONS_PRINT = SYSTEM_ADD_ONS_PRINT,
    SYSTEM_ADD_ONS_PRINT_TRANSPORT = SYSTEM_ADD_ONS_PRINT_TRANSPORT,
    SYSTEM_ADD_ONS_SCREENSAVERS = SYSTEM_ADD_ONS_SCREENSAVERS,
    SYSTEM_ADD_ONS_DRIVERS_AUDIO = SYSTEM_ADD_ONS_DRIVERS_AUDIO,
    SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD = SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD,
    SYSTEM_ADD_ONS_DRIVERS_GRAPHICS = SYSTEM_ADD_ONS_DRIVERS_GRAPHICS,
    SYSTEM_ADD_ONS_DRIVERS_MIDI = SYSTEM_ADD_ONS_DRIVERS_MIDI,
    SYSTEM_ADD_ONS_DRIVERS_NET = SYSTEM_ADD_ONS_DRIVERS_NET,
    SYSTEM_ADD_ONS_DRIVERS_POWER = SYSTEM_ADD_ONS_DRIVERS_POWER,
    SYSTEM_ADD_ONS_DRIVERS_SENSOR = SYSTEM_ADD_ONS_DRIVERS_SENSOR,
    SYSTEM_ADD_ONS_BUS_MANAGERS = SYSTEM_ADD_ONS_BUS_MANAGERS,
    SYSTEM_ADD_ONS_FILE_SYSTEMS = SYSTEM_ADD_ONS_FILE_SYSTEMS,
    HaikuImageGetSystemLibs = HaikuImageGetSystemLibs,
    HaikuImageGetPrivateSystemLibs = HaikuImageGetPrivateSystemLibs,
    SetupBootstrapImageContent = SetupBootstrapImageContent,
    BuildHaikuPortsSourcePackageDirectory = BuildHaikuPortsSourcePackageDirectory,
    BuildHaikuPortsRepositoryConfig = BuildHaikuPortsRepositoryConfig,
}