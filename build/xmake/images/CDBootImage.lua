-- CDBootImage.lua - CD Boot Image builder
-- Ported from build/jam/images/CDBootImage
--
-- This file defines what ends up in the CD boot image and it executes the
-- rules building the image.

-- ============================================================================
-- Private helper (global with _ prefix = private, not exported but accessible)
-- ============================================================================

function _get_config(key)
    local config = import("core.project.config", {try = true})
    return config and config.get(key) or nil
end

-- ============================================================================
-- Configuration
-- ============================================================================

-- Container name for CD boot image
local HAIKU_CD_BOOT_IMAGE_CONTAINER_NAME = "haiku-cd-boot-image"

-- CD boot image file name
local function get_cd_boot_image_name()
    return "haiku-boot-cd.iso"
end

-- ============================================================================
-- Extra Files
-- ============================================================================

-- Common extra files to put on the boot ISO
local function get_extra_files()
    local config = import("core.project.config", {try = true})
    local haiku_top = config and config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local extras_dir = path.join(haiku_top, "data", "boot", "extras")

    return {
        path.join(extras_dir, "README.html"),
    }
end

-- ============================================================================
-- Platform-Specific Build Functions
-- ============================================================================

--[[
    Build CD boot image for EFI systems.

    Creates an ISO with:
    - EFI System Partition for UEFI boot
    - MBR for legacy BIOS compatibility
]]
local function build_cd_boot_image_efi(cd_image, boot_mbr, efi_partition, extras)
    local ImageRules = import("rules.ImageRules")
    return ImageRules.BuildCDBootImageEFI(cd_image, boot_mbr, efi_partition, extras)
end

--[[
    Build CD boot image for BIOS-only systems.

    Creates an ISO with:
    - El Torito boot record
    - BIOS bootloader
]]
local function build_cd_boot_image_bios(cd_image, boot_mbr, bios_loader, extras)
    local ImageRules = import("rules.ImageRules")
    return ImageRules.BuildCDBootImageBIOS(cd_image, boot_mbr, bios_loader, extras)
end

-- ============================================================================
-- EFI System Partition
-- ============================================================================

--[[
    Build EFI System Partition for CD boot.

    Creates a FAT image containing:
    - EFI/BOOT/BOOT*.EFI - The EFI loader
    - .VolumeIcon.icns - Mac volume icon
    - KEYS/* - UEFI signing keys
]]
local function build_efi_system_partition_for_cd(efi_partition, efi_loader)
    local ImageRules = import("rules.ImageRules")
    return ImageRules.BuildEfiSystemPartition(efi_partition, efi_loader)
end

-- ============================================================================
-- Main Build Function
-- ============================================================================

function BuildCDBootImageTarget()
    local config = import("core.project.config")
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")

    -- CD boot image target
    local cd_boot_image = path.join(output_dir, get_cd_boot_image_name())

    -- Extra files
    local extras = get_extra_files()

    -- Get boot platform configuration
    local boot_platform = _get_config("boot_platform") or "efi"
    local target_arch = _get_config("arch") or "x86_64"

    -- Base MBR for boot
    local boot_mbr = path.join(output_dir, "base_mbr.bin")

    if boot_platform == "efi" then
        -- EFI boot
        local efi_loader = "haiku_loader.efi"
        local efi_partition = path.join(output_dir, "esp.image")

        -- Build EFI System Partition
        build_efi_system_partition_for_cd(efi_partition, efi_loader)

        -- Build CD boot image with EFI support
        build_cd_boot_image_efi(cd_boot_image, boot_mbr, efi_partition, extras)

    elseif boot_platform == "bios_ia32" then
        -- BIOS-only boot
        local bios_loader = "haiku_loader.bios_ia32"

        -- Build CD boot image for BIOS
        build_cd_boot_image_bios(cd_boot_image, boot_mbr, bios_loader, extras)
    end

    return cd_boot_image
end

-- ============================================================================
-- Multi-Boot Support
-- ============================================================================

--[[
    Setup CD boot image for all configured boot platforms.

    Haiku supports multiple boot platforms simultaneously on some architectures.
]]
function SetupMultiBootCDImage()
    local boot_platforms = _get_config("boot_platforms") or {"efi"}

    for _, platform in ipairs(boot_platforms) do
        -- Each platform may need its own configuration
        -- This is handled by MultiBootSubDirSetup in Jam
    end
end

-- ============================================================================
-- xmake Target
-- ============================================================================

if target then

target("haiku-boot-cd")
    set_kind("phony")

    on_build(function (target)
        import("images.CDBootImage")
        print("Building CD boot image...")
        local cd_image = CDBootImage.BuildCDBootImageTarget()
        print("CD boot image built: " .. (cd_image or "unknown"))
    end)

end -- if target

-- ============================================================================
-- Module Exports
-- ============================================================================

return {
    HAIKU_CD_BOOT_IMAGE_CONTAINER_NAME = HAIKU_CD_BOOT_IMAGE_CONTAINER_NAME,
    get_cd_boot_image_name = get_cd_boot_image_name,
    get_extra_files = get_extra_files,
    build_cd_boot_image_efi = build_cd_boot_image_efi,
    build_cd_boot_image_bios = build_cd_boot_image_bios,
    build_efi_system_partition_for_cd = build_efi_system_partition_for_cd,
    BuildCDBootImageTarget = BuildCDBootImageTarget,
    SetupMultiBootCDImage = SetupMultiBootCDImage,
}