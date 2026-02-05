-- Minimum Haiku image definition
-- Ported from build/jam/images/definitions/minimum
--
-- This file defines the content of the minimum Haiku image.
--
-- NOTE: xmake only exports FUNCTIONS from modules, not variables.
-- All data is stored in local tables and accessed via getter functions.

local ImageRules = import("rules.ImageRules")
local BuildFeatureRules = import("rules.BuildFeatureRules")
local HelperRules = import("rules.HelperRules")
local SystemLibraryRules = import("rules.SystemLibraryRules")

-- ============================================================================
-- System Binaries
-- ============================================================================

local _SYSTEM_BIN = BuildFeatureRules.FFilterByBuildFeatures({
    "addattr",
    "alert",
    "arp",
    "autologin",
    "beep",
    "bfsinfo",
    "catattr",
    "checkfs",
    "checkitout",
    "chop",
    "clear",
    "collectcatkeys",
    "copyattr",
    "desklink",
    "df",
    "diskimage",
    "draggers",
    "driveinfo",
    "dstcheck",
    "dumpcatalog",
    "eject",
    "error",
    "fdinfo",
    "ffm",
    "filepanel",
    "finddir",
    "findpaths",
    "fortune",
    "fstrim",
    "ftpd",
    "getarch",
    "groupadd",
    "groupdel",
    "groupmod",
    "hd",
    "hey",
    "ifconfig",
    "iroster",
    "isvolume",
    "kernel_debugger",
    "keymap",
    "keystore",
    "launch_roster",
    "linkcatkeys",
    "listarea",
    "listattr",
    "listimage",
    "listdev",
    "listfont",
    "listport",
    "listres",
    "listsem",
    "listusb",
    "locale",
    "logger",
    "login",
    "lsindex",
    "makebootable",
    "message",
    "mimeset",
    "mkfs",
    "mkindex",
    "modifiers",
    "mount",
    "mountvolume",
    "netstat",
    "notify",
    "open",
    "package",
    "package_repo",
    "passwd",
    "pc",
    "ping",
    "pkgman",
    "prio",
    "profile",
    "ps",
    "query",
    "quit",
    "ramdisk",
    "rc",
    "reindex",
    "release",
    "renice",
    "resattr",
    "resizefs",
    "rmattr",
    "rmindex",
    "roster",
    "route",
    "safemode",
    "screen_blanker",
    "screeninfo",
    "screenmode",
    "setarch",
    "setmime",
    "settype",
    "setversion",
    "setvolume",
    "shutdown",
    "strace",
    "su",
    "sysinfo",
    "system_time",
    "tcptester",
    "top",
    "traceroute",
    "trash",
    "unchop",
    "unmount",
    "urlwrapper",
    "useradd",
    "userdel",
    "version",
    "vmstat",
    "waitfor",
    "watch",
    "writembr@x86,x86_64",
    "xres",
})

-- ============================================================================
-- System Applications
-- ============================================================================

local _SYSTEM_APPS = BuildFeatureRules.FFilterByBuildFeatures({
    "AboutSystem",
    "BootManager@x86,x86_64",
    "CharacterMap",
    "Debugger@libedit",
    "DeskCalc",
    "Devices",
    "DiskProbe",
    "DiskUsage",
    "DriveSetup",
    "Expander",
    "Installer",
    "NetworkStatus",
    "ProcessController",
    "ShowImage",
    "StyledEdit",
    "Terminal",
    "TextSearch",
    "Workspaces",
})

-- ============================================================================
-- Deskbar Configuration
-- ============================================================================

local _DESKBAR_APPLICATIONS = {
    "CharacterMap",
    "DeskCalc",
    "Devices",
    "DiskProbe",
    "DiskUsage",
    "DriveSetup",
    "Expander",
    "Installer",
    "StyledEdit",
    "Terminal",
}

local _DESKBAR_DESKTOP_APPLETS = {
    "NetworkStatus",
    "ProcessController",
    "Workspaces",
}

-- ============================================================================
-- System Preferences
-- ============================================================================

local _SYSTEM_PREFERENCES = BuildFeatureRules.FFilterByBuildFeatures({
    "Appearance",
    "Backgrounds",
    "<preference>Deskbar",
    "FileTypes",
    "Input",
    "Keymap",
    "Locale",
    "Network",
    "Notifications",
    "Screen",
    "Shortcuts",
    "Time",
    "<preference>Tracker",
    "VirtualMemory",
})

