-- Regular Haiku image definition
-- Ported from build/jam/images/definitions/regular
--
-- This file defines the content of the regular Haiku image.
-- It imports everything from minimum and adds additional content.

import("rules.ImageRules")
import("rules.BuildFeatureRules")

-- Import minimum image definitions
local minimum = import("images.definitions.minimum")

-- ============================================================================
-- Inherit from Minimum
-- ============================================================================

-- Start with minimum system binaries and extend
SYSTEM_BIN = TableCopy(minimum.SYSTEM_BIN)

-- Add userguide, quicktour, welcome scripts
local haiku_top = os.projectdir()
local additional_bin_scripts = {"userguide", "quicktour", "welcome"}
for _, script in ipairs(additional_bin_scripts) do
    table.insert(SYSTEM_BIN, script)
end

-- Add more system binaries
local additional_system_bin = FFilterByBuildFeatures({
    "cddb_lookup",
    "clipboard",
    "CortexAddOnHost",
    "dpms",
    "FirstBootPrompt",
    "<bin>i2c@x86,x86_64",
    "installsound",
    "mail",
    "mail2mbox",
    "mbox2mail",
    "media_client",
    "mount_nfs",
    "pidof",
    "recover",
    "screenshot",
    "setcontrollook",
    "setdecor",
    "spamdbm",
    "translate",
    "WindowShade",
})
for _, bin in ipairs(additional_system_bin) do
    table.insert(SYSTEM_BIN, bin)
end

-- ============================================================================
-- System Applications (extended)
-- ============================================================================

SYSTEM_APPS = TableCopy(minimum.SYSTEM_APPS)

local additional_system_apps = FFilterByBuildFeatures({
    "ActivityMonitor",
    "AutoRaise",
    "CodyCam",
    "GLInfo@mesa",
    "HaikuDepot",
    "Icon-O-Matic@expat",
    "LaunchBox",
    "LegacyPackageInstaller",
    "Magnify",
    "Mail",
    "MediaConverter",
    "MediaPlayer",
    "MidiPlayer",
    "People",
    "PoorMan",
    "PowerStatus",
    "RemoteDesktop",
    "Screenshot",
    "SerialConnect",
    "SoftwareUpdater",
    "SoundRecorder",
})
for _, app in ipairs(additional_system_apps) do
    table.insert(SYSTEM_APPS, app)
end

-- ============================================================================
-- Deskbar Configuration (extended)
-- ============================================================================

DESKBAR_APPLICATIONS = TableCopy(minimum.DESKBAR_APPLICATIONS)
local additional_deskbar_apps = {
    "ActivityMonitor",
    "CodyCam",
    "HaikuDepot",
    "Icon-O-Matic",
    "Magnify",
    "Mail",
    "MediaConverter",
    "MediaPlayer",
    "MidiPlayer",
    "People",
    "PoorMan",
    "SerialConnect",
    "SoftwareUpdater",
    "SoundRecorder",
}
for _, app in ipairs(additional_deskbar_apps) do
    table.insert(DESKBAR_APPLICATIONS, app)
end

DESKBAR_DESKTOP_APPLETS = TableCopy(minimum.DESKBAR_DESKTOP_APPLETS)
local additional_deskbar_applets = {
    "AutoRaise",
    "LaunchBox",
    "PowerStatus",
}
for _, applet in ipairs(additional_deskbar_applets) do
    table.insert(DESKBAR_DESKTOP_APPLETS, applet)
end

-- ============================================================================
-- System Preferences (extended)
-- ============================================================================

SYSTEM_PREFERENCES = TableCopy(minimum.SYSTEM_PREFERENCES)
local additional_preferences = FFilterByBuildFeatures({
    "Bluetooth",
    "DataTranslations",
    "E-mail",
    "Media",
    "Printers",
    "Repositories",
    "ScreenSaver",
    "Sounds",
})
for _, pref in ipairs(additional_preferences) do
    table.insert(SYSTEM_PREFERENCES, pref)
end

-- ============================================================================
-- System Demos
-- ============================================================================

SYSTEM_DEMOS = FFilterByBuildFeatures({
    "Chart",
    "Clock",
    "Cortex@expat",
    "FontDemo",
    "GLTeapot@mesa",
    "Haiku3d@mesa",  -- HAIKU_INCLUDE_TRADEMARKS conditional
    "Mandelbrot",
    "OverlayImage",
    "Pairs",
    "PatchBay",
    "Playground",
    "Pulse",
    "Sudoku",
    "ThorVGDemo",
})

