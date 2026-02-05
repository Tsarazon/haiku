--[[
    CDRules.lua - Haiku CD image build rules

    xmake equivalent of build/jam/CDRules (1:1 migration)

    Rules defined:
    - BuildHaikuCD - Build a Haiku CD image from boot floppy and scripts

    The rule runs the main build_haiku_image script with environment
    variables cdImagePath and cdBootFloppy, followed by additional
    init/user scripts rooted at the output directory.

    Usage:
        -- In a target that produces a CD image:
        add_rules("BuildHaikuCD")
        set_values("cd.boot_floppy", bootFloppyPath)  -- optional, empty for EFI-only
        set_values("cd.scripts", {script1, script2, ...})
]]

-- Dual-load guard: define rules only when loaded via includes() (description scope)
if rule then

rule("BuildHaikuCD")

    on_build(function(target)
        import("core.project.config")
        import("core.project.depend")

        local haiku_top = config.get("haiku_top") or os.projectdir()
        local output_dir = config.get("output_dir") or target:targetdir()

        local cd_image_path = target:targetfile()
        local boot_floppy = target:values("cd.boot_floppy") or ""
        local scripts = target:values("cd.scripts") or {}

        -- Main script: build/scripts/build_haiku_image
        local main_script = path.join(haiku_top, "build", "scripts", "build_haiku_image")

        -- Collect all dependency files for incremental build
        local depfiles = {main_script}
        for _, s in ipairs(scripts) do
            table.insert(depfiles, s)
        end
        if boot_floppy ~= "" then
            table.insert(depfiles, boot_floppy)
        end

        depend.on_changed(function()
            -- Build the argument list: main_script followed by remaining scripts
            -- rooted at the absolute output directory
            local args = {}
            for _, s in ipairs(scripts) do
                table.insert(args, s)
            end

            -- Run with environment variables matching Jam actions
            os.execv(main_script, args, {
                envs = {
                    cdImagePath = cd_image_path,
                    cdBootFloppy = boot_floppy
                }
            })
        end, {
            files = depfiles,
            dependfile = target:dependfile(cd_image_path)
        })
    end)

rule_end()

end -- if rule
