--[[
    CDRules.lua - Haiku CD/ISO image creation rules

    xmake equivalent of build/jam/CDRules

    Rules defined:
    - BuildHaikuCD - Create bootable Haiku CD/ISO image

    This rule creates bootable CD images using the build_haiku_image script.
    The CD image boots from a floppy image and contains the full Haiku system.
]]


-- ============================================================================
-- CD Image Creation
-- ============================================================================

--[[
    BuildHaikuCD(haiku_cd, boot_floppy, scripts)

    Creates a bootable Haiku CD/ISO image.

    The rule:
    1. Takes a boot floppy image for BIOS boot
    2. Runs the build_haiku_image script
    3. Produces an ISO image

    Equivalent to Jam:
        rule BuildHaikuCD { }
        actions BuildHaikuCD1 {
            export cdImagePath="$(1)"
            export cdBootFloppy="$(2[1])"
            $(2[2]) $(2[3-])
        }

    Parameters:
        haiku_cd - Output CD/ISO image path
        boot_floppy - Boot floppy image for BIOS boot
        scripts - Build scripts (first is main script, rest are arguments)
]]
function BuildHaikuCD(haiku_cd, boot_floppy, scripts)
    if type(scripts) ~= "table" then
        scripts = {scripts}
    end

    -- Main script is build_haiku_image
    local haiku_top = os.projectdir()
    local main_script = path.join(haiku_top, "build", "scripts", "build_haiku_image")

    -- Output directory
    local output_dir = get_config("buildir") or "$(buildir)"

    -- Prepare script arguments with absolute paths
    local script_args = {}
    for _, script in ipairs(scripts) do
        table.insert(script_args, path.join(output_dir, script))
    end

    print("BuildHaikuCD: %s", haiku_cd)

    -- Set environment and run
    os.setenv("cdImagePath", haiku_cd)
    os.setenv("cdBootFloppy", boot_floppy)

    -- Execute main script with arguments
    local args = {main_script}
    for _, arg in ipairs(script_args) do
        table.insert(args, arg)
    end

    os.execv("/bin/sh", args)
end

-- ============================================================================
-- CD Image Rule
-- ============================================================================

--[[
    HaikuCD rule for xmake targets

    Usage:
        target("haiku-cd")
            add_rules("HaikuCD")
            set_values("boot_floppy", "haiku-boot-floppy")
            set_values("scripts", {"script1.sh", "script2.sh"})
]]
rule("HaikuCD")
    set_extensions(".iso")

    on_build(function (target)
        local output = target:targetfile()
        local boot_floppy = target:values("boot_floppy")
        local scripts = target:values("scripts") or {}

        if not boot_floppy then
            error("HaikuCD: boot_floppy not specified")
        end

        BuildHaikuCD(output, boot_floppy, scripts)
    end)

-- ============================================================================
-- Convenience Function
-- ============================================================================

--[[
    haiku_cd(name, config)

    Convenience function for defining a Haiku CD target.

    Usage:
        haiku_cd("haiku-r1-cd", {
            boot_floppy = "haiku-boot-floppy",
            scripts = {"config1.sh"}
        })
]]
function haiku_cd(name, cfg)
    target(name)
        add_rules("HaikuCD")
        set_kind("phony")

        if cfg.boot_floppy then
            set_values("boot_floppy", cfg.boot_floppy)
            add_deps(cfg.boot_floppy)
        end

        if cfg.scripts then
            set_values("scripts", cfg.scripts)
        end

        -- Output ISO file
        local output_dir = "$(buildir)/$(plat)/$(arch)"
        set_targetdir(output_dir)
        set_filename(name .. ".iso")
end