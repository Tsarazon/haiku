-- NetBootArchive.lua - Network Boot Archive builder
-- Ported from build/jam/images/NetBootArchive
--
-- This file defines what ends up in the network boot archive and it executes
-- the rules building the archive.
--
-- The network boot archive is a minimal bootable system that can be loaded
-- over the network via PXE/TFTP for diskless booting.

-- ============================================================================
-- Container Configuration
-- ============================================================================

-- Container name for net boot archive (equivalent to HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME in Jam)
local HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME = "haiku-net-boot-archive"

local function get_net_boot_archive_container_name()
    return HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME
end

-- ============================================================================
-- Network Configuration
-- ============================================================================

local _SYSTEM_NETWORK_DEVICES = {
    "ethernet",
    "loopback",
}

local _SYSTEM_NETWORK_DATALINK_PROTOCOLS = {
    "ethernet_frame",
    "<module>arp",
    "loopback_frame",
}

local _SYSTEM_NETWORK_PROTOCOLS = {
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
local _SYSTEM_ADD_ONS_DRIVERS_NET = {
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

-- ============================================================================
-- Bus Managers
-- ============================================================================

-- NOTE: Raw data - FFilterByBuildFeatures will be called lazily when needed
local _SYSTEM_ADD_ONS_BUS_MANAGERS = {
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

-- ============================================================================
-- File Systems (minimal for netboot)
-- ============================================================================

local _SYSTEM_ADD_ONS_FILE_SYSTEMS = {
    "bfs",
    "packagefs",
}

-- ============================================================================
-- Archive Content Setup
-- ============================================================================

function SetupNetBootArchiveContent()
    -- Import ImageRules for container functions
    local ImageRules = import("rules.ImageRules")
    local config = import("core.project.config")

    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local target_arch = get_config("arch") or "x86_64"

    -- Modules: Bus managers
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "bus_managers"},
        _SYSTEM_ADD_ONS_BUS_MANAGERS
    )

    -- AGP gart busses
    if target_arch == "x86" or target_arch == "x86_64" then
        ImageRules.AddFilesToNetBootArchive(
            {"system", "add-ons", "kernel", "busses", "agp_gart"},
            {"<agp_gart>intel"}
        )
    end

    -- IDE busses
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "busses", "ide"},
        {"generic_ide_pci"}
    )

    -- SCSI busses
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "busses", "scsi"},
        {"ahci"}
    )

    -- Console
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "console"},
        {"vga_text"}
    )

    -- File systems
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "file_systems"},
        _SYSTEM_ADD_ONS_FILE_SYSTEMS
    )

    -- Generic kernel modules
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "generic"},
        {"ata_adapter", "scsi_periph"}
    )

    -- Partitioning systems
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "partitioning_systems"},
        {"intel", "session"}
    )

    -- Interrupt controllers (PPC only)
    if target_arch == "ppc" then
        ImageRules.AddFilesToNetBootArchive(
            {"system", "add-ons", "kernel", "interrupt_controllers"},
            {"openpic"}
        )
    end

    -- CPU modules
    if target_arch == "x86" or target_arch == "x86_64" then
        ImageRules.AddFilesToNetBootArchive(
            {"system", "add-ons", "kernel", "cpu"},
            {"generic_x86"}
        )
    end

    -- Drivers: disk/scsi
    ImageRules.AddNewDriversToNetBootArchive(
        {"disk", "scsi"},
        {"scsi_cd", "scsi_disk"}
    )

    -- Drivers: disk/virtual
    ImageRules.AddDriversToNetBootArchive(
        {"disk", "virtual"},
        {"remote_disk"}  -- nbd could be added here
    )

    -- Drivers: network
    ImageRules.AddDriversToNetBootArchive(
        {"net"},
        _SYSTEM_ADD_ONS_DRIVERS_NET
    )

    -- Kernel
    ImageRules.AddFilesToNetBootArchive(
        {"system"},
        {string.format("<revisioned>kernel_%s", target_arch)}
    )

    -- Driver settings
    local driver_settings = path.join(haiku_top, "data", "settings", "kernel", "drivers", "kernel")
    ImageRules.AddFilesToNetBootArchive(
        {"home", "config", "settings", "kernel", "drivers"},
        {driver_settings},
        "kernel"
    )

    -- Network stack
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "network"},
        {"stack"}
    )

    -- Network devices
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "network", "devices"},
        _SYSTEM_NETWORK_DEVICES
    )

    -- Network datalink protocols
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "network", "datalink_protocols"},
        _SYSTEM_NETWORK_DATALINK_PROTOCOLS
    )

    -- Network protocols
    ImageRules.AddFilesToNetBootArchive(
        {"system", "add-ons", "kernel", "network", "protocols"},
        _SYSTEM_NETWORK_PROTOCOLS
    )

    -- Boot module symlinks
    local boot_modules = TableMerge(
        _SYSTEM_ADD_ONS_BUS_MANAGERS,
        {"ahci", "generic_ide_pci"},
        _SYSTEM_ADD_ONS_FILE_SYSTEMS,
        {"ata_adapter", "scsi_periph"},
        {"intel", "session"},
        {"remote_disk"},
        _SYSTEM_ADD_ONS_DRIVERS_NET,
        {"stack"},
        _SYSTEM_NETWORK_DEVICES,
        _SYSTEM_NETWORK_DATALINK_PROTOCOLS,
        _SYSTEM_NETWORK_PROTOCOLS
    )

    -- Add arch-specific boot modules
    if target_arch == "ppc" then
        table.insert(boot_modules, "openpic")
    end
    if target_arch == "x86" or target_arch == "x86_64" then
        table.insert(boot_modules, "generic_x86")
    end

    ImageRules.AddBootModuleSymlinksToNetBootArchive(boot_modules)