-- ============================================================================
-- System Libraries (override)
-- ============================================================================

function HaikuImageGetSystemLibs()
    return TableMerge(
        -- libs with special grist
        MultiArchDefaultGristFiles({"libroot.so"}, "revisioned"),
        Libstdc++ForImage(),
        -- libs with standard grist
        MultiArchDefaultGristFiles(FFilterByBuildFeatures({
            "libalm.so",
            "libbe.so",
            "libbsd.so",
            "libbnetapi.so",
            "libbluetooth.so",
            "libdebug.so",
            "libdebugger.so@primary",
            "libdevice.so",
            "libgame.so",
            "libglut.so@mesa",
            "libgnu.so",
            "libmail.so",
            "libmedia.so",
            "libmidi.so",
            "libmidi2.so",
            "libnetwork.so",
            "libpackage.so",
            "libscreensaver.so",
            "libtextencoding.so",
            "libthorvg.so",
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
-- System Servers (extended)
-- ============================================================================

SYSTEM_SERVERS = TableCopy(minimum.SYSTEM_SERVERS)
local additional_servers = FFilterByBuildFeatures({
    "mail_daemon",
    "media_addon_server",
    "media_server",
    "midi_server",
    "nfs4_idmapper_server",
    "bluetooth_server",
    "print_server",
    "print_addon_server",
})
for _, server in ipairs(additional_servers) do
    table.insert(SYSTEM_SERVERS, server)
end

-- ============================================================================
-- Network Configuration (extended)
-- ============================================================================

SYSTEM_NETWORK_DEVICES = TableCopy(minimum.SYSTEM_NETWORK_DEVICES)
SYSTEM_NETWORK_DATALINK_PROTOCOLS = TableCopy(minimum.SYSTEM_NETWORK_DATALINK_PROTOCOLS)
SYSTEM_NETWORK_PROTOCOLS = TableCopy(minimum.SYSTEM_NETWORK_PROTOCOLS)

-- Bluetooth stack + drivers
-- SYSTEM_NETWORK_PROTOCOLS += "l2cap"

SYSTEM_BT_STACK = {
    "hci",
    "btCoreData",
}

SYSTEM_ADD_ONS_DRIVERS_BT_H2 = {
    "h2generic",
}

-- ============================================================================
-- Add-ons: Accelerants (extended)
-- ============================================================================

SYSTEM_ADD_ONS_ACCELERANTS = TableCopy(minimum.SYSTEM_ADD_ONS_ACCELERANTS)
local additional_accelerants = FFilterByBuildFeatures({
    "ati.accelerant@x86,x86_64",
    "intel_extreme.accelerant@x86,x86_64",
    "radeon_hd.accelerant@x86,x86_64",
    "virtio_gpu.accelerant",
})
for _, acc in ipairs(additional_accelerants) do
    table.insert(SYSTEM_ADD_ONS_ACCELERANTS, acc)
end

-- ============================================================================
-- Add-ons: Translators (extended)
-- ============================================================================

SYSTEM_ADD_ONS_TRANSLATORS = TableCopy(minimum.SYSTEM_ADD_ONS_TRANSLATORS)
local additional_translators = FFilterByBuildFeatures({
    "AVIFTranslator@libavif",
    "BMPTranslator",
    "GIFTranslator",
    "HVIFTranslator",
    "ICOTranslator",
    "ICNSTranslator@libicns",
    "JPEGTranslator@jpeg",
    "JPEG2000Translator@jasper",
    "PCXTranslator",
    "PNGTranslator@libpng",
    "PPMTranslator",
    "PSDTranslator",
    "RAWTranslator",
    "RTFTranslator",
    "SGITranslator",
    "TGATranslator",
    "TIFFTranslator@tiff",
    "WebPTranslator@libwebp",
    "WonderBrushTranslator",
})
for _, trans in ipairs(additional_translators) do
    table.insert(SYSTEM_ADD_ONS_TRANSLATORS, trans)
end

-- ============================================================================
-- Add-ons: Locale Catalogs
-- ============================================================================

SYSTEM_ADD_ONS_LOCALE_CATALOGS = TableCopy(minimum.SYSTEM_ADD_ONS_LOCALE_CATALOGS)

-- ============================================================================
-- Add-ons: Media
-- ============================================================================

SYSTEM_ADD_ONS_MEDIA = FFilterByBuildFeatures({
    "cortex_audioadapter.media_addon",
    "cortex_flanger.media_addon",
    "cortex_logging_consumer.media_addon",
    "equalizer.media_addon",
    "hmulti_audio.media_addon",
    -- "legacy.media_addon",
    "mixer.media_addon",
    "opensound.media_addon",
    "tone_producer_demo.media_addon",
    "usb_webcam.media_addon",
    "video_producer_demo.media_addon",
    "video_window_demo.media_addon",
    "vst_host.media_addon",
})

-- ============================================================================
-- Add-ons: Media Plugins
-- ============================================================================

SYSTEM_ADD_ONS_MEDIA_PLUGINS = FFilterByBuildFeatures({
    "ape_reader@x86",
    "ffmpeg@ffmpeg",
    "http_streamer",
    "raw_decoder",
})

-- ============================================================================
-- Add-ons: Print
-- ============================================================================

SYSTEM_ADD_ONS_PRINT = FFilterByBuildFeatures({
    "Canon LIPS3 Compatible",
    "Canon LIPS4 Compatible",
    "Gutenprint@gutenprint",
    "PCL5 Compatible",
    "PCL6 Compatible",
    "PS Compatible",
    "Preview",
})

SYSTEM_ADD_ONS_PRINT_TRANSPORT = {
    "HP JetDirect",
    "IPP",
    "LPR",
    -- "Parallel Port",
    "Print To File",
    "Serial Port",
    "USB Port",
}

-- ============================================================================
-- Add-ons: Screensavers
-- ============================================================================

SYSTEM_ADD_ONS_SCREENSAVERS = FFilterByBuildFeatures({
    "Butterfly",
    "DebugNow",
    "Flurry@mesa",
    "GLife@mesa",
    "Gravity@mesa",
    "Icons",
    "IFS",
    "Leaves",
    "Message",
    "Nebula",
    "Shelf",
    "Spider",
})

-- ============================================================================
-- Add-ons: Drivers - Audio
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_AUDIO = {
    "hda",
    "usb_audio",
}

SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD = {
    -- "cmedia",
}

-- ============================================================================
-- Add-ons: Drivers - Graphics (extended)
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_GRAPHICS = TableCopy(minimum.SYSTEM_ADD_ONS_DRIVERS_GRAPHICS)
local additional_graphics = FFilterByBuildFeatures({
    "ati@x86,x86_64",
    "intel_extreme@x86,x86_64",
    "radeon_hd@x86,x86_64",
})
for _, drv in ipairs(additional_graphics) do
    table.insert(SYSTEM_ADD_ONS_DRIVERS_GRAPHICS, drv)
end

-- ============================================================================
-- Add-ons: Drivers - MIDI
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_MIDI = {
    "usb_midi",
}

-- ============================================================================
-- Add-ons: Drivers - Network (extended with WLAN)
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_NET = TableCopy(minimum.SYSTEM_ADD_ONS_DRIVERS_NET)
local wlan_drivers = FFilterByBuildFeatures({
    "aironetwifi@x86,x86_64,riscv64",
    "atheroswifi@x86,x86_64,riscv64",
    "broadcom43xx@x86,x86_64,riscv64",
    "iaxwifi200@x86,x86_64,riscv64",
    "idualwifi7260@x86,x86_64,riscv64",
    "iprowifi2100@x86,x86_64,riscv64",
    "iprowifi2200@x86,x86_64,riscv64",
    "iprowifi3945@x86,x86_64,riscv64",
    "iprowifi4965@x86,x86_64,riscv64",
    "marvell88w8363@x86,x86_64,riscv64",
    "marvell88w8335@x86,x86_64,riscv64",
    "ralinkwifi@x86,x86_64,riscv64",
    "realtekwifi@x86,x86_64,riscv64",
})
for _, drv in ipairs(wlan_drivers) do
    table.insert(SYSTEM_ADD_ONS_DRIVERS_NET, drv)
end

-- ============================================================================
-- Add-ons: Drivers - Power (extended)
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_POWER = TableCopy(minimum.SYSTEM_ADD_ONS_DRIVERS_POWER)
local additional_power = FFilterByBuildFeatures({
    "acpi_battery@x86,x86_64",
})
for _, drv in ipairs(additional_power) do
    table.insert(SYSTEM_ADD_ONS_DRIVERS_POWER, drv)
end

-- ============================================================================
-- Add-ons: Drivers - Sensor
-- ============================================================================

SYSTEM_ADD_ONS_DRIVERS_SENSOR = TableCopy(minimum.SYSTEM_ADD_ONS_DRIVERS_SENSOR)

-- ============================================================================
-- Add-ons: Bus Managers
-- ============================================================================

SYSTEM_ADD_ONS_BUS_MANAGERS = TableCopy(minimum.SYSTEM_ADD_ONS_BUS_MANAGERS)

-- ============================================================================
-- Add-ons: File Systems (extended)
-- ============================================================================

SYSTEM_ADD_ONS_FILE_SYSTEMS = TableCopy(minimum.SYSTEM_ADD_ONS_FILE_SYSTEMS)
local additional_fs = {
    "cdda",
    -- "googlefs",
    "nfs",
    "nfs4",
    "ufs2",
}
for _, fs in ipairs(additional_fs) do
    table.insert(SYSTEM_ADD_ONS_FILE_SYSTEMS, fs)
end

-- ============================================================================
-- Image Content Setup (extends minimum)
-- ============================================================================

function SetupRegularImageContent()
    -- First setup minimum content
    minimum.SetupMinimumImageContent()

    local haiku_top = os.projectdir()

    -- Mailbox folders and symlink
    AddDirectoryToHaikuImage({"home", "mail"}, "home-mail.rdef")
    AddDirectoryToHaikuImage({"home", "mail", "draft"}, "home-mail-draft.rdef")
    AddDirectoryToHaikuImage({"home", "mail", "in"}, "home-mail-in.rdef")
    AddDirectoryToHaikuImage({"home", "mail", "out"}, "home-mail-out.rdef")
    AddDirectoryToHaikuImage({"home", "mail", "queries"}, "home-mail-queries.rdef")
    AddDirectoryToHaikuImage({"home", "mail", "sent"}, "home-mail-sent.rdef")
    AddDirectoryToHaikuImage({"home", "mail", "spam"}, "home-mail-spam.rdef")

    -- Add boot launch directory
    AddDirectoryToHaikuImage({"home", "config", "settings", "boot", "launch"})

    -- Add mail provider infos
    -- AddFilesToHaikuImage({"home", "config", "settings", "Mail", "ProviderInfo"}, HAIKU_PROVIDER_INFOS)

    -- Add Tracker New Templates
    AddDirectoryToHaikuImage({"home", "config", "settings", "Tracker", "Tracker New Templates"},
        "tracker-new-templates.rdef")

    local templates_search = path.join(haiku_top, "data", "config", "settings", "Tracker", "Tracker New Templates")
    local templates = {
        "<tracker-new-templates>C++ header",
        "<tracker-new-templates>C++ source",
        "<tracker-new-templates>Makefile",
        "<tracker-new-templates>Person",
        "<tracker-new-templates>text file",
    }
    AddFilesToHaikuImage({"home", "config", "settings", "Tracker", "Tracker New Templates"}, templates)

    -- printers
    AddDirectoryToHaikuImage({"home", "config", "settings", "printers", "Preview"},
        "home-config-settings-printers-preview.rdef")
    AddDirectoryToHaikuImage({"home", "config", "settings", "printers", "Save as PDF"},
        "home-config-settings-printers-save-as-pdf.rdef")

    -- padblocker
    AddDirectoryToHaikuImage({"home", "config", "settings", "touchpad"})

    -- shortcuts defaults
    local shortcuts_file = path.join(haiku_top, "data", "settings", "shortcuts_settings")
    AddFilesToHaikuImage({"home", "config", "settings"}, {shortcuts_file}, "shortcuts_settings")
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
    SYSTEM_BT_STACK = SYSTEM_BT_STACK,
    SYSTEM_ADD_ONS_DRIVERS_BT_H2 = SYSTEM_ADD_ONS_DRIVERS_BT_H2,
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
    SetupRegularImageContent = SetupRegularImageContent,
}