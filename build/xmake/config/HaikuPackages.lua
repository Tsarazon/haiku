--[[
    HaikuPackages.lua - Haiku system package definitions

    xmake equivalent of build/jam/HaikuPackages

    This module defines:
    - Core packages built for the system
    - Architecture-dependent packages
    - Secondary architecture packages
    - Package definition loading
]]

-- ============================================================================
-- Package Lists
-- ============================================================================

--[[
    HAIKU_CORE_PACKAGES

    Core packages filtered by build features.
    These are built once for the primary architecture.
]]
HAIKU_CORE_PACKAGES = {
    "HaikuDevel",
    "HaikuDataTranslators",
    "HaikuExtras",
    "HaikuLoader",
    "HaikuSource",
    "MakefileEngine",
    "NetFS",
    "UserlandFS",
}

--[[
    HAIKU_ARCH_DEPENDENT_PACKAGES

    Packages built for each architecture.
]]
HAIKU_ARCH_DEPENDENT_PACKAGES = {
    "HaikuCrossDevel",
    "WebPositive",
}

--[[
    HAIKU_SECONDARY_ARCH_PACKAGES

    Packages built only for secondary architectures.
]]
HAIKU_SECONDARY_ARCH_PACKAGES = {
    "HaikuDevelSecondary",
}

-- ============================================================================
-- Build Type Package Selection
-- ============================================================================

--[[
    GetMainPackageName()

    Returns the main package name based on build type.
    - "HaikuBootstrap" for bootstrap builds
    - "Haiku" for regular builds
]]
function GetMainPackageName()
    if HAIKU_BUILD_TYPE == "bootstrap" then
        return "HaikuBootstrap"
    else
        return "Haiku"
    end
end

--[[
    GetSecondaryPackageName()

    Returns the secondary arch package name based on build type.
    - "HaikuSecondaryBootstrap" for bootstrap builds
    - "HaikuSecondary" for regular builds
]]
function GetSecondaryPackageName()
    if HAIKU_BUILD_TYPE == "bootstrap" then
        return "HaikuSecondaryBootstrap"
    else
        return "HaikuSecondary"
    end
end

-- ============================================================================
-- Package Definition Loading
-- ============================================================================

