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

function CreateImageInitScript(script_path, options)
    local config = import("core.project.config")
    options = options or {}

    -- haiku_top is the source root (/home/ruslan/haiku)
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    -- output_dir is the build output (/home/ruslan/haiku/spawned)
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")
    local tools_dir = path.join(output_dir, "tools")
    local tmp_dir = path.join(output_dir, "tmp")
    local download_dir = config.get("haiku_download_dir") or path.join(haiku_top, "download")
    local install_dir = get_config("install_dir") or ""
    local image_defaults = GetImageDefaults()

    -- Build type and flags
    local build_type = get_config("haiku_build_type") or "regular"
    local update_only = get_config("haiku_update_only") and "1" or ""
    local dont_clear = get_config("haiku_dont_clear_image") and "1" or ""
    local no_downloads = get_config("haiku_no_downloads") and "1" or ""
    local update_all = get_config("haiku_update_all_packages") and "1" or ""

    -- Resolve dependencies unless bootstrap or update-only
    local resolve_deps = ""
    if build_type ~= "bootstrap" and (not update_only or update_all) then
        resolve_deps = "1"
    end

    -- LD_LIBRARY_PATH for build compatibility
    local compat_lib_dir = config.get("host_build_compatibility_lib_dir") or ""
    local add_lib_dir = ""
    if compat_lib_dir ~= "" then
        add_lib_dir = string.format('export LD_LIBRARY_PATH="%s:$LD_LIBRARY_PATH"', compat_lib_dir)
    end

    local lines = {
        "#!/bin/sh",
        "# Auto-generated image initialization script",
        "# Generated by xmake build system",
        "",
        "# === Paths ===",
        string.format('sourceDir="%s"', haiku_top),
        string.format('outputDir="%s"', output_dir),
        string.format('tmpDir="%s"', tmp_dir),
        string.format('installDir="%s"', install_dir),
        string.format('downloadDir="%s"', download_dir),
        "",
        "# === Image parameters ===",
        string.format('imageSize="%d"', image_defaults.size),
        string.format('imageLabel="%s"', image_defaults.label),
        "",
        "# === Build flags ===",
        string.format('dontClearImage="%s"', dont_clear),
        string.format('updateOnly="%s"', update_only),
        string.format('resolvePackageDependencies="%s"', resolve_deps),
        string.format('noDownloads="%s"', no_downloads),
        string.format('updateAllPackages="%s"', update_all),
        "",
        "# === Build compatibility ===",
        string.format('addBuildCompatibilityLibDir=\'%s\'', add_lib_dir),
        "",
        "# === Build tools ===",
        string.format('addattr="%s/addattr"', tools_dir),
        string.format('bfsShell="%s/bfs_shell"', tools_dir),
        string.format('fsShellCommand="%s/fs_shell_command"', tools_dir),
        string.format('copyattr="%s/copyattr"', tools_dir),
        string.format('createImage="%s/create_image"', tools_dir),
        string.format('makebootable="%s/makebootable"', tools_dir),
        string.format('package="%s/package"', tools_dir),
        string.format('getPackageDependencies="%s/get_package_dependencies"', tools_dir),
        string.format('mimeset="%s/mimeset"', tools_dir),
        string.format('rc="%s/rc"', tools_dir),
        string.format('resattr="%s/resattr"', tools_dir),
        string.format('vmdkimage="%s/vmdkimage"', tools_dir),
        'unzip="unzip"',
        'rmAttrs="rm"',
        "",
    }

    -- Add CD-specific variables if requested
    if options.is_cd then
        table.insert(lines, "# === CD-specific ===")
        table.insert(lines, string.format('generate_attribute_stores="%s/generate_attribute_stores"', tools_dir))
        table.insert(lines, string.format('cdLabel="%s"', options.cd_label or image_defaults.label))
        table.insert(lines, "")
    end

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

    -- Ensure output directory exists
    os.mkdir(output_dir)

    -- Haiku image target
    local haiku_image = path.join(output_dir, defaults.name)

    -- Create init script with base variables
    local init_script = path.join(output_dir, "haiku.image-init-vars")
    local script_content = CreateImageInitScript(init_script)
    io.writefile(init_script, script_content)

    -- Add package and repository variables to init script
    ImageRules.AddPackagesAndRepositoryVariablesToContainerScript(init_script, "haiku-image")

    -- Create directory/file scripts
    local make_dirs_script = path.join(output_dir, "haiku.image-make-dirs")
    local copy_files_script = path.join(output_dir, "haiku.image-copy-files")
    local extract_files_script = path.join(output_dir, "haiku.image-extract-files")

    ImageRules.CreateHaikuImageMakeDirectoriesScript(make_dirs_script)
    ImageRules.CreateHaikuImageCopyFilesScript(copy_files_script)
    ImageRules.CreateHaikuImageExtractFilesScript(extract_files_script)

    -- Build the image
    local scripts = {
        init_script,
        make_dirs_script,
        copy_files_script,
        extract_files_script,
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