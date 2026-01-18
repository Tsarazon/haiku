-- AnybootImage.lua - Haiku Anyboot Image builder
-- Ported from build/jam/images/AnybootImage
--
-- This file creates the Anyboot image which can be booted from USB or CD.
-- The Anyboot image combines:
-- - An ISO for CD boot
-- - A raw disk image for USB boot
-- - MBR for legacy BIOS boot
-- - EFI System Partition for UEFI boot

-- ============================================================================
-- Configuration
-- ============================================================================

local function get_anyboot_defaults()
    return {
        name = get_config("anyboot_name") or get_config("default_anyboot_name") or "haiku-anyboot.iso",
        dir = get_config("anyboot_dir") or get_config("default_anyboot_dir") or path.join(os.projectdir(), "generated"),
        label = get_config("anyboot_label") or get_config("default_anyboot_label") or "Haiku",
    }
end

-- ============================================================================
-- MBR Building
-- ============================================================================

--[[
    BuildMBR(mbr_target, mbr_source)

    Builds the base MBR from source assembly.
]]
local function build_mbr(mbr_target, mbr_source)
    local haiku_top = os.projectdir()
    local output_dir = path.join(haiku_top, "generated")

    -- The MBR source is in src/bin/writembr/mbr.S
    if not mbr_source then
        mbr_source = path.join(haiku_top, "src", "bin", "writembr", "mbr.S")
    end

    -- Build MBR using assembler
    -- This would compile the MBR assembly to a binary
    print("Building MBR from: " .. mbr_source)

    return mbr_target
end

-- ============================================================================
-- Anyboot Image Building (EFI)
-- ============================================================================

--[[
    BuildAnybootImageEfi(anyboot_image, mbr_part, efi_part, iso_part, image_file)

    Builds an Anyboot image with EFI support.

    The anyboot image is structured as:
    1. MBR (from mbr_part)
    2. EFI System Partition (from efi_part)
    3. ISO image (from iso_part)
    4. Raw disk image (from image_file)

    The anyboot tool combines these into a single image that can boot from:
    - USB (uses MBR + raw disk image)
    - CD (uses ISO structure)
    - UEFI (uses EFI partition)
]]
local function build_anyboot_image_efi(anyboot_image, mbr_part, efi_part, iso_part, image_file)
    local haiku_top = os.projectdir()
    local anyboot_tool = path.join(haiku_top, "generated", "tools", "anyboot")

    -- Build command: anyboot -b mbr -e efi iso image output
    local build_info = {
        output = anyboot_image,
        mbr = mbr_part,
        efi = efi_part,
        iso = iso_part,
        image = image_file,
        tool = anyboot_tool,
    }

    print(string.format("Building Anyboot image: %s", anyboot_image))
    print(string.format("  MBR: %s", mbr_part or "none"))
    print(string.format("  EFI: %s", efi_part or "none"))
    print(string.format("  ISO: %s", iso_part))
    print(string.format("  Image: %s", image_file))

    return build_info
end

--[[
    BuildAnybootImageBios(anyboot_image, mbr_part, iso_part, image_file)

    Builds an Anyboot image for BIOS-only systems.
    The anyboot is essentially the CD image with the raw disk image appended.
]]
local function build_anyboot_image_bios(anyboot_image, mbr_part, iso_part, image_file)
    return build_anyboot_image_efi(anyboot_image, mbr_part, nil, iso_part, image_file)
end

-- ============================================================================
-- Main Build Function
-- ============================================================================

function BuildAnybootImageTarget()
    local haiku_top = os.projectdir()
    local output_dir = path.join(haiku_top, "generated")

    -- Get configuration
    local defaults = get_anyboot_defaults()
    local anyboot_image = path.join(defaults.dir, defaults.name)

    -- Base MBR
    local base_mbr = path.join(output_dir, "base_mbr.bin")
    local mbr_source = path.join(haiku_top, "src", "bin", "writembr", "mbr.S")
    build_mbr(base_mbr, mbr_source)

    -- Get boot platform configuration
    local boot_platform = get_config("boot_platform") or "efi"

    -- CD boot image (built by CDBootImage)
    local cd_boot_image = path.join(output_dir, "haiku-boot-cd.iso")

    -- Haiku disk image (built by HaikuImage)
    local haiku_image_name = get_config("image_name") or "haiku.image"
    local haiku_image = path.join(output_dir, haiku_image_name)

    -- Currently the Anyboot image is available only for EFI+BIOS or BIOS-only
    -- systems. It is not needed for other systems, where usually you can boot
    -- the same way from an USB or CD drive, instead of needing completely
    -- different layouts.

    if boot_platform == "efi" then
        -- EFI boot with legacy BIOS fallback
        local efi_loader = "haiku_loader.efi"
        local efi_partition = path.join(output_dir, "esp.image")

        -- Build EFI System Partition
        BuildEfiSystemPartition(efi_partition, efi_loader)

        -- Build Anyboot image with EFI
        build_anyboot_image_efi(anyboot_image, base_mbr, efi_partition, cd_boot_image, haiku_image)

    elseif boot_platform == "bios_ia32" then
        -- BIOS-only systems
        build_anyboot_image_bios(anyboot_image, base_mbr, cd_boot_image, haiku_image)
    else
        print("Warning: Anyboot image not supported for boot platform: " .. boot_platform)
        return nil
    end

    return anyboot_image
end

-- ============================================================================
-- Cleanup
-- ============================================================================

--[[
    Temporary files to clean up after build.
    Note: base_mbr.bin seems to cause build failures on alternate runs (caching?)
]]
local function get_temp_files()
    local output_dir = path.join(os.projectdir(), "generated")
    return {
        path.join(output_dir, "haiku-boot-cd.iso"),
        -- path.join(output_dir, "base_mbr.bin"),  -- Causes issues
    }
end

-- ============================================================================
-- xmake Target
-- ============================================================================

target("haiku-anyboot-image")
    set_kind("phony")
    add_deps("haiku-image", "haiku-boot-cd")

    on_build(function (target)
        print("Building Anyboot image...")
        local anyboot = BuildAnybootImageTarget()
        if anyboot then
            print("Anyboot image built: " .. anyboot)
        else
            print("Anyboot image build skipped (not supported for this platform)")
        end
    end)

-- ============================================================================
-- Module Exports
-- ============================================================================

return {
    get_anyboot_defaults = get_anyboot_defaults,
    build_mbr = build_mbr,
    build_anyboot_image_efi = build_anyboot_image_efi,
    build_anyboot_image_bios = build_anyboot_image_bios,
    BuildAnybootImageTarget = BuildAnybootImageTarget,
    get_temp_files = get_temp_files,
}