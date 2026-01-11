-- NetBootArchive.lua - Network Boot Archive builder
-- Ported from build/jam/images/NetBootArchive
--
-- This file defines what ends up in the network boot archive and it executes
-- the rules building the archive.
--
-- The network boot archive is a minimal bootable system that can be loaded
-- over the network via PXE/TFTP for diskless booting.

-- import("rules.ImageRules")
-- import("rules.BuildFeatureRules")

-- ============================================================================
-- Network Configuration
-- ============================================================================

SYSTEM_NETWORK_DEVICES = {
    "ethernet",
    "loopback",
}

SYSTEM_NETWORK_DATALINK_PROTOCOLS = {
    "ethernet_frame",
    "<module>arp",
    "loopback_frame",
}

SYSTEM_NETWORK_PROTOCOLS = {
    "ipv4",
    "tcp",
    "udp",
    "icmp",
    "unix",
}

-- ============================================================================
-- Network Drivers
-- ============================================================================

-- NOTE: Raw data - FFilterByBuildFeatures will be called lazily when GetNetDrivers() is invoked
local _NET_DRIVERS_RAW = {
    -- x86, x86_64 specific
    "atheros813x@x86,x86_64",
    "atheros81xx@x86,x86_64",
    "attansic_l2@x86,x86_64",
    "broadcom440x@x86,x86_64",
    "broadcom570x@x86,x86_64",
    "ipro100@x86,x86_64",
    "ipro1000@x86,x86_64",
    "marvell_yukon@x86,x86_64",
    "rtl8139@x86,x86_64",
    "syskonnect@x86,x86_64",
    -- All architectures
    "etherpci",
    "pegasus",
    "rtl81xx",
    "usb_ecm",
    "wb840",
}
SYSTEM_ADD_ONS_DRIVERS_NET = _NET_DRIVERS_RAW

-- ============================================================================
-- Bus Managers
-- ============================================================================

-- NOTE: Raw data - FFilterByBuildFeatures will be called lazily when needed
local _BUS_MANAGERS_RAW = {
    "pci",
    "<pci>x86@x86,x86_64",
    "isa@x86",
    "usb",
    "ata",
    "scsi",
    "agp_gart",
    "dpc",
    "acpi",
}
SYSTEM_ADD_ONS_BUS_MANAGERS = _BUS_MANAGERS_RAW

-- ============================================================================
-- File Systems (minimal for netboot)
-- ============================================================================

SYSTEM_ADD_ONS_FILE_SYSTEMS = {
    "bfs",
    "packagefs",
}

-- ============================================================================
-- Archive Content Setup
-- ============================================================================

function SetupNetBootArchiveContent()
    local haiku_top = os.projectdir()
    local target_arch = get_config("arch") or "x86_64"

    -- Modules: Bus managers
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "bus_managers"},
        SYSTEM_ADD_ONS_BUS_MANAGERS
    )

    -- AGP gart busses
    if target_arch == "x86" or target_arch == "x86_64" then
        AddFilesToNetBootArchive(
            {"system", "add-ons", "kernel", "busses", "agp_gart"},
            {"<agp_gart>intel"}
        )
    end

    -- IDE busses
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "busses", "ide"},
        {"generic_ide_pci"}
    )

    -- SCSI busses
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "busses", "scsi"},
        {"ahci"}
    )

    -- Console
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "console"},
        {"vga_text"}
    )

    -- File systems
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "file_systems"},
        SYSTEM_ADD_ONS_FILE_SYSTEMS
    )

    -- Generic kernel modules
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "generic"},
        {"ata_adapter", "scsi_periph"}
    )

    -- Partitioning systems
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "partitioning_systems"},
        {"intel", "session"}
    )

    -- Interrupt controllers (PPC only)
    if target_arch == "ppc" then
        AddFilesToNetBootArchive(
            {"system", "add-ons", "kernel", "interrupt_controllers"},
            {"openpic"}
        )
    end

    -- CPU modules
    if target_arch == "x86" or target_arch == "x86_64" then
        AddFilesToNetBootArchive(
            {"system", "add-ons", "kernel", "cpu"},
            {"generic_x86"}
        )
    end

    -- Drivers: disk/scsi
    AddNewDriversToNetBootArchive(
        {"disk", "scsi"},
        {"scsi_cd", "scsi_disk"}
    )

    -- Drivers: disk/virtual
    AddDriversToNetBootArchive(
        {"disk", "virtual"},
        {"remote_disk"}  -- nbd could be added here
    )

    -- Drivers: network
    AddDriversToNetBootArchive(
        {"net"},
        SYSTEM_ADD_ONS_DRIVERS_NET
    )

    -- Kernel
    AddFilesToNetBootArchive(
        {"system"},
        {string.format("<revisioned>kernel_%s", target_arch)}
    )

    -- Driver settings
    local driver_settings = path.join(haiku_top, "data", "settings", "kernel", "drivers", "kernel")
    AddFilesToNetBootArchive(
        {"home", "config", "settings", "kernel", "drivers"},
        {driver_settings},
        "kernel"
    )

    -- Network stack
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "network"},
        {"stack"}
    )

    -- Network devices
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "network", "devices"},
        SYSTEM_NETWORK_DEVICES
    )

    -- Network datalink protocols
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "network", "datalink_protocols"},
        SYSTEM_NETWORK_DATALINK_PROTOCOLS
    )

    -- Network protocols
    AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "network", "protocols"},
        SYSTEM_NETWORK_PROTOCOLS
    )

    -- Boot module symlinks
    local boot_modules = TableMerge(
        SYSTEM_ADD_ONS_BUS_MANAGERS,
        {"ahci", "generic_ide_pci"},
        SYSTEM_ADD_ONS_FILE_SYSTEMS,
        {"ata_adapter", "scsi_periph"},
        {"intel", "session"},
        {"remote_disk"},
        SYSTEM_ADD_ONS_DRIVERS_NET,
        {"stack"},
        SYSTEM_NETWORK_DEVICES,
        SYSTEM_NETWORK_DATALINK_PROTOCOLS,
        SYSTEM_NETWORK_PROTOCOLS
    )

    -- Add arch-specific boot modules
    if target_arch == "ppc" then
        table.insert(boot_modules, "openpic")
    end
    if target_arch == "x86" or target_arch == "x86_64" then
        table.insert(boot_modules, "generic_x86")
    end

    AddBootModuleSymlinksToNetBootArchive(boot_modules)