--[[
    LoadPackageDefinition(package_name, architecture)

    Loads a package definition file.

    Parameters:
        package_name: Name of the package (e.g., "Haiku", "HaikuDevel")
        architecture: Target architecture for the package

    Returns:
        Package definition table or nil if not found
]]
function LoadPackageDefinition(package_name, architecture)
    local package_file = path.join(os.scriptdir(), "..", "packages", package_name .. ".lua")
    if os.isfile(package_file) then
        local chunk, err = loadfile(package_file)
        if chunk then
            -- Set up environment for package definition
            local env = {
                architecture = architecture,
                is_primary = (architecture == get_config("arch")),
                TARGET_ARCH = get_config("arch"),
                HAIKU_BUILD_TYPE = HAIKU_BUILD_TYPE,
                HAIKU_KERNEL_PLATFORM = HAIKU_KERNEL_PLATFORM,
                -- Inherit global functions
                HaikuPackage = HaikuPackage,
                BuildHaikuPackage = BuildHaikuPackage,
                AddFilesToPackage = AddFilesToPackage,
                AddLibrariesToPackage = AddLibrariesToPackage,
                AddSymlinkToPackage = AddSymlinkToPackage,
                AddDirectoryToPackage = AddDirectoryToPackage,
                AddHeaderDirectoryToPackage = AddHeaderDirectoryToPackage,
                CopyDirectoryToPackage = CopyDirectoryToPackage,
                AddDriversToPackage = AddDriversToPackage,
                AddNewDriversToPackage = AddNewDriversToPackage,
                AddBootModuleSymlinksToPackage = AddBootModuleSymlinksToPackage,
                AddWifiFirmwareToPackage = AddWifiFirmwareToPackage,
                FFilterByBuildFeatures = FFilterByBuildFeatures,
                FIsBuildFeatureEnabled = FIsBuildFeatureEnabled,
                HaikuImageGetSystemLibs = HaikuImageGetSystemLibs,
                HaikuImageGetPrivateSystemLibs = HaikuImageGetPrivateSystemLibs,
                MultiArchDefaultGristFiles = MultiArchDefaultGristFiles,
                MultiArchIfPrimary = MultiArchIfPrimary,
                TargetLibstdc = TargetLibstdc,
                TargetLibsupc = TargetLibsupc,
                -- Global tables
                SYSTEM_ADD_ONS_BUS_MANAGERS = SYSTEM_ADD_ONS_BUS_MANAGERS or {},
                SYSTEM_ADD_ONS_FILE_SYSTEMS = SYSTEM_ADD_ONS_FILE_SYSTEMS or {},
                SYSTEM_ADD_ONS_DRIVERS_GRAPHICS = SYSTEM_ADD_ONS_DRIVERS_GRAPHICS or {},
                SYSTEM_ADD_ONS_DRIVERS_NET = SYSTEM_ADD_ONS_DRIVERS_NET or {},
                SYSTEM_ADD_ONS_DRIVERS_AUDIO = SYSTEM_ADD_ONS_DRIVERS_AUDIO or {},
                SYSTEM_ADD_ONS_DRIVERS_POWER = SYSTEM_ADD_ONS_DRIVERS_POWER or {},
                SYSTEM_ADD_ONS_DRIVERS_SENSOR = SYSTEM_ADD_ONS_DRIVERS_SENSOR or {},
                SYSTEM_ADD_ONS_ACCELERANTS = SYSTEM_ADD_ONS_ACCELERANTS or {},
                SYSTEM_ADD_ONS_TRANSLATORS = SYSTEM_ADD_ONS_TRANSLATORS or {},
                SYSTEM_ADD_ONS_MEDIA = SYSTEM_ADD_ONS_MEDIA or {},
                SYSTEM_ADD_ONS_MEDIA_PLUGINS = SYSTEM_ADD_ONS_MEDIA_PLUGINS or {},
                SYSTEM_ADD_ONS_PRINT = SYSTEM_ADD_ONS_PRINT or {},
                SYSTEM_ADD_ONS_SCREENSAVERS = SYSTEM_ADD_ONS_SCREENSAVERS or {},
                SYSTEM_ADD_ONS_LOCALE_CATALOGS = SYSTEM_ADD_ONS_LOCALE_CATALOGS or {},
                SYSTEM_SERVERS = SYSTEM_SERVERS or {},
                SYSTEM_BIN = SYSTEM_BIN or {},
                SYSTEM_APPS = SYSTEM_APPS or {},
                SYSTEM_PREFERENCES = SYSTEM_PREFERENCES or {},
                SYSTEM_DEMOS = SYSTEM_DEMOS or {},
                SYSTEM_NETWORK_DEVICES = SYSTEM_NETWORK_DEVICES or {},
                SYSTEM_NETWORK_PROTOCOLS = SYSTEM_NETWORK_PROTOCOLS or {},
                SYSTEM_NETWORK_DATALINK_PROTOCOLS = SYSTEM_NETWORK_DATALINK_PROTOCOLS or {},
                SYSTEM_NETWORK_PPP = SYSTEM_NETWORK_PPP or {},
                SYSTEM_BT_STACK = SYSTEM_BT_STACK or {},
                HAIKU_KEYMAP_FILES = HAIKU_KEYMAP_FILES or {},
                HAIKU_KEYMAP_ALIASES = HAIKU_KEYMAP_ALIASES or {},
                HAIKU_KEYBOARD_LAYOUT_FILES = HAIKU_KEYBOARD_LAYOUT_FILES or {},
                HAIKU_BOOT_TARGETS = HAIKU_BOOT_TARGETS or {},
                DESKBAR_APPLICATIONS = DESKBAR_APPLICATIONS or {},
                DESKBAR_DESKTOP_APPLETS = DESKBAR_DESKTOP_APPLETS or {},
                -- Standard library access
                path = path,
                os = os,
                table = table,
                string = string,
                print = print,
                pairs = pairs,
                ipairs = ipairs,
                tostring = tostring,
                tonumber = tonumber,
                type = type,
            }
            setfenv(chunk, env)
            return chunk()
        else
            print("Error loading package " .. package_name .. ": " .. tostring(err))
        end
    end
    return nil
end

-- ============================================================================
-- Package Build Orchestration
-- ============================================================================