-- ============================================================================
-- System Demos (empty for minimum)
-- ============================================================================

local _SYSTEM_DEMOS = {}

-- ============================================================================
-- System Servers
-- ============================================================================

local _SYSTEM_SERVERS = BuildFeatureRules.FFilterByBuildFeatures({
    "app_server",
    "debug_server",
    "dns_resolver_server",
    "input_server",
    "keystore_server",
    "launch_daemon",
    "mount_server",
    "net_server",
    "notification_server",
    "package_daemon",
    "power_daemon",
    "registrar",
    "syslog_daemon",
})

-- ============================================================================
-- Network Configuration
-- ============================================================================

local _SYSTEM_NETWORK_DEVICES = {
    "ethernet",
    "loopback",
    "tunnel",
}

local _SYSTEM_NETWORK_DATALINK_PROTOCOLS = {
    "<module>arp",
    "ethernet_frame",
    "ipv6_datagram",
    "loopback_frame",
}

-- SYSTEM_NETWORK_PPP = { "ipcp", "modem", "pap", "pppoe", "KPPPManager" }

local _SYSTEM_NETWORK_PROTOCOLS = {
    "icmp",
    "icmp6",
    "ipv4",
    "ipv6",
    "tcp",
    "udp",
    "unix",
}

-- ============================================================================
-- Add-ons: Accelerants
-- ============================================================================

local _SYSTEM_ADD_ONS_ACCELERANTS = BuildFeatureRules.FFilterByBuildFeatures({
    "framebuffer.accelerant",
    "vesa.accelerant@x86,x86_64",
    -- riscv64: ati for qemu, radeon_hd for unmatched
    "ati.accelerant@riscv64",
    "radeon_hd.accelerant@riscv64",
})

-- ============================================================================
-- Add-ons: Translators
-- ============================================================================

local _SYSTEM_ADD_ONS_TRANSLATORS = {
    "STXTTranslator",
}

-- ============================================================================
-- Add-ons: Locale Catalogs
-- ============================================================================

local _SYSTEM_ADD_ONS_LOCALE_CATALOGS = {
    "<catalog-addon>plaintext",
}

-- ============================================================================
-- Add-ons: Media (empty for minimum)
-- ============================================================================

local _SYSTEM_ADD_ONS_MEDIA = {}
local _SYSTEM_ADD_ONS_MEDIA_PLUGINS = {}

-- ============================================================================
-- Add-ons: Print (empty for minimum)
-- ============================================================================

local _SYSTEM_ADD_ONS_PRINT = {}
local _SYSTEM_ADD_ONS_PRINT_TRANSPORT = {}

-- ============================================================================
-- Add-ons: Screensavers (empty for minimum)
-- ============================================================================

local _SYSTEM_ADD_ONS_SCREENSAVERS = {}

-- ============================================================================
-- Add-ons: Drivers - Audio (empty for minimum)
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_AUDIO = {}
local _SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD = {}

-- ============================================================================
-- Add-ons: Drivers - Graphics
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_GRAPHICS = BuildFeatureRules.FFilterByBuildFeatures({
    "framebuffer",
    "vesa@x86,x86_64",
    -- riscv64: ati for qemu, radeon_hd for unmatched
    "ati@riscv64",
    "radeon_hd@riscv64",
})

-- ============================================================================
-- Add-ons: Drivers - MIDI (empty for minimum)
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_MIDI = {}

-- ============================================================================
-- Add-ons: Drivers - Network
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_NET = BuildFeatureRules.FFilterByBuildFeatures({
    -- x86, x86_64, riscv64 specific
    "atheros813x@x86,x86_64,riscv64",
    "atheros81xx@x86,x86_64,riscv64",
    "attansic_l1@x86,x86_64,riscv64",
    "attansic_l2@x86,x86_64,riscv64",
    "broadcom440x@x86,x86_64,riscv64",
    "broadcom570x@x86,x86_64,riscv64",
    "dec21xxx@x86,x86_64,riscv64",
    "ipro100@x86,x86_64,riscv64",
    "ipro1000@x86,x86_64,riscv64",
    "intel22x@x86,x86_64,riscv64",
    "jmicron2x0@x86,x86_64,riscv64",
    "marvell_yukon@x86,x86_64,riscv64",
    "pcnet@x86,x86_64,riscv64",
    "rtl8125@x86,x86_64,riscv64",
    "rtl8139@x86,x86_64,riscv64",
    "rtl81xx@x86,x86_64,riscv64",
    "sis19x@x86,x86_64,riscv64",
    "syskonnect@x86,x86_64,riscv64",
    "vmxnet@x86,x86_64,riscv64",
    "vt612x@x86,x86_64,riscv64",
    -- All architectures
    "etherpci",
    "pegasus",
    "usb_asix",
    "usb_davicom",
    "usb_rndis",
    "wb840",
})