end

-- ============================================================================
-- Archive Building
-- ============================================================================

function BuildNetBootArchiveTarget()
    local haiku_top = os.projectdir()
    local output_dir = path.join(haiku_top, "generated")

    -- Archive target
    local archive_name = "haiku-netboot.tgz"
    local archive_path = path.join(output_dir, archive_name)

    -- Container name
    local container_name = get_net_boot_archive_container_name()

    -- Setup content
    SetupNetBootArchiveContent()

    -- Prepare init script
    local init_script = path.join(output_dir, "haiku-netboot-init-vars")
    local tmp_dir = path.join(output_dir, "tmp")

    local init_content = {
        "#!/bin/sh",
        "# Auto-generated netboot archive initialization script",
        "",
        string.format('tmpDir="%s"', tmp_dir),
        string.format('copyattr="%s/generated/tools/copyattr"', haiku_top),
        "",
    }
    io.writefile(init_script, table.concat(init_content, "\n"))

    -- Create directory/file scripts
    local make_dirs_script = path.join(output_dir, "haiku-netboot-make-dirs")
    local copy_files_script = path.join(output_dir, "haiku-netboot-copy-files")

    CreateNetBootArchiveMakeDirectoriesScript(make_dirs_script)
    CreateNetBootArchiveCopyFilesScript(copy_files_script)

    -- Build the archive
    local scripts = {
        init_script,
        make_dirs_script,
        copy_files_script,
    }

    BuildNetBootArchive(archive_path, scripts)

    return archive_path
end

-- ============================================================================
-- xmake Target
-- ============================================================================

target("haiku-netboot-archive")
    set_kind("phony")

    on_build(function (target)
        print("Building network boot archive...")
        -- Access global function via _G since callbacks run in sandbox
        if _G.BuildNetBootArchiveTarget then
            local archive = _G.BuildNetBootArchiveTarget()
            print("Network boot archive built: " .. (archive or "unknown"))
        else
            print("Warning: BuildNetBootArchiveTarget not available yet")
        end
    end)

-- ============================================================================
-- Helper Functions
-- ============================================================================

-- Merge multiple tables
function TableMerge(...)
    local result = {}
    for _, t in ipairs({...}) do
        if type(t) == "table" then
            for _, v in ipairs(t) do
                table.insert(result, v)
            end
        else
            table.insert(result, t)
        end
    end
    return result
end

-- ============================================================================
-- Module Exports
-- ============================================================================

return {
    SYSTEM_NETWORK_DEVICES = SYSTEM_NETWORK_DEVICES,
    SYSTEM_NETWORK_DATALINK_PROTOCOLS = SYSTEM_NETWORK_DATALINK_PROTOCOLS,
    SYSTEM_NETWORK_PROTOCOLS = SYSTEM_NETWORK_PROTOCOLS,
    SYSTEM_ADD_ONS_DRIVERS_NET = SYSTEM_ADD_ONS_DRIVERS_NET,
    SYSTEM_ADD_ONS_BUS_MANAGERS = SYSTEM_ADD_ONS_BUS_MANAGERS,
    SYSTEM_ADD_ONS_FILE_SYSTEMS = SYSTEM_ADD_ONS_FILE_SYSTEMS,
    SetupNetBootArchiveContent = SetupNetBootArchiveContent,
    BuildNetBootArchiveTarget = BuildNetBootArchiveTarget,
    TableMerge = TableMerge,
}