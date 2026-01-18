--[[
    Haiku.lua - Main Haiku system package

    xmake equivalent of build/jam/packages/Haiku

    This package contains:
    - Kernel and drivers
    - System libraries
    - Servers and applications
    - Boot modules
    - System data files
]]

return function(context)
    local architecture = context.architecture
    local haikuPackage = "haiku.hpkg"

    HaikuPackage(haikuPackage)

    -- ========================================================================
    -- WiFi Firmware
    -- ========================================================================
    for _, driver in ipairs(SYSTEM_ADD_ONS_DRIVERS_NET or {}) do
        local archive = HAIKU_WIFI_FIRMWARE_ARCHIVE and HAIKU_WIFI_FIRMWARE_ARCHIVE[driver]
        local extract = HAIKU_WIFI_FIRMWARE_DO_EXTRACT and HAIKU_WIFI_FIRMWARE_DO_EXTRACT[driver]
        if archive then
            AddWifiFirmwareToPackage(driver, nil, archive, extract)
        end
    end

    -- ========================================================================
    -- Boot Loaders
    -- ========================================================================
    for _, bootTarget in ipairs(HAIKU_BOOT_TARGETS or {}) do
        AddFilesToPackage("data/platform_loaders", "haiku_loader." .. bootTarget)
    end

    -- ========================================================================
    -- Kernel Modules
    -- ========================================================================

    -- Bus managers
    AddFilesToPackage("add-ons/kernel/bus_managers", SYSTEM_ADD_ONS_BUS_MANAGERS)

    -- AGP GART (x86, x86_64)
    AddFilesToPackage("add-ons/kernel/busses/agp_gart", {
        {name = "intel", grist = "agp_gart", archs = {"x86", "x86_64"}},
    })

    -- ATA busses
    AddFilesToPackage("add-ons/kernel/busses/ata", {
        "generic_ide_pci",
        "legacy_sata",
    })

    -- I2C busses
    AddFilesToPackage("add-ons/kernel/busses/i2c", {
        {name = "pch_i2c", archs = {"x86", "x86_64"}},
    })

    -- MMC busses
    AddFilesToPackage("add-ons/kernel/busses/mmc", {"sdhci"})

    -- PCI busses (architecture-specific)
    AddFilesToPackage("add-ons/kernel/busses/pci", {
        {name = "designware", grist = "pci", archs = {"riscv64"}},
        {name = "ecam", grist = "pci", archs = {"riscv64", "arm", "arm64"}},
        {name = "x86", grist = "pci", archs = {"x86", "x86_64"}},
    })

    -- Random busses
    AddFilesToPackage("add-ons/kernel/busses/random", {
        {name = "ccp_rng", archs = {"x86", "x86_64"}},
        "virtio_rng",
    })

    -- SCSI busses
    AddFilesToPackage("add-ons/kernel/busses/scsi", {"ahci", "virtio_scsi"})

    -- USB busses
    AddFilesToPackage("add-ons/kernel/busses/usb", {
        {name = "uhci", grist = "usb"},
        {name = "ohci", grist = "usb"},
        {name = "ehci", grist = "usb"},
        {name = "xhci", grist = "usb"},
    })

    -- Virtio busses
    AddFilesToPackage("add-ons/kernel/busses/virtio", {
        {name = "virtio_mmio", archs = {"riscv64", "arm", "arm64"}},
        "virtio_pci",
    })

    -- Console
    AddFilesToPackage("add-ons/kernel/console", {"vga_text"})

    -- Debugger modules
    AddFilesToPackage("add-ons/kernel/debugger", {
        {name = "demangle", grist = "kdebug"},
        {name = "disasm", grist = "kdebug", archs = {"x86", "x86_64"}},
        {name = "hangman", grist = "kdebug"},
        {name = "invalidate_on_exit", grist = "kdebug"},
        {name = "usb_keyboard", grist = "kdebug"},
        {name = "qrencode", grist = "kdebug", feature = "libqrencode"},
        {name = "run_on_exit", grist = "kdebug"},
    })

    -- File systems
    AddFilesToPackage("add-ons/kernel/file_systems", SYSTEM_ADD_ONS_FILE_SYSTEMS)

    -- Generic kernel modules
    AddFilesToPackage("add-ons/kernel/generic", {
        "ata_adapter",
        {name = "bios", archs = {"x86", "x86_64"}},
        "dpc",
        "scsi_periph",
        {name = "smbios", archs = {"x86", "x86_64"}},
        {name = "tty", grist = "module"},
    })

    -- Partitioning systems
    AddFilesToPackage("add-ons/kernel/partitioning_systems", {
        "efi_gpt",
        "intel",
        "session",
    })

    -- Power management
    AddFilesToPackage("add-ons/kernel/power/cpufreq", {
        {name = "amd_pstates", archs = {"x86", "x86_64"}},
        {name = "intel_pstates", archs = {"x86", "x86_64"}},
    })
    AddFilesToPackage("add-ons/kernel/power/cpuidle", {
        {name = "x86_cstates", archs = {"x86", "x86_64"}},
    })

    -- CPU modules (x86 only)
    if architecture == "x86" or architecture == "x86_64" then
        AddFilesToPackage("add-ons/kernel/cpu", {"generic_x86"})
    end

    -- ========================================================================
    -- Drivers (New-Style)
    -- ========================================================================
    AddNewDriversToPackage("", {
        {name = "wmi", archs = {"x86", "x86_64"}},
    })
    AddNewDriversToPackage("disk", {"nvme_disk", "usb_disk"})
    AddNewDriversToPackage("disk/mmc", {"mmc_disk"})
    AddNewDriversToPackage("disk/scsi", {"scsi_cd", "scsi_disk"})
    AddNewDriversToPackage("disk/virtual", {"ram_disk", "virtio_block"})
    AddNewDriversToPackage("graphics", {"virtio_gpu"})
    AddNewDriversToPackage("power", SYSTEM_ADD_ONS_DRIVERS_POWER)
    AddNewDriversToPackage("sensor", SYSTEM_ADD_ONS_DRIVERS_SENSOR)
    AddNewDriversToPackage("network", {"usb_ecm", "virtio_net"})
    AddNewDriversToPackage("input", {"i2c_elan", "virtio_input"})

    -- ========================================================================
    -- Drivers (Legacy)
    -- ========================================================================
    AddDriversToPackage("", {
        "console", "dprintf", "null",
        {name = "pty", grist = "driver"},
        "usb_modeswitch",
    })
    AddDriversToPackage("audio/hmulti", SYSTEM_ADD_ONS_DRIVERS_AUDIO)
    AddDriversToPackage("audio/old", SYSTEM_ADD_ONS_DRIVERS_AUDIO_OLD)
    AddDriversToPackage("bluetooth/h2", SYSTEM_ADD_ONS_DRIVERS_BT_H2)
    AddDriversToPackage("midi", SYSTEM_ADD_ONS_DRIVERS_MIDI)
    AddDriversToPackage("bus", {"usb_raw"})
    AddDriversToPackage("disk/virtual", {"nbd"})
    AddDriversToPackage("graphics", SYSTEM_ADD_ONS_DRIVERS_GRAPHICS)
    AddDriversToPackage("input", {"ps2_hid", "usb_hid"})
    AddDriversToPackage("misc", {
        {name = "poke", grist = "driver"},
        {name = "mem", grist = "driver"},
    })
    AddDriversToPackage("net", SYSTEM_ADD_ONS_DRIVERS_NET)
    AddDriversToPackage("ports", {"pc_serial", "usb_serial"})

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
    -- Trust Database
    -- ========================================================================
    AddFilesToPackage("data/trust_db", {"haiku-2019.pub"}, {
        search_path = "$(HAIKU_TOP)/data/trust_db"
    })

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

    -- WiFi firmware installer
    AddFilesToPackage("bin", {"install-wifi-firmwares.sh"}, {
        search_path = "$(HAIKU_TOP)/data/bin"
    })

    -- Symlinks
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

    AddFilesToPackage("data/user_launch", {"user"}, {
        grist = "data!launch",
        search_path = "$(HAIKU_TOP)/data/launch"
    })

    -- First login scripts
    AddFilesToPackage("boot/first-login", {"default_deskbar_items.sh"}, {
        grist = "first-login",
        search_path = "$(HAIKU_TOP)/data/system/boot/first_login"
    })

    -- ========================================================================
    -- Artwork and Sounds
    -- ========================================================================
    local logoArtwork = {
        "HAIKU logo - white on blue - big.png",
        "HAIKU logo - white on blue - normal.png",
    }
    AddFilesToPackage("data/artwork", logoArtwork, {
        search_path = "$(HAIKU_TOP)/data/artwork",
        include_trademarks = HAIKU_INCLUDE_TRADEMARKS,
    })

    -- Mail spell check dictionaries
    AddFilesToPackage("data/spell_check/word_dictionary", {"words", "geekspeak"}, {
        grist = "spell",
        search_path = "$(HAIKU_TOP)/src/apps/mail"
    })

    -- Fortune files
    AddFilesToPackage("data/fortunes", {glob = "[a-zA-Z0-9]*"}, {
        grist = "data!fortunes",
        search_path = "$(HAIKU_TOP)/data/system/data/fortunes"
    })

    -- Fonts
    AddFilesToPackage("data/fonts/psfonts", {glob = "*.afm", glob2 = "*.pfb"}, {
        search_path = "$(HAIKU_TOP)/data/system/data/fonts/psfonts"
    })

    -- ========================================================================
    -- Keymaps and Keyboard Layouts
    -- ========================================================================
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

    -- Terminal themes
    AddFilesToPackage("data/Terminal/Themes", {glob = "[a-zA-Z0-9 ]*"}, {
        search_path = "$(HAIKU_TOP)/data/system/data/terminal_themes"
    })

    -- ========================================================================
    -- Boot Module Symlinks
    -- ========================================================================
    AddBootModuleSymlinksToPackage({
        {name = "acpi", archs = {"x86", "x86_64", "arm64"}},
        "ahci",
        "ata",
        "ata_adapter",
        "bfs",
        "dpc",
        "efi_gpt",
        "generic_ide_pci",
        {name = "isa", archs = {"x86", "x86_64"}},
        "intel",
        "legacy_sata",
        "mmc",
        "mmc_disk",
        "nvme_disk",
        "packagefs",
        "pci",
        {name = "designware", grist = "pci", archs = {"riscv64"}},
        {name = "ecam", grist = "pci", archs = {"riscv64", "arm", "arm64"}},
        {name = "x86", grist = "pci", archs = {"x86", "x86_64"}},
        {name = "fdt", archs = {"riscv64", "arm", "arm64"}},
        "scsi",
        "scsi_cd",
        "scsi_disk",
        "scsi_periph",
        "sdhci",
        "usb",
        "usb_disk",
        {name = "ehci", grist = "usb"},
        {name = "ohci", grist = "usb"},
        {name = "uhci", grist = "usb"},
        {name = "xhci", grist = "usb"},
        "virtio",
        "virtio_block",
        {name = "virtio_mmio", archs = {"riscv64", "arm", "arm64"}},
        "virtio_pci",
        "virtio_scsi",
    })

    -- ========================================================================
    -- Add-ons
    -- ========================================================================
    AddFilesToPackage("add-ons/accelerants", SYSTEM_ADD_ONS_ACCELERANTS)
    AddFilesToPackage("add-ons/locale/catalogs", SYSTEM_ADD_ONS_LOCALE_CATALOGS)

    -- Mail daemon
    AddFilesToPackage("add-ons/mail_daemon/inbound_protocols", {"POP3", "IMAP"})
    AddFilesToPackage("add-ons/mail_daemon/outbound_protocols", {"SMTP"})
    AddFilesToPackage("add-ons/mail_daemon/inbound_filters", {
        "MatchHeader", "SpamFilter", "NewMailNotification"
    })
    AddFilesToPackage("add-ons/mail_daemon/outbound_filters", {"Fortune"})

    -- Media add-ons
    AddFilesToPackage("add-ons/media", SYSTEM_ADD_ONS_MEDIA)

    -- Network settings
    AddFilesToPackage("add-ons/Network Settings", {
        "IPv4Interface", "IPv6Interface", "DNSClientService",
        "Hostname", "FTPService", "SSHService", "TelnetService",
    })

    -- Tracker add-ons
    AddFilesToPackage("add-ons/Tracker", {
        "FileType", "Mark as…", "Mark as Read",
        "Open Target Folder", "Open Terminal", "ZipOMatic",
    })
    AddSymlinkToPackage("add-ons/Tracker",
        "/boot/system/preferences/Backgrounds", "Background")
    AddSymlinkToPackage("add-ons/Tracker",
        "/boot/system/apps/TextSearch", "TextSearch")
    AddSymlinkToPackage("add-ons/Tracker",
        "/boot/system/apps/DiskUsage", "DiskUsage")

    -- Input server
    AddFilesToPackage("add-ons/input_server/devices", {
        {name = "keyboard", grist = "input"},
        {name = "mouse", grist = "input"},
        {name = "tablet", grist = "input"},
        {name = "virtio", grist = "input"},
    })
    AddFilesToPackage("add-ons/input_server/filters", {
        "padblocker", "screen_saver", "shortcut_catcher", "switch_workspace",
    })
    AddDirectoryToPackage("add-ons/input_server/methods")

    -- Network stack
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

    -- Screen savers
    AddFilesToPackage("add-ons/Screen Savers", SYSTEM_ADD_ONS_SCREENSAVERS)

    -- Disk systems
    AddFilesToPackage("add-ons/disk_systems", {
        {name = "fat", grist = "disk_system"},
        {name = "intel", grist = "disk_system"},
        {name = "gpt", grist = "disk_system"},
        {name = "bfs", grist = "disk_system"},
        {name = "ntfs", grist = "disk_system"},
    })

    -- Bluetooth stack
    AddFilesToPackage("add-ons/kernel/bluetooth", SYSTEM_BT_STACK)

    -- ========================================================================
    -- MIME Database
    -- ========================================================================
    CopyDirectoryToPackage("data", "mime_db", {
        grist = "mimedb",
        is_target = true,
    })

    -- ========================================================================
    -- Directory Attributes
    -- ========================================================================
    AddDirectoryToPackage("apps", {rdef = "system-apps.rdef"})
    AddDirectoryToPackage("preferences", {rdef = "system-preferences.rdef"})

    -- ========================================================================
    -- Deskbar Menu Symlinks
    -- ========================================================================

    -- Applications
    AddDirectoryToPackage("data/deskbar/menu/Applications",
        {rdef = "deskbar-applications.rdef"})
    for _, linkTarget in ipairs(DESKBAR_APPLICATIONS or {}) do
        AddSymlinkToPackage("data/deskbar/menu/Applications",
            "../../../../apps/" .. linkTarget, linkTarget)
    end

    -- Desktop applets
    AddDirectoryToPackage("data/deskbar/menu/Desktop applets",
        {rdef = "deskbar-applets.rdef"})
    for _, linkTarget in ipairs(DESKBAR_DESKTOP_APPLETS or {}) do
        AddSymlinkToPackage("data/deskbar/menu/Desktop applets",
            "../../../../apps/" .. linkTarget, linkTarget)
    end

    -- Preferences
    AddDirectoryToPackage("data/deskbar/menu/Preferences",
        {rdef = "deskbar-preferences.rdef"})
    for _, linkTarget in ipairs(SYSTEM_PREFERENCES or {}) do
        local name = path.basename(linkTarget)
        AddSymlinkToPackage("data/deskbar/menu/Preferences",
            "../../../../preferences/" .. name, name)
    end

    -- Demos
    AddDirectoryToPackage("data/deskbar/menu/Demos",
        {rdef = "deskbar-demos.rdef"})
    for _, linkTarget in ipairs(SYSTEM_DEMOS or {}) do
        AddSymlinkToPackage("data/deskbar/menu/Demos",
            "../../../../demos/" .. linkTarget, linkTarget)
    end

    -- Menu entries
    AddFilesToPackage("data/deskbar", {"menu_entries"}, {grist = "deskbar"})

    -- ========================================================================
    -- Data Directories
    -- ========================================================================

    -- Licenses
    CopyDirectoryToPackage("data", "licenses", {
        source = "$(HAIKU_TOP)/data/system/data/licenses"
    })

    -- Network support files
    CopyDirectoryToPackage("data", "network", {
        source = "$(HAIKU_TOP)/data/system/data/network"
    })

    -- GPU firmware
    CopyDirectoryToPackage("data", "firmware", {
        source = "$(HAIKU_TOP)/data/system/data/firmware"
    })

    -- Documentation
    CopyDirectoryToPackage("documentation", "diskusage", {
        source = "$(HAIKU_TOP)/docs/apps/diskusage"
    })

    -- Empty directory
    AddDirectoryToPackage("data/empty")

    -- ========================================================================
    -- Build Package
    -- ========================================================================
    BuildHaikuPackage(haikuPackage, "haiku")
end