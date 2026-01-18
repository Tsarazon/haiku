-- Minimum Haiku image definition
-- Ported from build/jam/images/definitions/minimum
--
-- This file defines the content of the minimum Haiku image.

import("rules.ImageRules")
import("rules.BuildFeatureRules")

-- ============================================================================
-- System Binaries
-- ============================================================================

SYSTEM_BIN = FFilterByBuildFeatures({
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

SYSTEM_APPS = FFilterByBuildFeatures({
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

DESKBAR_APPLICATIONS = {
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

DESKBAR_DESKTOP_APPLETS = {
    "NetworkStatus",
    "ProcessController",
    "Workspaces",
}

-- ============================================================================
-- System Preferences
-- ============================================================================

SYSTEM_PREFERENCES = FFilterByBuildFeatures({
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

SYSTEM_DEMOS = {}

-- ============================================================================
-- System Libraries
-- ============================================================================

function HaikuImageGetSystemLibs()
    return TableMerge(
        -- libs with special grist
        MultiArchDefaultGristFiles({"libroot.so"}, "revisioned"),
        Libstdc++ForImage(),
        -- libs with standard grist
        MultiArchDefaultGristFiles(FFilterByBuildFeatures({
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
    return MultiArchDefaultGristFiles(FFilterByBuildFeatures({
        "libalm.so",
        "libpackage-add-on-libsolv.so",
        "libroot-addon-icu.so",
    }))
end

-- ============================================================================
-- System Servers
-- ============================================================================

SYSTEM_SERVERS = FFilterByBuildFeatures({
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

SYSTEM_NETWORK_DEVICES = {
    "ethernet",
    "loopback",
    "tunnel",
}

SYSTEM_NETWORK_DATALINK_PROTOCOLS = {
    "<module>arp",
    "ethernet_frame",
    "ipv6_datagram",
    "loopback_frame",
}

-- SYSTEM_NETWORK_PPP = { "ipcp", "modem", "pap", "pppoe", "KPPPManager" }

SYSTEM_NETWORK_PROTOCOLS = {
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

SYSTEM_ADD_ONS_ACCELERANTS = FFilterByBuildFeatures({
    "framebuffer.accelerant",
    "vesa.accelerant@x86,x86_64",
    -- riscv64: ati for qemu, radeon_hd for unmatched
    "ati.accelerant@riscv64",
    "radeon_hd.accelerant@riscv64",
})

-- ============================================================================
-- Add-ons: Translators
-- ============================================================================

SYSTEM_ADD_ONS_TRANSLATORS = {
    "STXTTranslator",
}

-- ============================================================================
-- Add-ons: Locale Catalogs
-- ============================================================================

SYSTEM_ADD_ONS_LOCALE_CATALOGS = {
    "<catalog-addon>plaintext",
}

-- ============================================================================
-- Add-ons: Media (empty for minimum)
-- ============================================================================

SYSTEM_ADD_ONS_MEDIA = {}
SYSTEM_ADD_ONS_MEDIA_PLUGINS = {}

-- ============================================================================
-- Add-ons: Print (empty for minimum)
-- ============================================================================

SYSTEM_ADD_ONS_PRINT = {}
SYSTEM_ADD_ONS_PRINT_TRANSPORT = {}

-- ============================================================================
-- Add-ons: Screensavers (empty for minimum)
-- ============================================================================

SYSTEM_ADD_ONS_SCREENSAVERS = {}

-- ============================================================================
-- Add-ons: Drivers - Audio (empty for minimum)
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_AUDIO = {}
SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD = {}

-- ============================================================================
-- Add-ons: Drivers - Graphics
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_GRAPHICS = FFilterByBuildFeatures({
    "framebuffer",
    "vesa@x86,x86_64",
    -- riscv64: ati for qemu, radeon_hd for unmatched
    "ati@riscv64",
    "radeon_hd@riscv64",
})

-- ============================================================================
-- Add-ons: Drivers - MIDI (empty for minimum)
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_MIDI = {}

-- ============================================================================
-- Add-ons: Drivers - Network
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_NET = FFilterByBuildFeatures({
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

SYSTEM_ADD_ONS_DRIVERS_POWER = FFilterByBuildFeatures({
    "acpi_button@x86,x86_64",
    "acpi_thermal@x86,x86_64",
    "amd_thermal@x86,x86_64",
    "pch_thermal@x86,x86_64",
})

-- ============================================================================
-- Add-ons: Drivers - Sensor
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_SENSOR = FFilterByBuildFeatures({
    "acpi_als@x86,x86_64",
})

-- ============================================================================
-- Add-ons: Bus Managers
-- ============================================================================

SYSTEM_ADD_ONS_BUS_MANAGERS = FFilterByBuildFeatures({
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

SYSTEM_ADD_ONS_FILE_SYSTEMS = {
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
-- Image Content Setup
-- ============================================================================

function SetupMinimumImageContent()
    -- symlink to home on desktop
    AddSymlinkToHaikuImage({"home", "Desktop"}, "/boot/home", "Home")

    -- global settings when a package is installed in ~/config
    AddDirectoryToHaikuImage({"home", "config", "settings", "global"})

    -- user scripts and data files
    local userBootScripts = {"UserBootscript", "UserSetupEnvironment.sample"}
    local haiku_top = os.projectdir()
    for _, script in ipairs(userBootScripts) do
        local search_path = path.join(haiku_top, "data", "config", "boot", script)
        AddFilesToHaikuImage({"home", "config", "settings", "boot"}, {search_path})
    end

    -- first login script
    local first_login = path.join(haiku_top, "data", "settings", "first_login")
    AddFilesToHaikuImage({"home", "config", "settings"}, {first_login}, "first_login")

    -- etc files
    local etcDir = path.join(haiku_top, "data", "etc")
    local etcFiles = {"inputrc", "profile"}
    for _, file in ipairs(etcFiles) do
        local file_path = path.join(etcDir, file)
        AddFilesToHaikuImage({"system", "settings", "etc"}, {file_path})
    end

    -- profile.d directory
    AddDirectoryToHaikuImage({"system", "settings", "etc", "profile.d"})
    -- Add .sh files from profile.d would go here

    -- driver settings
    local driverSettingsFile = path.join(haiku_top, "data", "settings", "kernel", "drivers", "kernel")
    AddFilesToHaikuImage({"home", "config", "settings", "kernel", "drivers"}, {driverSettingsFile}, "kernel")

    -- network settings
    local networkSettingsDir = path.join(haiku_top, "data", "settings", "network")
    AddFilesToHaikuImage({"system", "settings", "network"},
        {path.join(networkSettingsDir, "services")}, "services")
    AddFilesToHaikuImage({"system", "settings", "network"},
        {path.join(networkSettingsDir, "hosts")}, "hosts")

    -- repository config and cache files would be added here
    -- for repository in HAIKU_REPOSITORIES do ... end
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
    SetupMinimumImageContent = SetupMinimumImageContent,
}