--[[
    HaikuBootstrap.lua - Haiku bootstrap system package

    xmake equivalent of build/jam/packages/HaikuBootstrap

    This is a minimal system package for bootstrap builds.
    It contains a stripped-down set of drivers and modules.
]]

return function(context)
    local architecture = context.architecture

    local haikuPackage = "haiku.hpkg"
    HaikuPackage(haikuPackage)

    -- ========================================================================
    -- Kernel Modules
    -- ========================================================================
    AddFilesToPackage("add-ons/kernel/bus_managers", SYSTEM_ADD_ONS_BUS_MANAGERS)

    AddFilesToPackage("add-ons/kernel/busses/agp_gart", {
        {name = "intel", grist = "agp_gart", archs = {"x86", "x86_64"}},
    })

    AddFilesToPackage("add-ons/kernel/busses/ata", {
        "generic_ide_pci",
        "legacy_sata",
    })

    AddFilesToPackage("add-ons/kernel/busses/pci", {
        {name = "designware", grist = "pci", archs = {"riscv64"}},
        {name = "ecam", grist = "pci", archs = {"riscv64", "arm", "arm64"}},
        {name = "x86", grist = "pci", archs = {"x86", "x86_64"}},
    })

    AddFilesToPackage("add-ons/kernel/busses/scsi", {"ahci", "virtio_scsi"})

    AddFilesToPackage("add-ons/kernel/busses/usb", {
        {name = "uhci", grist = "usb"},
        {name = "ohci", grist = "usb"},
        {name = "ehci", grist = "usb"},
    })

    AddFilesToPackage("add-ons/kernel/busses/virtio", {"virtio_pci"})

    AddFilesToPackage("add-ons/kernel/console", {"vga_text"})

    AddFilesToPackage("add-ons/kernel/debugger", {
        {name = "demangle", grist = "kdebug"},
        {name = "disasm", grist = "kdebug", archs = {"x86"}},
        {name = "invalidate_on_exit", grist = "kdebug"},
        {name = "usb_keyboard", grist = "kdebug"},
        {name = "run_on_exit", grist = "kdebug"},
    })

    AddFilesToPackage("add-ons/kernel/file_systems", SYSTEM_ADD_ONS_FILE_SYSTEMS)

    AddFilesToPackage("add-ons/kernel/generic", {
        "ata_adapter",
        {name = "bios", archs = {"x86", "x86_64"}},
        "dpc",
        "scsi_periph",
        {name = "tty", grist = "module"},
    })

    AddFilesToPackage("add-ons/kernel/partitioning_systems", {
        {name = "amiga_rdb", archs = {"m68k"}},
        {name = "apple", archs = {"ppc"}},
        "efi_gpt",
        "intel",
        "session",
    })

    AddFilesToPackage("add-ons/kernel/interrupt_controllers", {
        {name = "openpic", archs = {"ppc"}},
    })

    AddFilesToPackage("add-ons/kernel/power/cpufreq", {
        {name = "intel_pstates", archs = {"x86", "x86_64"}},
    })
    AddFilesToPackage("add-ons/kernel/power/cpuidle", {
        {name = "x86_cstates", archs = {"x86", "x86_64"}},
    })

    if architecture == "x86" or architecture == "x86_64" then
        AddFilesToPackage("add-ons/kernel/cpu", {"generic_x86"})
    end

    -- ========================================================================
    -- Drivers
    -- ========================================================================
    AddNewDriversToPackage("disk/scsi", {"scsi_cd", "scsi_disk"})
    AddNewDriversToPackage("disk/virtual", {"ram_disk", "remote_disk", "virtio_block"})
    AddNewDriversToPackage("power", {
        {name = "acpi_battery", archs = {"x86"}},
    })
    AddNewDriversToPackage("network", {"virtio_net"})

    AddDriversToPackage("", {
        "console", "dprintf", "null", "random",
        {name = "pty", grist = "driver"},
    })
    AddDriversToPackage("audio/hmulti", SYSTEM_ADD_ONS_DRIVERS_AUDIO)
    AddDriversToPackage("audio/old", SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD)
    AddDriversToPackage("midi", SYSTEM_ADD_ONS_DRIVERS_MIDI)
    AddDriversToPackage("bus", {"usb_raw"})
    AddDriversToPackage("disk/virtual", {"nbd"})
    AddDriversToPackage("graphics", SYSTEM_ADD_ONS_DRIVERS_GRAPHICS)
    AddDriversToPackage("input", {"ps2_hid", "usb_hid", "wacom"})
    AddDriversToPackage("misc", {
        {name = "poke", grist = "driver"},
        {name = "mem", grist = "driver"},
    })
    AddDriversToPackage("net", SYSTEM_ADD_ONS_DRIVERS_NET)
    AddDriversToPackage("ports", {"pc_serial", "usb_serial"})
    AddDriversToPackage("power", SYSTEM_ADD_ONS_DRIVERS_POWER)

    -- ========================================================================
    -- Kernel
    -- ========================================================================
    AddFilesToPackage("", {"kernel_" .. architecture}, {revisioned = true})

    -- ========================================================================
    -- Libraries
    -- ========================================================================
    AddLibrariesToPackage("lib",
        HaikuImageGetSystemLibs(),
        HaikuImageGetPrivateSystemLibs()
    )

    -- ========================================================================
    -- Servers
    -- ========================================================================
    AddFilesToPackage("servers", SYSTEM_SERVERS)

    -- ========================================================================
    -- Applications
    -- ========================================================================
    AddFilesToPackage("", {"runtime_loader", "Deskbar", "Tracker"})
    AddFilesToPackage("bin", SYSTEM_BIN)
    AddFilesToPackage("bin", {"consoled"})
    AddFilesToPackage("apps", SYSTEM_APPS)
    AddFilesToPackage("preferences", SYSTEM_PREFERENCES)
    AddFilesToPackage("demos", SYSTEM_DEMOS)

    AddFilesToPackage("bin", {"install-wifi-firmwares.sh"}, {
        search_path = "$(HAIKU_TOP)/data/bin"
    })

    AddSymlinkToPackage("bin", "trash", "untrash")
    AddSymlinkToPackage("bin", "less", "more")

    -- ========================================================================
    -- Boot Scripts
    -- ========================================================================
    AddFilesToPackage("boot", {"PostInstallScript", "SetupEnvironment"}, {
        search_path = "$(HAIKU_TOP)/data/system/boot"
    })

    AddFilesToPackage("data/launch", {"system"}, {
        grist = "data!launch",
        search_path = "$(HAIKU_TOP)/data/launch"
    })

    AddFilesToPackage("data/user_launch", {"user", "bootstrap"}, {
        grist = "data!launch",
        search_path = "$(HAIKU_TOP)/data/launch"
    })

    AddFilesToPackage("boot/first_login", {"default_deskbar_items.sh"}, {
        grist = "first-login",
        search_path = "$(HAIKU_TOP)/data/system/boot/first_login"
    })

    -- ========================================================================
    -- Data Files
    -- ========================================================================
    AddFilesToPackage("data/fortunes", {glob = "[a-zA-Z0-9]*"}, {
        grist = "data!fortunes",
        search_path = "$(HAIKU_TOP)/data/system/data/fortunes"
    })

    AddFilesToPackage("data/fonts/psfonts", {glob = "*.afm", glob2 = "*.pfb"}, {
        search_path = "$(HAIKU_TOP)/data/system/data/fonts/psfonts"
    })

    AddFilesToPackage("data/Keymaps", HAIKU_KEYMAP_FILES)
    for _, keymapAlias in ipairs(HAIKU_KEYMAP_ALIASES or {}) do
        local aliasedTo = HAIKU_KEYMAP_FILE[keymapAlias]
        if aliasedTo then
            AddSymlinkToPackage("data/Keymaps", aliasedTo, keymapAlias)
        end
    end

    AddFilesToPackage("data/KeyboardLayouts", HAIKU_KEYBOARD_LAYOUT_FILES)
    AddFilesToPackage("data/KeyboardLayouts/Apple Aluminum",
        HAIKU_APPLE_ALUMINUM_KEYBOARD_LAYOUT_FILES)
    AddFilesToPackage("data/KeyboardLayouts/ThinkPad",
        HAIKU_THINKPAD_KEYBOARD_LAYOUT_FILES)

    AddFilesToPackage("data/Terminal/Themes", {glob = "[a-zA-Z0-9 ]*"}, {
        search_path = "$(HAIKU_TOP)/data/system/data/terminal_themes"
    })

    -- ========================================================================
    -- Boot Module Symlinks
    -- ========================================================================
    AddBootModuleSymlinksToPackage({
        {name = "acpi", archs = {"x86", "x86_64"}},
        "ata",
        "pci",
        {name = "designware", grist = "pci", archs = {"riscv64"}},
        {name = "ecam", grist = "pci", archs = {"riscv64", "arm", "arm64"}},
        {name = "x86", grist = "pci", archs = {"x86", "x86_64"}},
        {name = "isa", archs = {"x86", "x86_64"}},
        "dpc",
        "scsi",
        "usb",
        {name = "openpic", archs = {"ppc"}},
        "ata_adapter",
        "scsi_periph",
        "ahci",
        "generic_ide_pci",
        "legacy_sata",
        {name = "uhci", grist = "usb"},
        {name = "ohci", grist = "usb"},
        {name = "ehci", grist = "usb"},
        "remote_disk",
        "scsi_cd",
        "scsi_disk",
        "virtio",
        "virtio_pci",
        "virtio_block",
        "virtio_scsi",
        "efi_gpt",
        "intel",
        "bfs",
        "packagefs",
        -- Network drivers
        SYSTEM_ADD_ONS_DRIVERS_NET,
        "stack",
        SYSTEM_NETWORK_DEVICES,
        SYSTEM_NETWORK_DATALINK_PROTOCOLS,
        SYSTEM_NETWORK_PROTOCOLS,
    })

    -- ========================================================================
    -- Add-ons
    -- ========================================================================
    AddFilesToPackage("add-ons/accelerants", SYSTEM_ADD_ONS_ACCELERANTS)
    AddFilesToPackage("add-ons/Translators", SYSTEM_ADD_ONS_TRANSLATORS)
    AddFilesToPackage("add-ons/locale/catalogs", SYSTEM_ADD_ONS_LOCALE_CATALOGS)

    AddFilesToPackage("add-ons/Tracker", {
        "FileType", "Mark as…", "Mark as Read",
        "Open Target Folder", "Open Terminal", "ZipOMatic",
    })
    AddSymlinkToPackage("add-ons/Tracker",
        "/boot/system/preferences/Backgrounds", "Background-B")
    AddSymlinkToPackage("add-ons/Tracker",
        "/boot/system/apps/TextSearch", "TextSearch-G")
    AddSymlinkToPackage("add-ons/Tracker",
        "/boot/system/apps/DiskUsage", "DiskUsage-I")

    AddFilesToPackage("add-ons/input_server/devices", {
        {name = "keyboard", grist = "input"},
        {name = "mouse", grist = "input"},
        {name = "tablet", grist = "input"},
        {name = "wacom", grist = "input"},
    })
    AddFilesToPackage("add-ons/input_server/filters", {"switch_workspace"})

    AddFilesToPackage("add-ons/kernel/network", {
        {name = "notifications", grist = "net"},
        "stack",
        "dns_resolver",
    })
    AddFilesToPackage("add-ons/kernel/network/devices", SYSTEM_NETWORK_DEVICES)
    AddFilesToPackage("add-ons/kernel/network/datalink_protocols",
        SYSTEM_NETWORK_DATALINK_PROTOCOLS)
    AddFilesToPackage("add-ons/kernel/network/ppp", SYSTEM_NETWORK_PPP)
    AddFilesToPackage("add-ons/kernel/network/protocols", SYSTEM_NETWORK_PROTOCOLS)

    AddFilesToPackage("add-ons/disk_systems", {
        {name = "intel", grist = "disk_system"},
        {name = "gpt", grist = "disk_system"},
        {name = "bfs", grist = "disk_system"},
        {name = "ntfs", grist = "disk_system"},
    })

    -- ========================================================================
    -- MIME Database
    -- ========================================================================
    CopyDirectoryToPackage("data", "mime_db", {
        grist = "mimedb",
        is_target = true,
    })

    -- ========================================================================
    -- Deskbar Menu
    -- ========================================================================
    local DESKBAR_APPLICATIONS = {
        "ActivityMonitor", "CharacterMap", "DeskCalc", "Devices",
        "DiskProbe", "DriveSetup", "DiskUsage", "Expander",
        "Installer", "StyledEdit", "Terminal",
    }
    for _, linkTarget in ipairs(DESKBAR_APPLICATIONS) do
        AddSymlinkToPackage("data/deskbar/menu/Applications",
            "../../../../apps/" .. linkTarget, linkTarget)
    end

    local DESKBAR_DESKTOP_APPLETS = {
        "LaunchBox", "NetworkStatus", "PowerStatus",
        "ProcessController", "Workspaces",
    }
    for _, linkTarget in ipairs(DESKBAR_DESKTOP_APPLETS) do
        AddSymlinkToPackage("data/deskbar/menu/Desktop applets",
            "../../../../apps/" .. linkTarget, linkTarget)
    end

    AddDirectoryToPackage("data/deskbar/menu/Preferences")
    for _, linkTarget in ipairs(SYSTEM_PREFERENCES or {}) do
        local name = path.basename(linkTarget)
        AddSymlinkToPackage("data/deskbar/menu/Preferences",
            "../../../../preferences/" .. name, name)
    end

    AddFilesToPackage("data/deskbar", {"menu_entries"}, {grist = "deskbar"})

    -- ========================================================================
    -- Data Directories
    -- ========================================================================
    CopyDirectoryToPackage("data", "licenses", {
        source = "$(HAIKU_TOP)/data/system/data/licenses"
    })
    CopyDirectoryToPackage("data", "network", {
        source = "$(HAIKU_TOP)/data/system/data/network"
    })
    CopyDirectoryToPackage("documentation", "diskusage", {
        source = "$(HAIKU_TOP)/docs/apps/diskusage"
    })

    -- ========================================================================
    -- Build Package
    -- ========================================================================
    BuildHaikuPackage(haikuPackage, "haiku")
end