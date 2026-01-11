-- Common tail for Haiku image definitions
-- Ported from build/jam/images/definitions/common-tail
--
-- This file adds content common to all images, it needs to be included after
-- all the other definitions.

import("rules.ImageRules")
import("rules.PackageRules")

-- ============================================================================
-- Directory Structure
-- ============================================================================

function SetupCommonDirectories()
    -- Create directories that may remain empty

    -- Home directories
    AddDirectoryToHaikuImage({"home"}, "home.rdef")
    AddDirectoryToHaikuImage({"home", "Desktop"}, "home-desktop.rdef")
    AddDirectoryToHaikuImage({"home", "mail"})
    AddDirectoryToHaikuImage({"home", "config"}, "home-config.rdef")
    AddDirectoryToHaikuImage({"home", "config", "cache"})
    AddDirectoryToHaikuImage({"home", "config", "packages"})
    AddDirectoryToHaikuImage({"home", "config", "settings"})
    AddDirectoryToHaikuImage({"home", "config", "var"})

    -- Home non-packaged directories
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "bin"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "data", "fonts"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "lib"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "control_look"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "decorators"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "opengl"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "kernel", "drivers", "bin"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "kernel", "drivers", "dev"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "input_server", "devices"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "input_server", "filters"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "input_server", "methods"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "media", "plugins"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "Tracker"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "Print"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "Screen Savers"})
    AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "Translators"})

    -- System directories
    AddDirectoryToHaikuImage({"system"}, "system.rdef")
    AddDirectoryToHaikuImage({"system", "cache", "tmp"})

    -- System non-packaged directories
    AddDirectoryToHaikuImage({"system", "non-packaged", "bin"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "data", "fonts"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "lib"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "control_look"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "decorators"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "opengl"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "kernel", "drivers", "bin"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "kernel", "drivers", "dev"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "input_server", "devices"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "input_server", "filters"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "input_server", "methods"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "media", "plugins"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "Tracker"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "Print"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "Screen Savers"})
    AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "Translators"})

    -- System var directories
    AddDirectoryToHaikuImage({"system", "var", "empty"})
    AddDirectoryToHaikuImage({"system", "var", "log"})

    -- Trash
    AddDirectoryToHaikuImage({"trash"}, "trash.rdef")
end

-- ============================================================================
-- Optional Packages
-- ============================================================================

function IncludeOptionalPackages()
    -- Import OptionalPackages configuration
    -- This would include the contents of OptionalPackages
    local ok, optional = pcall(import, "OptionalPackages")
    if ok and optional then
        -- Optional packages are included
    end
end

-- ============================================================================
-- User/Group Setup
-- ============================================================================

function SetupUsersAndGroups()
    -- Add the root user and the root and users groups
    local root_user_name = get_config("root_user_name") or "baron"
    local root_user_real_name = get_config("root_user_real_name") or "Root User"

    AddUserToHaikuImage(root_user_name, 0, 0, "/boot/home", "/bin/bash", root_user_real_name)
    AddGroupToHaikuImage("root", 0, {})
    AddGroupToHaikuImage("users", 100, {})
end

-- ============================================================================
-- Host Name Setup
-- ============================================================================

function SetupHostname()
    local hostname = get_config("image_host_name")
    if hostname then
        -- Build hostname file
        local output_dir = path.join(os.projectdir(), "generated", "objects")
        local hostname_file = path.join(output_dir, "hostname")

        -- Create the hostname file
        io.writefile(hostname_file, hostname .. "\n")

        -- Add to image
        AddFilesToHaikuImage({"system", "settings", "network"}, {hostname_file}, "hostname")
    end
end

-- ============================================================================
-- Image Configuration
-- ============================================================================

-- Default image settings
local IMAGE_DEFAULTS = {
    name = "haiku.image",
    size = 800,  -- MB
    label = "Haiku",
}

function GetImageDefaults()
    return {
        name = get_config("image_name") or IMAGE_DEFAULTS.name,
        size = get_config("image_size") or IMAGE_DEFAULTS.size,
        label = get_config("image_label") or IMAGE_DEFAULTS.label,
    }
end

-- ============================================================================
-- Image Script Generation
-- ============================================================================

