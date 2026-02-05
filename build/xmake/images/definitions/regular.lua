-- Regular Haiku image definition
-- Ported from build/jam/images/definitions/regular
--
-- This file defines the content of the regular Haiku image.
-- It imports everything from minimum and adds additional content.
--
-- NOTE: xmake only exports FUNCTIONS from modules, not variables.
-- All data is stored in local tables and accessed via getter functions.

local ImageRules = import("rules.ImageRules")
local BuildFeatureRules = import("rules.BuildFeatureRules")
local HelperRules = import("rules.HelperRules")
local SystemLibraryRules = import("rules.SystemLibraryRules")

-- Import minimum image definitions
local minimum = import("images.definitions.minimum")

-- ============================================================================
-- Inherit from Minimum (call getter functions!)
-- ============================================================================

-- Start with minimum system binaries and extend
local _SYSTEM_BIN = HelperRules.TableCopy(minimum.SYSTEM_BIN())

-- Add userguide, quicktour, welcome scripts
local config = import("core.project.config", {try = true})
local haiku_top = config and config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
local additional_bin_scripts = {"userguide", "quicktour", "welcome"}
for _, script in ipairs(additional_bin_scripts) do
    table.insert(_SYSTEM_BIN, script)
end

-- Add more system binaries
local additional_system_bin = BuildFeatureRules.FFilterByBuildFeatures({
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
    table.insert(_SYSTEM_BIN, bin)
end

-- ============================================================================
-- System Applications (extended)
-- ============================================================================

local _SYSTEM_APPS = HelperRules.TableCopy(minimum.SYSTEM_APPS())

local additional_system_apps = BuildFeatureRules.FFilterByBuildFeatures({
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
    table.insert(_SYSTEM_APPS, app)
end

-- ============================================================================
-- Deskbar Configuration (extended)
-- ============================================================================

local _DESKBAR_APPLICATIONS = HelperRules.TableCopy(minimum.DESKBAR_APPLICATIONS())
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
    table.insert(_DESKBAR_APPLICATIONS, app)
end

local _DESKBAR_DESKTOP_APPLETS = HelperRules.TableCopy(minimum.DESKBAR_DESKTOP_APPLETS())
local additional_deskbar_applets = {
    "AutoRaise",
    "LaunchBox",
    "PowerStatus",
}
for _, applet in ipairs(additional_deskbar_applets) do
    table.insert(_DESKBAR_DESKTOP_APPLETS, applet)
end

-- ============================================================================
-- System Preferences (extended)
-- ============================================================================

local _SYSTEM_PREFERENCES = HelperRules.TableCopy(minimum.SYSTEM_PREFERENCES())
local additional_preferences = BuildFeatureRules.FFilterByBuildFeatures({
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
    table.insert(_SYSTEM_PREFERENCES, pref)
end

-- ============================================================================
-- System Demos
-- ============================================================================

local _SYSTEM_DEMOS = BuildFeatureRules.FFilterByBuildFeatures({
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
-- System Servers (extended)
-- ============================================================================

local _SYSTEM_SERVERS = HelperRules.TableCopy(minimum.SYSTEM_SERVERS())
local additional_servers = BuildFeatureRules.FFilterByBuildFeatures({
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
    table.insert(_SYSTEM_SERVERS, server)
end

-- ============================================================================
-- Network Configuration (extended)
-- ============================================================================

local _SYSTEM_NETWORK_DEVICES = HelperRules.TableCopy(minimum.SYSTEM_NETWORK_DEVICES())
local _SYSTEM_NETWORK_DATALINK_PROTOCOLS = HelperRules.TableCopy(minimum.SYSTEM_NETWORK_DATALINK_PROTOCOLS())
local _SYSTEM_NETWORK_PROTOCOLS = HelperRules.TableCopy(minimum.SYSTEM_NETWORK_PROTOCOLS())

-- Bluetooth stack + drivers
-- SYSTEM_NETWORK_PROTOCOLS += "l2cap"

local _SYSTEM_BT_STACK = {
    "hci",
    "btCoreData",
}

local _SYSTEM_ADD_ONS_DRIVERS_BT_H2 = {
    "h2generic",
}

-- ============================================================================
-- Add-ons: Accelerants (extended)
-- ============================================================================

local _SYSTEM_ADD_ONS_ACCELERANTS = HelperRules.TableCopy(minimum.SYSTEM_ADD_ONS_ACCELERANTS())
local additional_accelerants = BuildFeatureRules.FFilterByBuildFeatures({
    "ati.accelerant@x86,x86_64",
    "intel_extreme.accelerant@x86,x86_64",
    "radeon_hd.accelerant@x86,x86_64",
    "virtio_gpu.accelerant",
})
for _, acc in ipairs(additional_accelerants) do
    table.insert(_SYSTEM_ADD_ONS_ACCELERANTS, acc)
end

-- ============================================================================
-- Add-ons: Translators (extended)
-- ============================================================================

local _SYSTEM_ADD_ONS_TRANSLATORS = HelperRules.TableCopy(minimum.SYSTEM_ADD_ONS_TRANSLATORS())
local additional_translators = BuildFeatureRules.FFilterByBuildFeatures({
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
    table.insert(_SYSTEM_ADD_ONS_TRANSLATORS, trans)
end

-- ============================================================================
-- Add-ons: Locale Catalogs
-- ============================================================================

local _SYSTEM_ADD_ONS_LOCALE_CATALOGS = HelperRules.TableCopy(minimum.SYSTEM_ADD_ONS_LOCALE_CATALOGS())

-- ============================================================================
-- Add-ons: Media
-- ============================================================================

local _SYSTEM_ADD_ONS_MEDIA = BuildFeatureRules.FFilterByBuildFeatures({
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

local _SYSTEM_ADD_ONS_MEDIA_PLUGINS = BuildFeatureRules.FFilterByBuildFeatures({
    "ffmpeg@ffmpeg",
    "http_streamer",
    "raw_decoder",
})

-- ============================================================================
-- Add-ons: Print
-- ============================================================================

local _SYSTEM_ADD_ONS_PRINT = BuildFeatureRules.FFilterByBuildFeatures({
    "Canon LIPS3 Compatible",
    "Canon LIPS4 Compatible",
    "Gutenprint@gutenprint",
    "PCL5 Compatible",
    "PCL6 Compatible",
    "PS Compatible",
    "Preview",
})

local _SYSTEM_ADD_ONS_PRINT_TRANSPORT = {
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

local _SYSTEM_ADD_ONS_SCREENSAVERS = BuildFeatureRules.FFilterByBuildFeatures({
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

local _SYSTEM_ADD_ONS_DRIVERS_AUDIO = {
    "hda",
    "usb_audio",
}

local _SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD = {
    -- "cmedia",
}

-- ============================================================================
-- Add-ons: Drivers - Graphics (extended)
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_GRAPHICS = HelperRules.TableCopy(minimum.SYSTEM_ADD_ONS_DRIVERS_GRAPHICS())
local additional_graphics = BuildFeatureRules.FFilterByBuildFeatures({
    "ati@x86,x86_64",
    "intel_extreme@x86,x86_64",
    "radeon_hd@x86,x86_64",
})
for _, drv in ipairs(additional_graphics) do
    table.insert(_SYSTEM_ADD_ONS_DRIVERS_GRAPHICS, drv)
end

-- ============================================================================
-- Add-ons: Drivers - MIDI
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_MIDI = {
    "usb_midi",
}

-- ============================================================================
-- Add-ons: Drivers - Network (extended with WLAN)
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_NET = HelperRules.TableCopy(minimum.SYSTEM_ADD_ONS_DRIVERS_NET())
local wlan_drivers = BuildFeatureRules.FFilterByBuildFeatures({
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
    table.insert(_SYSTEM_ADD_ONS_DRIVERS_NET, drv)
end

-- ============================================================================
-- Add-ons: Drivers - Power (extended)
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_POWER = HelperRules.TableCopy(minimum.SYSTEM_ADD_ONS_DRIVERS_POWER())
local additional_power = BuildFeatureRules.FFilterByBuildFeatures({
    "acpi_battery@x86,x86_64",
})
for _, drv in ipairs(additional_power) do
    table.insert(_SYSTEM_ADD_ONS_DRIVERS_POWER, drv)
end

-- ============================================================================
-- Add-ons: Drivers - Sensor
-- ============================================================================

local _SYSTEM_ADD_ONS_DRIVERS_SENSOR = HelperRules.TableCopy(minimum.SYSTEM_ADD_ONS_DRIVERS_SENSOR())

-- ============================================================================
-- Add-ons: Bus Managers
-- ============================================================================

local _SYSTEM_ADD_ONS_BUS_MANAGERS = HelperRules.TableCopy(minimum.SYSTEM_ADD_ONS_BUS_MANAGERS())

-- ============================================================================
-- Add-ons: File Systems (extended)
-- ============================================================================

local _SYSTEM_ADD_ONS_FILE_SYSTEMS = HelperRules.TableCopy(minimum.SYSTEM_ADD_ONS_FILE_SYSTEMS())
local additional_fs = {
    "cdda",
    -- "googlefs",
    "nfs",
    "nfs4",
    "ufs2",
}
for _, fs in ipairs(additional_fs) do
    table.insert(_SYSTEM_ADD_ONS_FILE_SYSTEMS, fs)
end

-- ============================================================================
-- System Libraries (override)
-- ============================================================================

function HaikuImageGetSystemLibs()
    return HelperRules.TableMerge(
        -- libs with special grist
        HelperRules.MultiArchDefaultGristFiles({"libroot.so"}, "revisioned"),
        SystemLibraryRules.Libstdc_ForImage(),
        -- libs with standard grist
        HelperRules.MultiArchDefaultGristFiles(BuildFeatureRules.FFilterByBuildFeatures({
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
    return HelperRules.MultiArchDefaultGristFiles(BuildFeatureRules.FFilterByBuildFeatures({
        "libalm.so",
        "libpackage-add-on-libsolv.so",
        "libroot-addon-icu.so",
    }))
end

-- ============================================================================
-- Image Content Setup (extends minimum)
-- ============================================================================

function SetupRegularImageContent()
    -- First setup minimum content
    minimum.SetupMinimumImageContent()

    local config = import("core.project.config")
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))

    -- Mailbox folders and symlink
    ImageRules.AddDirectoryToHaikuImage({"home", "mail"}, "home-mail.rdef")
    ImageRules.AddDirectoryToHaikuImage({"home", "mail", "draft"}, "home-mail-draft.rdef")
    ImageRules.AddDirectoryToHaikuImage({"home", "mail", "in"}, "home-mail-in.rdef")
    ImageRules.AddDirectoryToHaikuImage({"home", "mail", "out"}, "home-mail-out.rdef")
    ImageRules.AddDirectoryToHaikuImage({"home", "mail", "queries"}, "home-mail-queries.rdef")
    ImageRules.AddDirectoryToHaikuImage({"home", "mail", "sent"}, "home-mail-sent.rdef")
    ImageRules.AddDirectoryToHaikuImage({"home", "mail", "spam"}, "home-mail-spam.rdef")

    -- Add boot launch directory
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "settings", "boot", "launch"})

    -- Add mail provider infos
    -- ImageRules.AddFilesToHaikuImage({"home", "config", "settings", "Mail", "ProviderInfo"}, HAIKU_PROVIDER_INFOS)

    -- Add Tracker New Templates
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "settings", "Tracker", "Tracker New Templates"},
        "tracker-new-templates.rdef")

    local templates_search = path.join(haiku_top, "data", "config", "settings", "Tracker", "Tracker New Templates")
    local templates = {
        "<tracker-new-templates>C++ header",
        "<tracker-new-templates>C++ source",
        "<tracker-new-templates>Makefile",
        "<tracker-new-templates>Person",
        "<tracker-new-templates>text file",
    }
    ImageRules.AddFilesToHaikuImage({"home", "config", "settings", "Tracker", "Tracker New Templates"}, templates)

    -- printers
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "settings", "printers", "Preview"},
        "home-config-settings-printers-preview.rdef")
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "settings", "printers", "Save as PDF"},
        "home-config-settings-printers-save-as-pdf.rdef")

    -- padblocker
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "settings", "touchpad"})

    -- shortcuts defaults
    local shortcuts_file = path.join(haiku_top, "data", "settings", "shortcuts_settings")
    ImageRules.AddFilesToHaikuImage({"home", "config", "settings"}, {shortcuts_file}, "shortcuts_settings")
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
function SYSTEM_BT_STACK() return _SYSTEM_BT_STACK end
function SYSTEM_ADD_ONS_DRIVERS_BT_H2() return _SYSTEM_ADD_ONS_DRIVERS_BT_H2 end
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