-- ============================================================================
-- Add-ons: Drivers - Power
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_POWER = BuildFeatureRules.FFilterByBuildFeatures({
    "acpi_button@x86,x86_64",
    "acpi_thermal@x86,x86_64",
    "amd_thermal@x86,x86_64",
    "pch_thermal@x86,x86_64",
})

-- ============================================================================
-- Add-ons: Drivers - Sensor
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_SENSOR = BuildFeatureRules.FFilterByBuildFeatures({
    "acpi_als@x86,x86_64",
})

-- ============================================================================
-- Add-ons: Bus Managers
-- ============================================================================

local _SYSTEM_ADD_ONS_BUS_MANAGERS = BuildFeatureRules.FFilterByBuildFeatures({
    "acpi@x86,x86_64,arm64",
    "agp_gart@x86,x86_64",
    "ata",
    "i2c@x86,x86_64",
    "isa@x86,x86_64",
    "mmc",
    "scsi",
    "pci",
    "ps2@x86,x86_64",
    "fdt@riscv64,arm,arm64",
    "random",
    "usb",
    "virtio",
})

-- ============================================================================
-- Add-ons: File Systems
-- ============================================================================

local _SYSTEM_ADD_ONS_FILE_SYSTEMS = {
    "attribute_overlay",
    "bfs",
    "bindfs",
    "btrfs",
    "exfat",
    "ext2",
    "fat",
    "iso9660",
    "log_overlay",
    "ntfs",
    "packagefs",
    "ramfs",
    "udf",
    "write_overlay",
}

-- ============================================================================
-- System Libraries
-- ============================================================================

function HaikuImageGetSystemLibs()
    return HelperRules.TableMerge(
        -- libs with special grist
        HelperRules.MultiArchDefaultGristFiles({"libroot.so"}, "revisioned"),
        SystemLibraryRules.Libstdc_ForImage(),
        -- libs with standard grist
        HelperRules.MultiArchDefaultGristFiles(BuildFeatureRules.FFilterByBuildFeatures({
            "libbe.so",
            "libbsd.so",
            "libbnetapi.so",
            "libdebug.so",
            "libdebugger.so@primary",
            "libdevice.so",
            "libgnu.so",
            "libnetwork.so",
            "libpackage.so",
            "libtextencoding.so",
            "libtracker.so",
            "libtranslation.so",
        }))
    )
end

function HaikuImageGetPrivateSystemLibs()
    return HelperRules.MultiArchDefaultGristFiles(BuildFeatureRules.FFilterByBuildFeatures({
        "libalm.so",
        "libpackage-add-on-libsolv.so",
        "libroot-addon-icu.so",
    }))
end

-- ============================================================================
-- Image Content Setup
-- ============================================================================

function SetupMinimumImageContent()
    -- symlink to home on desktop
    ImageRules.AddSymlinkToHaikuImage({"home", "Desktop"}, "/boot/home", "Home")

    -- global settings when a package is installed in ~/config
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "settings", "global"})

    -- user scripts and data files
    local userBootScripts = {"UserBootscript", "UserSetupEnvironment.sample"}
    local config = import("core.project.config")
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    for _, script in ipairs(userBootScripts) do
        local search_path = path.join(haiku_top, "data", "config", "boot", script)
        ImageRules.AddFilesToHaikuImage({"home", "config", "settings", "boot"}, {search_path})
    end

    -- first login script
    local first_login = path.join(haiku_top, "data", "settings", "first_login")
    ImageRules.AddFilesToHaikuImage({"home", "config", "settings"}, {first_login}, "first_login")

    -- etc files
    local etcDir = path.join(haiku_top, "data", "etc")
    local etcFiles = {"inputrc", "profile"}
    for _, file in ipairs(etcFiles) do
        local file_path = path.join(etcDir, file)
        ImageRules.AddFilesToHaikuImage({"system", "settings", "etc"}, {file_path})
    end

    -- profile.d directory
    ImageRules.AddDirectoryToHaikuImage({"system", "settings", "etc", "profile.d"})
    -- Add .sh files from profile.d would go here

    -- driver settings
    local driverSettingsFile = path.join(haiku_top, "data", "settings", "kernel", "drivers", "kernel")
    ImageRules.AddFilesToHaikuImage({"home", "config", "settings", "kernel", "drivers"}, {driverSettingsFile}, "kernel")

    -- network settings
    local networkSettingsDir = path.join(haiku_top, "data", "settings", "network")
    ImageRules.AddFilesToHaikuImage({"system", "settings", "network"},
        {path.join(networkSettingsDir, "services")}, "services")
    ImageRules.AddFilesToHaikuImage({"system", "settings", "network"},
        {path.join(networkSettingsDir, "hosts")}, "hosts")

    -- repository config and cache files would be added here
    -- for repository in HAIKU_REPOSITORIES do ... end