--[[
    BuildCorePackages(architectures)

    Build core packages for the primary architecture.

    Parameters:
        architectures: Table of architectures (first is primary)
]]
function BuildCorePackages(architectures)
    local primary_arch = architectures[1]

    -- Build main package (Haiku or HaikuBootstrap)
    local main_package = GetMainPackageName()
    LoadPackageDefinition(main_package, primary_arch)

    -- Build core packages filtered by build features
    for _, package_name in ipairs(HAIKU_CORE_PACKAGES) do
        if FFilterByBuildFeatures and FFilterByBuildFeatures({package_name})[1] then
            LoadPackageDefinition(package_name, primary_arch)
        else
            LoadPackageDefinition(package_name, primary_arch)
        end
    end
end

--[[
    BuildArchDependentPackages(architectures)

    Build architecture-dependent packages for each architecture.

    Parameters:
        architectures: Table of all target architectures
]]
function BuildArchDependentPackages(architectures)
    for _, arch in ipairs(architectures) do
        for _, package_name in ipairs(HAIKU_ARCH_DEPENDENT_PACKAGES) do
            LoadPackageDefinition(package_name, arch)
        end
    end
end

--[[
    BuildSecondaryArchPackages(architectures)

    Build packages for secondary architectures only.

    Parameters:
        architectures: Table of all target architectures
]]
function BuildSecondaryArchPackages(architectures)
    -- Skip primary architecture (index 1)
    for i = 2, #architectures do
        local arch = architectures[i]

        -- Build secondary package (HaikuSecondary or HaikuSecondaryBootstrap)
        local secondary_package = GetSecondaryPackageName()
        LoadPackageDefinition(secondary_package, arch)

        -- Build secondary-only packages
        for _, package_name in ipairs(HAIKU_SECONDARY_ARCH_PACKAGES) do
            LoadPackageDefinition(package_name, arch)
        end
    end
end

--[[
    BuildAllPackages(architectures)

    Main entry point to build all packages.

    Parameters:
        architectures: Table of target architectures (first is primary)
]]
function BuildAllPackages(architectures)
    if not architectures or #architectures == 0 then
        architectures = {get_config("arch")}
    end

    print("Building packages for architectures: " .. table.concat(architectures, ", "))

    -- Build core packages
    BuildCorePackages(architectures)

    -- Build architecture-dependent packages
    BuildArchDependentPackages(architectures)

    -- Build secondary architecture packages
    BuildSecondaryArchPackages(architectures)
end

-- ============================================================================
-- Package Information
-- ============================================================================

--[[
    PACKAGE_INFO

    Metadata about each package.
]]
PACKAGE_INFO = {
    -- Main system packages
    Haiku = {
        output = "haiku.hpkg",
        description = "Main Haiku system package",
        includes_kernel = true,
        includes_servers = true,
        includes_apps = true,
    },
    HaikuBootstrap = {
        output = "haiku.hpkg",
        description = "Haiku bootstrap system package (minimal)",
        includes_kernel = true,
        includes_servers = true,
        includes_apps = true,
    },

    -- Development packages
    HaikuDevel = {
        output = "haiku_devel.hpkg",
        description = "Haiku development package (headers, libs)",
        includes_headers = true,
        includes_libs = true,
    },
    HaikuDevelSecondary = {
        output_pattern = "haiku_%s_devel.hpkg",  -- %s = architecture
        description = "Haiku development package for secondary architecture",
        includes_libs = true,
    },
    HaikuCrossDevel = {
        output_pattern = "haiku_cross_devel_%s.hpkg",  -- %s = arch combination
        description = "Cross-development sysroot package",
        includes_headers = true,
        includes_libs = true,
    },

    -- Secondary architecture packages
    HaikuSecondary = {
        output_pattern = "haiku_%s.hpkg",  -- %s = architecture
        description = "Haiku libraries for secondary architecture",
        includes_libs = true,
    },
    HaikuSecondaryBootstrap = {
        output_pattern = "haiku_%s.hpkg",  -- %s = architecture
        description = "Haiku bootstrap libraries for secondary architecture",
        includes_libs = true,
    },

    -- Feature packages
    HaikuDataTranslators = {
        output = "haiku_datatranslators.hpkg",
        description = "Data translators and media plugins",
    },
    HaikuExtras = {
        output = "haiku_extras.hpkg",
        description = "Extra add-ons (decorators, control looks)",
    },
    HaikuLoader = {
        output = "haiku_loader.hpkg",
        description = "Boot loader package",
        compression_level = 0,  -- No compression for bootloader
    },
    HaikuSource = {
        output = "haiku_source.hpkg",
        description = "Haiku source code package",
    },

    -- Tool packages
    MakefileEngine = {
        output = "makefile_engine.hpkg",
        description = "Makefile build system for Haiku apps",
    },

    -- Filesystem packages
    NetFS = {
        output = "netfs.hpkg",
        description = "Network filesystem",
    },
    UserlandFS = {
        output = "userland_fs.hpkg",
        description = "Userland filesystem support (FUSE)",
    },

    -- Applications
    WebPositive = {
        output = "webpositive.hpkg",
        description = "WebPositive web browser",
        build_feature = "webpositive",  -- Only built if feature enabled
    },
}

