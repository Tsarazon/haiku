-- Bootstrap Haiku image definition
-- Ported from build/jam/images/definitions/bootstrap
--
-- This file defines the content of the bootstrap Haiku image.
-- It imports everything from minimum and adds bootstrap-specific content.
--
-- NOTE: xmake only exports FUNCTIONS from modules, not variables.
-- Bootstrap simply delegates to minimum's getter functions.

local ImageRules = import("rules.ImageRules")
local BuildFeatureRules = import("rules.BuildFeatureRules")
local HelperRules = import("rules.HelperRules")
local SystemLibraryRules = import("rules.SystemLibraryRules")

-- Import minimum image definitions (bootstrap is based on minimum)
local minimum = import("images.definitions.minimum")

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

    local config = import("core.project.config")
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))

    -- Build and add the source package directory and a haikuports.config file
    local source_pkg_dir = BuildHaikuPortsSourcePackageDirectory()
    if source_pkg_dir then
        ImageRules.CopyDirectoryToHaikuImage(
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
            ImageRules.AddFilesToHaikuImage({"home", "haikuports"}, {formatVersionsFile}, "FormatVersions")
        end
    end

    -- Bootstrap daemon
    local bootstrapDaemon = path.join(haiku_top, "build", "scripts", "bootstrap_daemon.py")
    if os.isfile(bootstrapDaemon) then
        ImageRules.AddFilesToHaikuImage(
            {"home", "config", "settings", "boot", "launch"},
            {bootstrapDaemon},
            "bootstrap_daemon.py"
        )
    end
end

-- ============================================================================
-- Getter Functions - Delegate to minimum (xmake only exports functions)
-- ============================================================================

function SYSTEM_BIN() return minimum.SYSTEM_BIN() end
function SYSTEM_APPS() return minimum.SYSTEM_APPS() end
function DESKBAR_APPLICATIONS() return minimum.DESKBAR_APPLICATIONS() end
function DESKBAR_DESKTOP_APPLETS() return minimum.DESKBAR_DESKTOP_APPLETS() end
function SYSTEM_PREFERENCES() return minimum.SYSTEM_PREFERENCES() end
function SYSTEM_DEMOS() return minimum.SYSTEM_DEMOS() end
function SYSTEM_SERVERS() return minimum.SYSTEM_SERVERS() end
function SYSTEM_NETWORK_DEVICES() return minimum.SYSTEM_NETWORK_DEVICES() end
function SYSTEM_NETWORK_DATALINK_PROTOCOLS() return minimum.SYSTEM_NETWORK_DATALINK_PROTOCOLS() end
function SYSTEM_NETWORK_PROTOCOLS() return minimum.SYSTEM_NETWORK_PROTOCOLS() end
function SYSTEM_ADD_ONS_ACCELERANTS() return minimum.SYSTEM_ADD_ONS_ACCELERANTS() end
function SYSTEM_ADD_ONS_TRANSLATORS() return minimum.SYSTEM_ADD_ONS_TRANSLATORS() end
function SYSTEM_ADD_ONS_LOCALE_CATALOGS() return minimum.SYSTEM_ADD_ONS_LOCALE_CATALOGS() end
function SYSTEM_ADD_ONS_MEDIA() return minimum.SYSTEM_ADD_ONS_MEDIA() end
function SYSTEM_ADD_ONS_MEDIA_PLUGINS() return minimum.SYSTEM_ADD_ONS_MEDIA_PLUGINS() end
function SYSTEM_ADD_ONS_PRINT() return minimum.SYSTEM_ADD_ONS_PRINT() end
function SYSTEM_ADD_ONS_PRINT_TRANSPORT() return minimum.SYSTEM_ADD_ONS_PRINT_TRANSPORT() end
function SYSTEM_ADD_ONS_SCREENSAVERS() return minimum.SYSTEM_ADD_ONS_SCREENSAVERS() end
function SYSTEM_ADD_ONS_DRIVERS_AUDIO() return minimum.SYSTEM_ADD_ONS_DRIVERS_AUDIO() end
function SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD() return minimum.SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD() end
function SYSTEM_ADD_ONS_DRIVERS_GRAPHICS() return minimum.SYSTEM_ADD_ONS_DRIVERS_GRAPHICS() end
function SYSTEM_ADD_ONS_DRIVERS_MIDI() return minimum.SYSTEM_ADD_ONS_DRIVERS_MIDI() end
function SYSTEM_ADD_ONS_DRIVERS_NET() return minimum.SYSTEM_ADD_ONS_DRIVERS_NET() end
function SYSTEM_ADD_ONS_DRIVERS_POWER() return minimum.SYSTEM_ADD_ONS_DRIVERS_POWER() end
function SYSTEM_ADD_ONS_DRIVERS_SENSOR() return minimum.SYSTEM_ADD_ONS_DRIVERS_SENSOR() end
function SYSTEM_ADD_ONS_BUS_MANAGERS() return minimum.SYSTEM_ADD_ONS_BUS_MANAGERS() end
function SYSTEM_ADD_ONS_FILE_SYSTEMS() return minimum.SYSTEM_ADD_ONS_FILE_SYSTEMS() end
function HaikuImageGetSystemLibs() return minimum.HaikuImageGetSystemLibs() end
function HaikuImageGetPrivateSystemLibs() return minimum.HaikuImageGetPrivateSystemLibs() end