end

-- ============================================================================
-- Archive Building
-- ============================================================================

function BuildNetBootArchiveTarget()
    -- Import ImageRules for build functions
    local ImageRules = import("rules.ImageRules")
    local config = import("core.project.config")

    -- haiku_top is the source root
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")

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
        string.format('copyattr="%s/tools/copyattr"', output_dir),
        "",
    }
    io.writefile(init_script, table.concat(init_content, "\n"))

    -- Create directory/file scripts
    local make_dirs_script = path.join(output_dir, "haiku-netboot-make-dirs")
    local copy_files_script = path.join(output_dir, "haiku-netboot-copy-files")

    ImageRules.CreateNetBootArchiveMakeDirectoriesScript(make_dirs_script)
    ImageRules.CreateNetBootArchiveCopyFilesScript(copy_files_script)

    -- Build the archive
    local scripts = {
        init_script,
        make_dirs_script,
        copy_files_script,
    }

    ImageRules.BuildNetBootArchive(archive_path, scripts)

    return archive_path
end

-- ============================================================================
-- xmake Target
-- ============================================================================

if target then

target("haiku-netboot-archive")
    set_kind("phony")
    on_build(function (target)
        import("images.NetBootArchive")
        print("Building network boot archive...")
        local archive = NetBootArchive.BuildNetBootArchiveTarget()
        print("Network boot archive built: " .. (archive or "unknown"))
    end)

end -- if target

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
-- Getter Functions (xmake only exports functions, not variables)
-- ============================================================================

function SYSTEM_NETWORK_DEVICES() return _SYSTEM_NETWORK_DEVICES end
function SYSTEM_NETWORK_DATALINK_PROTOCOLS() return _SYSTEM_NETWORK_DATALINK_PROTOCOLS end
function SYSTEM_NETWORK_PROTOCOLS() return _SYSTEM_NETWORK_PROTOCOLS end
function SYSTEM_ADD_ONS_DRIVERS_NET() return _SYSTEM_ADD_ONS_DRIVERS_NET end
function SYSTEM_ADD_ONS_BUS_MANAGERS() return _SYSTEM_ADD_ONS_BUS_MANAGERS end
function SYSTEM_ADD_ONS_FILE_SYSTEMS() return _SYSTEM_ADD_ONS_FILE_SYSTEMS end

-- ============================================================================
-- Module Exports (xmake exports global functions automatically)
-- ============================================================================
-- All functions above are global and automatically exported by xmake.
-- No return statement needed.