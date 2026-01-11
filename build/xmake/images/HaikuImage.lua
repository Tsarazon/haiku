-- HaikuImage.lua - Main Haiku disk image builder
-- Ported from build/jam/images/HaikuImage
--
-- This file defines what ends up on the Haiku image (respectively in the Haiku
-- installation directory) and it executes the rules building the image
-- (respectively installing the files in the installation directory).

-- import("rules.ImageRules")
-- import("rules.PackageRules")

-- ============================================================================
-- Image Type Selection
-- ============================================================================

-- Import the appropriate image definition based on build type
local function get_image_definition()
    local build_type = get_config("build_type") or "regular"

    if build_type == "bootstrap" then
        return import("images.definitions.bootstrap")
    elseif build_type == "minimum" then
        return import("images.definitions.minimum")
    else
        return import("images.definitions.regular")
    end
end

-- ============================================================================
-- Main Image Setup
-- ============================================================================

function SetupHaikuImage()
    -- Get image definition based on build type
    local definition = get_image_definition()

    -- Setup image content from definition
    local build_type = get_config("build_type") or "regular"

    if build_type == "bootstrap" then
        definition.SetupBootstrapImageContent()
    elseif build_type == "minimum" then
        definition.SetupMinimumImageContent()
    else
        definition.SetupRegularImageContent()
    end

    -- Build and add the haiku system packages
    AddSystemPackages()

    -- Import common tail (shared by all images)
    local common_tail = import("images.definitions.common-tail")
    common_tail.SetupCommonTail()
end

-- ============================================================================
-- System Packages
-- ============================================================================

function AddSystemPackages()
    -- Add haiku system packages
    local packages = {
        "haiku_loader.hpkg",
        "haiku.hpkg",
        "haiku_datatranslators.hpkg",
    }

    -- Add secondary architecture packages if configured
    local secondary_archs = get_config("secondary_architectures") or {}
    for _, arch in ipairs(secondary_archs) do
        table.insert(packages, string.format("haiku_%s.hpkg", arch))
    end

    AddPackageFilesToHaikuImage(
        {"system", "packages"},
        packages,
        {nameFromMetaInfo = true}
    )
end

-- ============================================================================
-- Image Building Rules
-- ============================================================================

--[[
    Build the Haiku disk image.

    This creates:
    1. The main haiku.image file
    2. Optionally a VMware .vmdk file

    Targets:
    - haiku-image: The main disk image
    - haiku-vmware-image: VMware compatible image
    - install-haiku: Install to directory (not an image)
]]

function BuildHaikuImageTarget()
    local common_tail = import("images.definitions.common-tail")

    -- Setup the image first
    SetupHaikuImage()

    -- Build the image
    return common_tail.BuildImage("haiku")
end

function BuildVMwareImageTarget()
    local common_tail = import("images.definitions.common-tail")

    -- Setup the image first
    SetupHaikuImage()

    -- Build VMware image
    return common_tail.BuildVMwareImage()
end

function InstallHaikuTarget()
    local common_tail = import("images.definitions.common-tail")

    -- Setup the image first
    SetupHaikuImage()

    -- Build as installation (not an image)
    return common_tail.BuildImage("install")
end

-- ============================================================================
-- Package List
-- ============================================================================

function BuildPackageListTarget()
    local common_tail = import("images.definitions.common-tail")
    local output_dir = path.join(os.projectdir(), "generated")
    local list_file = path.join(output_dir, "haiku-packages.txt")

    return common_tail.BuildPackageList(list_file)
end

-- ============================================================================
-- xmake Targets
-- ============================================================================

-- Main Haiku image target
target("haiku-image")
    set_kind("phony")
    add_rules("HaikuImage")

    on_build(function (target)
        print("Building Haiku disk image...")
        local image = BuildHaikuImageTarget()
        print("Haiku image built: " .. image)
    end)

-- VMware image target
target("haiku-vmware-image")
    set_kind("phony")
    add_rules("VMWareImage")
    add_deps("haiku-image")

    on_build(function (target)
        print("Building VMware image...")
        local image = BuildVMwareImageTarget()
        print("VMware image built: " .. image)
    end)

-- Install to directory target
target("install-haiku")
    set_kind("phony")

    on_build(function (target)
        print("Installing Haiku to directory...")
        InstallHaikuTarget()
        print("Haiku installed.")
    end)

-- Package list target
target("haiku-package-list")
    set_kind("phony")

    on_build(function (target)
        print("Generating package list...")
        BuildPackageListTarget()
        print("Package list generated.")
    end)

-- ============================================================================
-- Module Exports
-- ============================================================================

return {
    SetupHaikuImage = SetupHaikuImage,
    AddSystemPackages = AddSystemPackages,
    BuildHaikuImageTarget = BuildHaikuImageTarget,
    BuildVMwareImageTarget = BuildVMwareImageTarget,
    InstallHaikuTarget = InstallHaikuTarget,
    BuildPackageListTarget = BuildPackageListTarget,
    get_image_definition = get_image_definition,
}