--[[
    GetPackageOutput(package_name, architecture)

    Returns the output filename for a package.

    Parameters:
        package_name: Name of the package
        architecture: Target architecture (for arch-specific packages)

    Returns:
        Output filename string
]]
function GetPackageOutput(package_name, architecture)
    local info = PACKAGE_INFO[package_name]
    if not info then
        return package_name:lower() .. ".hpkg"
    end

    if info.output_pattern and architecture then
        return string.format(info.output_pattern, architecture)
    end

    return info.output or (package_name:lower() .. ".hpkg")
end

--[[
    GetPackageCompressionLevel(package_name)

    Returns the compression level for a package.

    Parameters:
        package_name: Name of the package

    Returns:
        Compression level (nil for default)
]]
function GetPackageCompressionLevel(package_name)
    local info = PACKAGE_INFO[package_name]
    if info then
        return info.compression_level
    end
    return nil
end

--[[
    ShouldBuildPackage(package_name)

    Checks if a package should be built based on build features.

    Parameters:
        package_name: Name of the package

    Returns:
        true if package should be built
]]
function ShouldBuildPackage(package_name)
    local info = PACKAGE_INFO[package_name]
    if not info then
        return true
    end

    if info.build_feature then
        return FIsBuildFeatureEnabled and FIsBuildFeatureEnabled(info.build_feature)
    end

    return true
end

-- ============================================================================
-- Package Content Definitions (Static Data)
-- ============================================================================

--[[
    HAIKU_PACKAGE_BOOT_MODULES

    Boot modules that need symlinks in system/boot/
]]
HAIKU_PACKAGE_BOOT_MODULES = {
    -- ACPI (x86, x86_64, arm64)
    {name = "acpi", archs = {"x86", "x86_64", "arm64"}},

    -- Core bus managers
    "ahci",
    "ata",
    "ata_adapter",
    "dpc",
    "scsi",
    "scsi_periph",
    "usb",

    -- Filesystems
    "bfs",
    "packagefs",

    -- Partition systems
    "efi_gpt",
    "intel",

    -- IDE/SATA
    "generic_ide_pci",
    "legacy_sata",

    -- MMC
    "mmc",
    "mmc_disk",
    "sdhci",

    -- NVMe
    "nvme_disk",

    -- PCI bus (architecture-specific)
    "pci",
    {name = "designware", grist = "pci", archs = {"riscv64"}},
    {name = "ecam", grist = "pci", archs = {"riscv64", "arm", "arm64"}},
    {name = "x86", grist = "pci", archs = {"x86", "x86_64"}},

    -- FDT (flattened device tree)
    {name = "fdt", archs = {"riscv64", "arm", "arm64"}},

    -- ISA bus
    {name = "isa", archs = {"x86", "x86_64"}},

    -- SCSI
    "scsi_cd",
    "scsi_disk",

    -- USB
    "usb_disk",
    {name = "ehci", grist = "usb"},
    {name = "ohci", grist = "usb"},
    {name = "uhci", grist = "usb"},
    {name = "xhci", grist = "usb"},

    -- Virtio
    "virtio",
    "virtio_block",
    "virtio_pci",
    "virtio_scsi",
    {name = "virtio_mmio", archs = {"riscv64", "arm", "arm64"}},
}

