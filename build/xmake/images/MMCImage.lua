-- MMCImage.lua - SD/MMC Image builder
-- Ported from build/jam/images/MMCImage
--
-- This file creates SD/MMC card images for ARM, RISC-V, and other
-- embedded platforms that boot from SD cards.

-- import("rules.ImageRules")
-- import("rules.BootRules")

-- ============================================================================
-- Configuration
-- ============================================================================

local function get_mmc_defaults()
    return {
        name = get_config("mmc_name") or get_config("default_mmc_name") or "haiku-mmc.image",
        dir = get_config("mmc_dir") or get_config("default_mmc_dir") or path.join(os.projectdir(), "generated"),
        label = get_config("mmc_label") or get_config("default_mmc_label") or "Haiku",
    }
end

-- ============================================================================
-- EFI Boot File Names
-- ============================================================================

local function get_efi_boot_name(arch)
    local names = {
        x86 = "BOOTIA32.EFI",
        x86_64 = "BOOTX64.EFI",
        arm = "BOOTARM.EFI",
        arm64 = "BOOTAA64.EFI",
        riscv32 = "BOOTRISCV32.EFI",
        riscv64 = "BOOTRISCV64.EFI",
    }
    return names[arch] or error("Unknown EFI architecture: " .. tostring(arch))
end

-- ============================================================================
-- U-Boot SD Image
-- ============================================================================

--[[
    BuildUBootSDImage(image, mbrtool, fatshell, os_image, loader, floppyboot, extras)

    Builds an SD card image for U-Boot based systems.

    The image layout:
    - Partition 0: EFI System Partition (FAT, contains U-Boot env and loader)
    - Partition 1: Haiku OS partition (BFS)

    The FAT partition contains:
    - uEnv.txt - U-Boot environment variables
    - haiku_loader.ub - Haiku bootloader for U-Boot
    - boot.scr - U-Boot boot script (optional)
    - fdt/ - Device tree files directory
]]
local function build_uboot_sd_image(image, files)
    local haiku_top = os.projectdir()
    local output_dir = path.join(haiku_top, "generated")

    local mbrtool = path.join(output_dir, "tools", "mbrtool")
    local fatshell = path.join(output_dir, "tools", "fat_shell")

    local os_image = files.os_image
    local loader = files.loader
    local extras = files.extras or {}

    -- Image sizes in KiB
    local EFI_SIZE = 32768  -- 32 MB
    local SDIMAGE_BEGIN = get_config("boot_sdimage_begin") or 63
    local BOOT_SIZE = EFI_SIZE + SDIMAGE_BEGIN
    local OS_SIZE = get_config("image_size") or 800  -- MB
    local MMC_SIZE = OS_SIZE * 1024 + BOOT_SIZE

    print(string.format("Building U-Boot SD image: %s", image))
    print(string.format("  EFI partition: %d KiB", EFI_SIZE))
    print(string.format("  OS partition: %d MB", OS_SIZE))

    local build_info = {
        output = image,
        mbrtool = mbrtool,
        fatshell = fatshell,
        os_image = os_image,
        loader = loader,
        extras = extras,
        efi_size = EFI_SIZE,
        boot_size = BOOT_SIZE,
        mmc_size = MMC_SIZE,
        sdimage_begin = SDIMAGE_BEGIN,
        -- uEnv.txt contents
        uenv = {
            os = "haiku",
            platform = "u-boot",
            loader = "haiku_loader.ub",
        },
    }

    return build_info
end

-- ============================================================================
-- EFI SD Image
-- ============================================================================