function CreateImageInitScript(script_path)
    local haiku_top = os.projectdir()
    local output_dir = path.join(haiku_top, "generated")
    local tmp_dir = path.join(output_dir, "tmp")
    local install_dir = get_config("install_dir") or ""
    local image_defaults = GetImageDefaults()

    local lines = {
        "#!/bin/sh",
        "# Auto-generated image initialization script",
        "",
        string.format('sourceDir="%s"', haiku_top),
        string.format('outputDir="%s"', output_dir),
        string.format('tmpDir="%s"', tmp_dir),
        string.format('installDir="%s"', install_dir),
        string.format('imageSize="%d"', image_defaults.size),
        string.format('imageLabel="%s"', image_defaults.label),
        "",
        "# Build tools",
        string.format('addattr="%s/build/tools/addattr"', haiku_top),
        string.format('bfsShell="%s/generated/tools/bfs_shell"', haiku_top),
        string.format('fsShellCommand="%s/generated/tools/fs_shell_command"', haiku_top),
        string.format('copyattr="%s/build/tools/copyattr"', haiku_top),
        string.format('createImage="%s/generated/tools/create_image"', haiku_top),
        string.format('makebootable="%s/generated/tools/makebootable"', haiku_top),
        string.format('rc="%s/generated/tools/rc"', haiku_top),
        string.format('resattr="%s/generated/tools/resattr"', haiku_top),
        string.format('unzip="unzip"'),
        string.format('vmdkimage="%s/generated/tools/vmdkimage"', haiku_top),
        'rmAttrs="rm"',
        "",
    }

    return table.concat(lines, "\n")
end

-- ============================================================================
-- Build The Image
-- ============================================================================

function BuildImage(image_type)
    -- Execute pre-image user config rules
    UserBuildConfigRulePreImage()

    -- Get image configuration
    local defaults = GetImageDefaults()
    local output_dir = path.join(os.projectdir(), "generated")

    -- Haiku image target
    local haiku_image = path.join(output_dir, defaults.name)

    -- Create init script
    local init_script = path.join(output_dir, "haiku.image-init-vars")
    local script_content = CreateImageInitScript(init_script)
    io.writefile(init_script, script_content)

    -- Create directory/file scripts
    CreateHaikuImageMakeDirectoriesScript(path.join(output_dir, "haiku.image-make-dirs"))
    CreateHaikuImageCopyFilesScript(path.join(output_dir, "haiku.image-copy-files"))
    CreateHaikuImageExtractFilesScript(path.join(output_dir, "haiku.image-extract-files"))

    -- Build the image
    local scripts = {
        init_script,
        path.join(output_dir, "haiku.image-make-dirs"),
        path.join(output_dir, "haiku.image-copy-files"),
        path.join(output_dir, "haiku.image-extract-files"),
    }

    BuildHaikuImage(haiku_image, scripts, true, false)

    -- Execute post-image user config rules
    UserBuildConfigRulePostImage()

    return haiku_image
end

function BuildVMwareImage()
    -- VMware image target
    local output_dir = path.join(os.projectdir(), "generated")
    local vmware_name = get_config("vmware_image_name") or "haiku.vmdk"
    local vmware_image = path.join(output_dir, vmware_name)

    -- Build as VMware image
    local defaults = GetImageDefaults()
    local haiku_image = path.join(output_dir, defaults.name)

    BuildHaikuImage(vmware_image, {}, true, true)

    return vmware_image
end

-- ============================================================================
-- Package List Generation
-- ============================================================================

function BuildPackageList(target_file)
    -- Generate list of packages in the image
    BuildHaikuImagePackageList(target_file)
end

-- ============================================================================
-- User Config Rules (hooks)
-- ============================================================================

function UserBuildConfigRulePreImage()
    -- Hook for user build config before image creation
    local user_config = import("UserBuildConfig", {try = true})
    if user_config and user_config.PreImage then
        user_config.PreImage()
    end
end

function UserBuildConfigRulePostImage()
    -- Hook for user build config after image creation
    local user_config = import("UserBuildConfig", {try = true})
    if user_config and user_config.PostImage then
        user_config.PostImage()
    end
end

-- ============================================================================
-- Main Setup Function
-- ============================================================================

function SetupCommonTail()
    -- Setup common directories
    SetupCommonDirectories()

    -- Include optional packages
    IncludeOptionalPackages()

    -- Setup users and groups
    SetupUsersAndGroups()

    -- Setup hostname if configured
    SetupHostname()
end

-- ============================================================================
-- Module Exports
-- ============================================================================

return {
    SetupCommonDirectories = SetupCommonDirectories,
    IncludeOptionalPackages = IncludeOptionalPackages,
    SetupUsersAndGroups = SetupUsersAndGroups,
    SetupHostname = SetupHostname,
    GetImageDefaults = GetImageDefaults,
    CreateImageInitScript = CreateImageInitScript,
    BuildImage = BuildImage,
    BuildVMwareImage = BuildVMwareImage,
    BuildPackageList = BuildPackageList,
    UserBuildConfigRulePreImage = UserBuildConfigRulePreImage,
    UserBuildConfigRulePostImage = UserBuildConfigRulePostImage,
    SetupCommonTail = SetupCommonTail,
}