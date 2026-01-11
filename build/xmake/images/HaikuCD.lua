-- HaikuCD.lua - Haiku CD/ISO image builder
-- Ported from build/jam/images/HaikuCD
--
-- This file builds the Haiku CD/ISO image that can be used for installation.

-- import("rules.ImageRules")
-- import("rules.CDRules")

-- ============================================================================
-- Configuration
-- ============================================================================

-- CD image defaults (can be overridden by config)
local function get_cd_defaults()
    return {
        name = get_config("cd_name") or get_config("default_cd_name") or "haiku.iso",
        dir = get_config("cd_dir") or get_config("default_cd_dir") or path.join(os.projectdir(), "generated"),
        label = get_config("cd_label") or get_config("default_cd_label") or "Haiku",
    }
end

-- ============================================================================
-- CD Init Variables Script
-- ============================================================================

local function create_cd_init_script(script_path)
    local haiku_top = os.projectdir()
    local output_dir = path.join(haiku_top, "generated")
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
        "# Build tools",
        string.format('addattr="%s/generated/tools/addattr"', haiku_top),
        string.format('copyattr="%s/generated/tools/copyattr"', haiku_top),
        string.format('rc="%s/generated/tools/rc"', haiku_top),
        string.format('resattr="%s/generated/tools/resattr"', haiku_top),
        'unzip="unzip"',
        string.format('generate_attribute_stores="%s/generated/tools/generate_attribute_stores"', haiku_top),
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
    local output_dir = path.join(os.projectdir(), "generated")

    -- Prepare scripts
    local init_script = path.join(output_dir, "haiku.cd-init-vars")
    local script_content = create_cd_init_script(init_script)
    io.writefile(init_script, script_content)

    local make_dirs_script = path.join(output_dir, "haiku.image-make-dirs")
    local copy_files_script = path.join(output_dir, "haiku.image-copy-files")
    local extract_files_script = path.join(output_dir, "haiku.image-extract-files")

    -- Create scripts
    CreateHaikuImageMakeDirectoriesScript(make_dirs_script)
    CreateHaikuImageCopyFilesScript(copy_files_script)
    CreateHaikuImageExtractFilesScript(extract_files_script)

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
    BuildHaikuCD(haiku_cd, boot_file, scripts)

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

target("haiku-cd")
    set_kind("phony")
    -- Note: on_build callbacks run in a sandbox, so we use _G to access global functions
    on_build(function (target)
        print("Building Haiku CD image...")
        -- Access global function via _G since callbacks run in sandbox
        if _G.BuildHaikuCDTarget then
            local cd_image = _G.BuildHaikuCDTarget()
            print("Haiku CD image built: " .. (cd_image or "unknown"))
        else
            print("Warning: BuildHaikuCDTarget not available yet")
        end
    end)

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