--[[
    BuildEfiSDImage(image, mbrtool, fatshell, os_image, loader, extras)

    Builds an SD card image for EFI based systems.

    The image layout:
    - Partition 0: EFI System Partition (FAT, contains EFI loader)
    - Partition 1: Haiku OS partition (BFS)

    The FAT partition contains:
    - EFI/BOOT/BOOT*.EFI - The EFI loader
    - uEnv.txt - Environment variables (for systems that also support U-Boot)
    - fdt/ - Device tree files directory (optional)
]]
local function build_efi_sd_image(image, files)
    local haiku_top = os.projectdir()
    local output_dir = path.join(haiku_top, "generated")

    local mbrtool = path.join(output_dir, "tools", "mbrtool")
    local fatshell = path.join(output_dir, "tools", "fat_shell")

    local os_image = files.os_image
    local loader = files.loader
    local extras = files.extras or {}
    local target_arch = get_config("arch") or "x86_64"
    local efi_name = get_efi_boot_name(target_arch)

    -- Image sizes in KiB
    local EFI_SIZE = 32768  -- 32 MB
    local SDIMAGE_BEGIN = get_config("boot_sdimage_begin") or 63
    local BOOT_SIZE = EFI_SIZE + SDIMAGE_BEGIN
    local OS_SIZE = get_config("image_size") or 800  -- MB
    local MMC_SIZE = OS_SIZE * 1024 + BOOT_SIZE

    print(string.format("Building EFI SD image: %s", image))
    print(string.format("  EFI partition: %d KiB", EFI_SIZE))
    print(string.format("  OS partition: %d MB", OS_SIZE))
    print(string.format("  EFI loader: %s -> EFI/BOOT/%s", loader, efi_name))

    local build_info = {
        output = image,
        mbrtool = mbrtool,
        fatshell = fatshell,
        os_image = os_image,
        loader = loader,
        efi_name = efi_name,
        extras = extras,
        efi_size = EFI_SIZE,
        boot_size = BOOT_SIZE,
        mmc_size = MMC_SIZE,
        sdimage_begin = SDIMAGE_BEGIN,
        -- uEnv.txt contents
        uenv = {
            os = "haiku",
            platform = "efi",
            loader = "EFI/BOOT/" .. efi_name,
        },
    }

    return build_info
end

-- ============================================================================
-- Main Build Function
-- ============================================================================

function BuildMMCImageTarget()
    local haiku_top = os.projectdir()
    local output_dir = path.join(haiku_top, "generated")

    -- Get configuration
    local defaults = get_mmc_defaults()
    local mmc_image = path.join(defaults.dir, defaults.name)

    -- Get boot platform configuration
    local boot_platform = get_config("boot_platform") or "efi"
    local target_arch = get_config("arch") or "x86_64"

    -- Haiku disk image (built by HaikuImage)
    local haiku_image_name = get_config("image_name") or "haiku.image"
    local haiku_image = path.join(output_dir, haiku_image_name)

    -- Loader name
    local loader = string.format("haiku_loader.%s", boot_platform)

    local files = {
        os_image = haiku_image,
        loader = loader,
        extras = {},
    }

    if boot_platform == "efi" then
        -- EFI platforms
        if target_arch == "arm" or target_arch == "riscv64" then
            -- These EFI platforms need u-boot to get them going
            files.extras = {"boot.scr"}
        end
        return build_efi_sd_image(mmc_image, files)

    elseif boot_platform == "u-boot" then
        -- U-Boot platforms
        files.extras = {"haiku-floppyboot.tgz." .. boot_platform, "boot.scr"}
        return build_uboot_sd_image(mmc_image, files)

    else
        -- Generic platform
        files.extras = {"boot.scr"}
        print("Warning: Unknown boot platform for MMC image: " .. boot_platform)
        return build_efi_sd_image(mmc_image, files)
    end
end

-- ============================================================================
-- xmake Target
-- ============================================================================

target("haiku-mmc-image")
    set_kind("phony")
    add_deps("haiku-image")

    on_build(function (target)
        print("Building MMC/SD card image...")
        local mmc = BuildMMCImageTarget()
        if mmc then
            print("MMC image built: " .. mmc.output)
        end
    end)

-- ============================================================================
-- Module Exports
-- ============================================================================

return {
    get_mmc_defaults = get_mmc_defaults,
    get_efi_boot_name = get_efi_boot_name,
    build_uboot_sd_image = build_uboot_sd_image,
    build_efi_sd_image = build_efi_sd_image,
    BuildMMCImageTarget = BuildMMCImageTarget,
}