end

-- ============================================================================
-- Getter Functions (xmake only exports functions, not variables)
-- ============================================================================

function SYSTEM_BIN() return _SYSTEM_BIN end
function SYSTEM_APPS() return _SYSTEM_APPS end
function DESKBAR_APPLICATIONS() return _DESKBAR_APPLICATIONS end
function DESKBAR_DESKTOP_APPLETS() return _DESKBAR_DESKTOP_APPLETS end
function SYSTEM_PREFERENCES() return _SYSTEM_PREFERENCES end
function SYSTEM_DEMOS() return _SYSTEM_DEMOS end
function SYSTEM_SERVERS() return _SYSTEM_SERVERS end
function SYSTEM_NETWORK_DEVICES() return _SYSTEM_NETWORK_DEVICES end
function SYSTEM_NETWORK_DATALINK_PROTOCOLS() return _SYSTEM_NETWORK_DATALINK_PROTOCOLS end
function SYSTEM_NETWORK_PROTOCOLS() return _SYSTEM_NETWORK_PROTOCOLS end
function SYSTEM_ADD_ONS_ACCELERANTS() return _SYSTEM_ADD_ONS_ACCELERANTS end
function SYSTEM_ADD_ONS_TRANSLATORS() return _SYSTEM_ADD_ONS_TRANSLATORS end
function SYSTEM_ADD_ONS_LOCALE_CATALOGS() return _SYSTEM_ADD_ONS_LOCALE_CATALOGS end
function SYSTEM_ADD_ONS_MEDIA() return _SYSTEM_ADD_ONS_MEDIA end
function SYSTEM_ADD_ONS_MEDIA_PLUGINS() return _SYSTEM_ADD_ONS_MEDIA_PLUGINS end
function SYSTEM_ADD_ONS_PRINT() return _SYSTEM_ADD_ONS_PRINT end
function SYSTEM_ADD_ONS_PRINT_TRANSPORT() return _SYSTEM_ADD_ONS_PRINT_TRANSPORT end
function SYSTEM_ADD_ONS_SCREENSAVERS() return _SYSTEM_ADD_ONS_SCREENSAVERS end
function SYSTEM_ADD_ONS_DRIVERS_AUDIO() return _SYSTEM_ADD_ONS_DRIVERS_AUDIO end
function SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD() return _SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD end
function SYSTEM_ADD_ONS_DRIVERS_GRAPHICS() return _SYSTEM_ADD_ONS_DRIVERS_GRAPHICS end
function SYSTEM_ADD_ONS_DRIVERS_MIDI() return _SYSTEM_ADD_ONS_DRIVERS_MIDI end
function SYSTEM_ADD_ONS_DRIVERS_NET() return _SYSTEM_ADD_ONS_DRIVERS_NET end
function SYSTEM_ADD_ONS_DRIVERS_POWER() return _SYSTEM_ADD_ONS_DRIVERS_POWER end
function SYSTEM_ADD_ONS_DRIVERS_SENSOR() return _SYSTEM_ADD_ONS_DRIVERS_SENSOR end
function SYSTEM_ADD_ONS_BUS_MANAGERS() return _SYSTEM_ADD_ONS_BUS_MANAGERS end
function SYSTEM_ADD_ONS_FILE_SYSTEMS() return _SYSTEM_ADD_ONS_FILE_SYSTEMS end
