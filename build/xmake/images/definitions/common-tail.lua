-- Common tail for Haiku image definitions
-- Ported from build/jam/images/definitions/common-tail
--
-- This file adds content common to all images, it needs to be included after
-- all the other definitions.

-- NOTE: xmake only exports FUNCTIONS from modules.
-- Module-level imports (local or global) are NOT accessible when functions
-- are called via import() from other modules.
-- Each function must import its dependencies internally.

-- ============================================================================
-- Directory Structure
-- ============================================================================

function SetupCommonDirectories()
    local ImageRules = import("rules.ImageRules")
    -- Create directories that may remain empty

    -- Home directories
    ImageRules.AddDirectoryToHaikuImage({"home"}, "home.rdef")
    ImageRules.AddDirectoryToHaikuImage({"home", "Desktop"}, "home-desktop.rdef")
    ImageRules.AddDirectoryToHaikuImage({"home", "mail"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config"}, "home-config.rdef")
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "cache"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "packages"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "settings"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "var"})

    -- Home non-packaged directories
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "bin"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "data", "fonts"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "lib"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "control_look"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "decorators"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "opengl"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "kernel", "drivers", "bin"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "kernel", "drivers", "dev"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "input_server", "devices"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "input_server", "filters"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "input_server", "methods"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "media", "plugins"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "Tracker"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "Print"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "Screen Savers"})
    ImageRules.AddDirectoryToHaikuImage({"home", "config", "non-packaged", "add-ons", "Translators"})

    -- System directories
    ImageRules.AddDirectoryToHaikuImage({"system"}, "system.rdef")
    ImageRules.AddDirectoryToHaikuImage({"system", "cache", "tmp"})

    -- System non-packaged directories
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "bin"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "data", "fonts"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "lib"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "control_look"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "decorators"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "opengl"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "kernel", "drivers", "bin"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "kernel", "drivers", "dev"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "input_server", "devices"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "input_server", "filters"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "input_server", "methods"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "media", "plugins"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "Tracker"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "Print"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "Screen Savers"})
    ImageRules.AddDirectoryToHaikuImage({"system", "non-packaged", "add-ons", "Translators"})

    -- System var directories
    ImageRules.AddDirectoryToHaikuImage({"system", "var", "empty"})
    ImageRules.AddDirectoryToHaikuImage({"system", "var", "log"})

    -- Trash
    ImageRules.AddDirectoryToHaikuImage({"trash"}, "trash.rdef")
end

-- ============================================================================
-- Optional Packages
-- ============================================================================

function IncludeOptionalPackages()
    -- Import OptionalPackages configuration
    -- This would include the contents of OptionalPackages
    -- Use {try = true} instead of pcall - xmake pattern
    local optional = import("OptionalPackages", {try = true})
    if optional then
        -- Optional packages are included
    end
end

-- ============================================================================
-- User/Group Setup
-- ============================================================================

function SetupUsersAndGroups()
    local ImageRules = import("rules.ImageRules")
    -- Add the root user and the root and users groups
    local root_user_name = get_config("root_user_name") or "baron"
    local root_user_real_name = get_config("root_user_real_name") or "Root User"

    ImageRules.AddUserToHaikuImage(root_user_name, 0, 0, "/boot/home", "/bin/bash", root_user_real_name)
    ImageRules.AddGroupToHaikuImage("root", 0, {})
    ImageRules.AddGroupToHaikuImage("users", 100, {})
end

-- ============================================================================
-- Host Name Setup
-- ============================================================================

function SetupHostname()
    local ImageRules = import("rules.ImageRules")
    local config = import("core.project.config")
    local hostname = get_config("image_host_name")
    if hostname then
        -- Build hostname file
        local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
        local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")
        local hostname_file = path.join(output_dir, "objects", "hostname")

        -- Create the hostname file
        io.writefile(hostname_file, hostname .. "\n")

        -- Add to image
        ImageRules.AddFilesToHaikuImage({"system", "settings", "network"}, {hostname_file}, "hostname")
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
    local config = import("core.project.config")
    -- haiku_top is the source root (/home/ruslan/haiku)
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    -- output_dir is the build output (/home/ruslan/haiku/spawned)
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")
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
        "# Build tools (built tools are in output_dir/tools)",
        string.format('addattr="%s/tools/addattr"', output_dir),
        string.format('bfsShell="%s/tools/bfs_shell"', output_dir),
        string.format('fsShellCommand="%s/tools/fs_shell_command"', output_dir),
        string.format('copyattr="%s/tools/copyattr"', output_dir),
        string.format('createImage="%s/tools/create_image"', output_dir),
        string.format('makebootable="%s/tools/makebootable"', output_dir),
        string.format('rc="%s/tools/rc"', output_dir),
        string.format('resattr="%s/tools/resattr"', output_dir),
        string.format('unzip="unzip"'),
        string.format('vmdkimage="%s/tools/vmdkimage"', output_dir),
        'rmAttrs="rm"',
        "",
    }

    return table.concat(lines, "\n")
end

-- ============================================================================
-- Build The Image
-- ============================================================================

function BuildImage(image_type)
    local ImageRules = import("rules.ImageRules")
    local config = import("core.project.config")

    -- Execute pre-image user config rules
    UserBuildConfigRulePreImage()

    -- Get image configuration
    local defaults = GetImageDefaults()
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")

    -- Haiku image target
    local haiku_image = path.join(output_dir, defaults.name)

    -- Create init script
    local init_script = path.join(output_dir, "haiku.image-init-vars")
    local script_content = CreateImageInitScript(init_script)
    io.writefile(init_script, script_content)

    -- Create directory/file scripts
    ImageRules.CreateHaikuImageMakeDirectoriesScript(path.join(output_dir, "haiku.image-make-dirs"))
    ImageRules.CreateHaikuImageCopyFilesScript(path.join(output_dir, "haiku.image-copy-files"))
    ImageRules.CreateHaikuImageExtractFilesScript(path.join(output_dir, "haiku.image-extract-files"))

    -- Build the image
    local scripts = {
        init_script,
        path.join(output_dir, "haiku.image-make-dirs"),
        path.join(output_dir, "haiku.image-copy-files"),
        path.join(output_dir, "haiku.image-extract-files"),
    }

    ImageRules.BuildHaikuImage(haiku_image, scripts, true, false)

    -- Execute post-image user config rules
    UserBuildConfigRulePostImage()

    return haiku_image
end

function BuildVMwareImage()
    local ImageRules = import("rules.ImageRules")
    local config = import("core.project.config")

    -- VMware image target
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")
    local vmware_name = get_config("vmware_image_name") or "haiku.vmdk"
    local vmware_image = path.join(output_dir, vmware_name)

    -- Build as VMware image
    local defaults = GetImageDefaults()
    local haiku_image = path.join(output_dir, defaults.name)

    ImageRules.BuildHaikuImage(vmware_image, {}, true, true)

    return vmware_image
end

-- ============================================================================
-- Package List Generation
-- ============================================================================

function BuildPackageList(target_file)
    local ImageRules = import("rules.ImageRules")
    -- Generate list of packages in the image
    ImageRules.BuildHaikuImagePackageList(target_file)
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
-- Module Exports (xmake exports global functions automatically)
-- ============================================================================
-- All functions above are global and automatically exported by xmake.
-- No return statement needed.