--[[
    HAIKU_PACKAGE_KERNEL_MODULES

    Kernel modules organized by category.
]]
HAIKU_PACKAGE_KERNEL_MODULES = {
    bus_managers = {
        -- Populated from SYSTEM_ADD_ONS_BUS_MANAGERS
    },

    busses = {
        agp_gart = {
            {name = "intel", grist = "agp_gart", archs = {"x86", "x86_64"}},
        },
        ata = {
            "generic_ide_pci",
            "legacy_sata",
        },
        i2c = {
            {name = "pch_i2c", archs = {"x86", "x86_64"}},
        },
        mmc = {
            "sdhci",
        },
        pci = {
            {name = "designware", grist = "pci", archs = {"riscv64"}},
            {name = "ecam", grist = "pci", archs = {"riscv64", "arm", "arm64"}},
            {name = "x86", grist = "pci", archs = {"x86", "x86_64"}},
        },
        random = {
            {name = "ccp_rng", archs = {"x86", "x86_64"}},
            "virtio_rng",
        },
        scsi = {
            "ahci",
            "virtio_scsi",
        },
        usb = {
            {name = "uhci", grist = "usb"},
            {name = "ohci", grist = "usb"},
            {name = "ehci", grist = "usb"},
            {name = "xhci", grist = "usb"},
        },
        virtio = {
            {name = "virtio_mmio", archs = {"riscv64", "arm", "arm64"}},
            "virtio_pci",
        },
    },

    console = {
        "vga_text",
    },

    debugger = {
        {name = "demangle", grist = "kdebug"},
        {name = "disasm", grist = "kdebug", archs = {"x86", "x86_64"}},
        {name = "hangman", grist = "kdebug"},
        {name = "invalidate_on_exit", grist = "kdebug"},
        {name = "usb_keyboard", grist = "kdebug"},
        {name = "qrencode", grist = "kdebug", feature = "libqrencode"},
        {name = "run_on_exit", grist = "kdebug"},
    },

    file_systems = {
        -- Populated from SYSTEM_ADD_ONS_FILE_SYSTEMS
    },

    generic = {
        "ata_adapter",
        {name = "bios", archs = {"x86", "x86_64"}},
        "dpc",
        "scsi_periph",
        {name = "smbios", archs = {"x86", "x86_64"}},
        {name = "tty", grist = "module"},
    },

    partitioning_systems = {
        "efi_gpt",
        "intel",
        "session",
    },

    power = {
        cpufreq = {
            {name = "amd_pstates", archs = {"x86", "x86_64"}},
            {name = "intel_pstates", archs = {"x86", "x86_64"}},
        },
        cpuidle = {
            {name = "x86_cstates", archs = {"x86", "x86_64"}},
        },
    },

    cpu = {
        -- Only for x86/x86_64
        {name = "generic_x86", archs = {"x86", "x86_64"}},
    },
}

--[[
    HAIKU_PACKAGE_DRIVERS

    Driver definitions organized by category.
]]
HAIKU_PACKAGE_DRIVERS = {
    -- New-style drivers (AddNewDriversToPackage)
    new_style = {
        [""] = {  -- Root
            {name = "wmi", archs = {"x86", "x86_64"}},
        },
        disk = {
            "nvme_disk",
            "usb_disk",
        },
        ["disk/mmc"] = {
            "mmc_disk",
        },
        ["disk/scsi"] = {
            "scsi_cd",
            "scsi_disk",
        },
        ["disk/virtual"] = {
            "ram_disk",
            "virtio_block",
        },
        graphics = {
            "virtio_gpu",
        },
        network = {
            "usb_ecm",
            "virtio_net",
        },
        input = {
            "i2c_elan",
            "virtio_input",
        },
    },

    -- Legacy drivers (AddDriversToPackage)
    legacy = {
        [""] = {  -- Root
            "console",
            "dprintf",
            "null",
            {name = "pty", grist = "driver"},
            "usb_modeswitch",
        },
        bus = {
            "usb_raw",
        },
        ["disk/virtual"] = {
            "nbd",
        },
        input = {
            "ps2_hid",
            "usb_hid",
            "wacom",
        },
        misc = {
            {name = "poke", grist = "driver"},
            {name = "mem", grist = "driver"},
        },
        ports = {
            "pc_serial",
            "usb_serial",
        },
    },
}