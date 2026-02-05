-- HaikuCD.lua - Haiku CD/ISO image builder
-- Ported from build/jam/images/HaikuCD
--
-- This file builds the Haiku CD/ISO image that can be used for installation.


-- ============================================================================
-- Configuration
-- ============================================================================

-- CD image defaults (can be overridden by config)
local function get_cd_defaults()
    local config = import("core.project.config", {try = true})
    local haiku_top = config and config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local output_dir = config and config.get("haiku_output_dir") or path.join(haiku_top, "spawned")
    return {
        name = get_config("cd_name") or get_config("default_cd_name") or "haiku.iso",
        dir = get_config("cd_dir") or get_config("default_cd_dir") or output_dir,
        label = get_config("cd_label") or get_config("default_cd_label") or "Haiku",
    }
end

-- ============================================================================
-- CD Init Variables Script
-- ============================================================================

local function create_cd_init_script(script_path)
    local config = import("core.project.config")
    -- haiku_top is the source root
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    -- output_dir is the build output
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")
    local tmp_dir = path.join(output_dir, "tmp")
    local cd_defaults = get_cd_defaults()

    local lines = {
        "#!/bin/sh",
        "# Auto-generated CD image initialization script",
        "",
        string.format('sourceDir="%s"', haiku_top),
        string.format('outputDir="%s"', output_dir),
        string.format('tmpDir="%s"', tmp_dir),
        'isCD=1',
        string.format('cdLabel="%s"', cd_defaults.label),
        "",
        "# Build tools (in output directory)",
        string.format('addattr="%s/tools/addattr"', output_dir),
        string.format('copyattr="%s/tools/copyattr"', output_dir),
        string.format('rc="%s/tools/rc"', output_dir),
        string.format('resattr="%s/tools/resattr"', output_dir),
        'unzip="unzip"',
        string.format('generate_attribute_stores="%s/tools/generate_attribute_stores"', output_dir),
        'rmAttrs="rm"',
        "",
    }

    return table.concat(lines, "\n")
end

-- ============================================================================
-- Build Haiku CD
-- ============================================================================

--[[
    _BuildHaikuCD(haiku_cd, boot_file)

    Convenience wrapper rule around BuildHaikuCD.
    boot_file can be empty for EFI-only CDs.
]]
local function _BuildHaikuCD(haiku_cd, boot_file)
    local config = import("core.project.config")
    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")

    -- Prepare scripts
    local init_script = path.join(output_dir, "haiku.cd-init-vars")
    local script_content = create_cd_init_script(init_script)
    io.writefile(init_script, script_content)

    local make_dirs_script = path.join(output_dir, "haiku.image-make-dirs")
    local copy_files_script = path.join(output_dir, "haiku.image-copy-files")
    local extract_files_script = path.join(output_dir, "haiku.image-extract-files")

    -- Create scripts
    local ImageRules = import("rules.ImageRules")
    ImageRules.CreateHaikuImageMakeDirectoriesScript(make_dirs_script)
    ImageRules.CreateHaikuImageCopyFilesScript(copy_files_script)
    ImageRules.CreateHaikuImageExtractFilesScript(extract_files_script)

    -- Collect all scripts
    -- HAIKU_IMAGE_EARLY_USER_SCRIPTS, HAIKU_IMAGE_LATE_USER_SCRIPTS can be
    -- specified by the user
    local early_scripts = get_config("image_early_user_scripts") or {}
    local late_scripts = get_config("image_late_user_scripts") or {}

    local scripts = {init_script}
    for _, s in ipairs(early_scripts) do table.insert(scripts, s) end
    table.insert(scripts, make_dirs_script)
    table.insert(scripts, copy_files_script)
    table.insert(scripts, extract_files_script)
    for _, s in ipairs(late_scripts) do table.insert(scripts, s) end

    -- Build the CD
    ImageRules.BuildHaikuCD(haiku_cd, boot_file, scripts)

    return haiku_cd
end

-- ============================================================================
-- Main Build Function
-- ============================================================================

function BuildHaikuCDTarget()
    -- Execute pre-image user config rules
    UserBuildConfigRulePreImage()

    -- Get CD configuration
    local cd_defaults = get_cd_defaults()
    local haiku_cd = path.join(cd_defaults.dir, cd_defaults.name)

    -- Setup the Haiku image content first
    local haiku_image = import("images.HaikuImage")
    haiku_image.SetupHaikuImage()

    -- Build the CD - no longer needs floppy boot file
    _BuildHaikuCD(haiku_cd, nil)

    -- Execute post-image user config rules
    UserBuildConfigRulePostImage()

    return haiku_cd
end

-- ============================================================================
-- User Config Rules (hooks)
-- ============================================================================

function UserBuildConfigRulePreImage()
    local user_config = import("UserBuildConfig", {try = true})
    if user_config and user_config.PreImage then
        user_config.PreImage()
    end
end

function UserBuildConfigRulePostImage()
    local user_config = import("UserBuildConfig", {try = true})
    if user_config and user_config.PostImage then
        user_config.PostImage()
    end
end

-- ============================================================================
-- xmake Target
-- ============================================================================

if target then

target("haiku-cd")
    set_kind("phony")
    on_build(function (target)
        import("images.HaikuCD")
        print("Building Haiku CD image...")
        local cd_image = HaikuCD.BuildHaikuCDTarget()
        print("Haiku CD image built: " .. (cd_image or "unknown"))
    end)

end -- if target

-- ============================================================================
-- Module Exports
-- ============================================================================

return {
    get_cd_defaults = get_cd_defaults,
    create_cd_init_script = create_cd_init_script,
    BuildHaikuCDTarget = BuildHaikuCDTarget,
    UserBuildConfigRulePreImage = UserBuildConfigRulePreImage,
    UserBuildConfigRulePostImage = UserBuildConfigRulePostImage,
}