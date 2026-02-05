-- HaikuImage.lua - Main Haiku disk image builder
-- Ported from build/jam/images/HaikuImage
--
-- This file defines what ends up on the Haiku image (respectively in the Haiku
-- installation directory) and it executes the rules building the image
-- (respectively installing the files in the installation directory).

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
    local ImageRules = import("rules.ImageRules")

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

    ImageRules.AddPackageFilesToHaikuImage(
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
    local config = import("core.project.config")
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")
    local list_file = path.join(output_dir, "haiku-packages.txt")

    return common_tail.BuildPackageList(list_file)
end

-- ============================================================================
-- xmake Targets
-- ============================================================================

-- Guard: only define targets when included via includes() (description scope)
-- When imported via import() (script scope), target() is not available
if target then

-- Main Haiku image target
target("haiku-image")
    set_kind("phony")

    on_build(function (target)
        import("images.HaikuImage")
        print("Building Haiku disk image...")
        local image = HaikuImage.BuildHaikuImageTarget()
        print("Haiku image built: " .. (image or "unknown"))
    end)

-- VMware image target
target("haiku-vmware-image")
    set_kind("phony")
    add_deps("haiku-image")

    on_build(function (target)
        import("images.HaikuImage")
        print("Building VMware image...")
        local image = HaikuImage.BuildVMwareImageTarget()
        print("VMware image built: " .. (image or "unknown"))
    end)

-- Install to directory target
target("install-haiku")
    set_kind("phony")

    on_build(function (target)
        import("images.HaikuImage")
        print("Installing Haiku to directory...")
        HaikuImage.InstallHaikuTarget()
        print("Haiku installed.")
    end)

-- Package list target
target("haiku-package-list")
    set_kind("phony")

    on_build(function (target)
        import("images.HaikuImage")
        print("Generating package list...")
        HaikuImage.BuildPackageListTarget()
        print("Package list generated.")
    end)

end -- if target

-- ============================================================================
-- Module Exports (xmake exports global functions automatically)
-- ============================================================================
-- All functions above are global and automatically exported by xmake.
-- No return statement needed.