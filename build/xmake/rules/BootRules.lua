--[[
    BootRules.lua - Haiku bootloader build rules

    xmake equivalent of build/jam/BootRules (1:1 migration)

    Rules defined:
    - SetupBoot          - Configure boot-specific compiler/linker flags on a target
    - BootObjects        - Compile objects with boot flags (rule)
    - BootLd             - Link bootloader binary with special LD flags (rule)
    - BootMergeObject    - Merge boot objects into single .o (rule)
    - BootStaticLibrary  - Build boot static library (rule)
    - BootStaticLibraryObjects - Build static library from pre-compiled boot objects (rule)
    - BuildMBR           - Build MBR binary with raw binary output (rule)

    Utility functions:
    - MultiBootSubDirSetup(boot_targets) - Create per-boot-platform build contexts
    - MultiBootGristFiles(files, platform) - Add boot platform prefix to file names

    Boot code uses special compiler flags (no standard libs, freestanding environment)
    and links with custom linker scripts.
]]

-- Boot settings per platform, populated by ArchitectureRules/BuildSetup
local _boot_settings = {}

-- SetBootSettings: store boot settings for a platform
function SetBootSettings(platform, settings)
    _boot_settings[platform] = settings
end

-- GetBootSettings: retrieve boot settings
function GetBootSettings(platform)
    return _boot_settings[platform] or {}
end

-- MultiBootSubDirSetup: create build context objects for each boot platform
-- Returns a list of context tables, each containing boot target configuration
function MultiBootSubDirSetup(boot_targets)
    local results = {}
    for _, boot_target in ipairs(boot_targets) do
        local ctx = {
            platform = boot_target,
            source_grist = boot_target,
            hdr_grist = boot_target,
        }
        table.insert(results, ctx)
    end
    return results
end

-- MultiBootGristFiles: prefix file names with boot platform
function MultiBootGristFiles(files, platform)
    local result = {}
    for _, f in ipairs(files) do
        table.insert(result, platform .. "_" .. f)
    end
    return result
end

-- SetupBootTarget: apply boot compiler/linker flags to an xmake target
-- This is the core function called from on_load callbacks
function SetupBootTarget(target, extra_ccflags, include_private_headers)
    import("core.project.config")

    local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
    local platform = target:values("boot.platform")
    local platform_upper = platform and platform:upper() or ""

    -- Boot compiler flags
    local boot_ccflags = config.get("boot_ccflags") or {}
    local boot_cxxflags = config.get("boot_cxxflags") or {}
    local boot_defines = config.get("target_kernel_defines") or {}
    local boot_platform_ccflags = _boot_settings[platform] and _boot_settings[platform].ccflags or {}
    local boot_platform_cxxflags = _boot_settings[platform] and _boot_settings[platform].cxxflags or {}
    local boot_target_defines = config.get("target_boot_defines") or {}

    -- Apply flags
    if type(boot_ccflags) == "table" then
        target:add("cflags", table.unpack(boot_ccflags))
    end
    if type(boot_platform_ccflags) == "table" then
        target:add("cflags", table.unpack(boot_platform_ccflags))
    end
    if type(boot_cxxflags) == "table" then
        target:add("cxxflags", table.unpack(boot_cxxflags))
    end
    if type(boot_platform_cxxflags) == "table" then
        target:add("cxxflags", table.unpack(boot_platform_cxxflags))
    end

    -- Extra caller-provided flags
    if extra_ccflags then
        if type(extra_ccflags) == "table" then
            target:add("cflags", table.unpack(extra_ccflags))
            target:add("cxxflags", table.unpack(extra_ccflags))
        else
            target:add("cflags", extra_ccflags)
            target:add("cxxflags", extra_ccflags)
        end
    end

    -- Defines
    if type(boot_defines) == "table" then
        target:add("defines", table.unpack(boot_defines))
    end
    if type(boot_target_defines) == "table" then
        target:add("defines", table.unpack(boot_target_defines))
    end

    -- Private kernel headers (opt-out with include_private_headers=false)
    if include_private_headers ~= false then
        local private_headers = config.get("private_kernel_headers")
        if private_headers then
            if type(private_headers) == "table" then
                target:add("includedirs", table.unpack(private_headers))
            else
                target:add("includedirs", private_headers)
            end
        end
    end

    -- Boot C++ headers
    local boot_cxx_headers = config.get("boot_cxx_headers_dir_" .. arch)
    if boot_cxx_headers then
        target:add("includedirs", boot_cxx_headers)
    end

    -- Override optimization
    local boot_optim = config.get("boot_optim")
    if boot_optim then
        target:add("cflags", boot_optim)
        target:add("cxxflags", boot_optim)
    end

    -- Set kernel arch as packaging arch
    target:data_set("haiku.packaging_arch", arch)
end

-- Dual-load guard
if rule then

-- BootObjects: compile objects with boot flags
rule("BootObjects")

    on_load(function(target)
        target:set("kind", "object")
        local extra = target:values("boot.extra_ccflags")
        SetupBootTarget(target, extra)
    end)

rule_end()


-- BootLd: link bootloader binary with LD
rule("BootLd")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "binary")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        local platform = target:values("boot.platform") or ""
        local platform_upper = platform and platform:upper() or ""

        -- Use LD instead of CC for linking
        local ld = config.get("target_ld_" .. arch)
        if ld then
            target:data_set("haiku.linker", ld)
        end

        -- Boot LD flags
        local ldflags = _boot_settings[platform] and _boot_settings[platform].ldflags or {}
        if type(ldflags) == "table" then
            target:add("ldflags", table.unpack(ldflags))
        end

        -- Linker script
        local linker_script = target:values("boot.linker_script")
        if linker_script then
            target:add("ldflags", "--script=" .. linker_script)
        end

        -- Extra linker args
        local extra_ldflags = target:values("boot.extra_ldflags")
        if extra_ldflags then
            if type(extra_ldflags) == "table" then
                target:add("ldflags", table.unpack(extra_ldflags))
            else
                target:add("ldflags", extra_ldflags)
            end
        end

        -- Link against libgcc; optionally libsupc++
        local no_libsupcxx = target:values("boot.no_libsupc++")
        if not no_libsupcxx then
            local libsupcxx = config.get("boot_libsupc++_" .. arch)
            if libsupcxx then
                target:add("ldflags", libsupcxx)
            end
        end
        local libgcc = config.get("boot_libgcc_" .. arch)
        if libgcc then
            target:add("ldflags", libgcc)
        end

        -- Apply boot compilation flags to objects
        local extra = target:values("boot.extra_ccflags")
        SetupBootTarget(target, extra)
    end)

    on_link(function(target)
        import("core.project.depend")

        local ld = target:data("haiku.linker") or "ld"
        local objects = target:objectfiles()
        local targetfile = target:targetfile()
        local ldflags = target:get("ldflags") or {}
        local linklibs = target:get("linklibs") or {}

        depend.on_changed(function()
            local args = {}
            for _, flag in ipairs(ldflags) do
                table.insert(args, flag)
            end
            table.insert(args, "-o")
            table.insert(args, targetfile)
            for _, obj in ipairs(objects) do
                table.insert(args, obj)
            end
            for _, lib in ipairs(linklibs) do
                table.insert(args, lib)
            end

            -- Version script if set
            local version_script = target:values("boot.version_script")
            if version_script then
                table.insert(args, "--version-script=" .. version_script)
            end

            os.mkdir(path.directory(targetfile))
            os.vrunv(ld, args)
        end, {
            files = objects,
            dependfile = target:dependfile(targetfile)
        })
    end)

rule_end()


-- BootMergeObject: merge boot objects into a single relocatable object
rule("BootMergeObject")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "object")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        target:data_set("haiku.packaging_arch", arch)

        local extra = target:values("boot.extra_ccflags")
        SetupBootTarget(target, extra)

        -- Add boot platform LDFLAGS for merge
        local platform = target:values("boot.platform") or ""
        local ldflags = _boot_settings[platform] and _boot_settings[platform].ldflags or {}
        if type(ldflags) == "table" then
            target:add("ldflags", table.unpack(ldflags))
        end
    end)

rule_end()


-- BootStaticLibrary: build static library from boot-compiled sources
rule("BootStaticLibrary")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "static")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        target:data_set("haiku.packaging_arch", arch)

        local extra_flags = target:values("boot.extra_ccflags") or {}
        if type(extra_flags) == "string" then extra_flags = {extra_flags} end

        -- Add -fvisibility=hidden unless opted out
        local no_hidden = target:values("boot.no_hidden_visibility")
        if not no_hidden then
            table.insert(extra_flags, "-fvisibility=hidden")
        end

        SetupBootTarget(target, extra_flags, false)
    end)

rule_end()


-- BootStaticLibraryObjects: build static library from pre-compiled objects
rule("BootStaticLibraryObjects")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "static")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        target:data_set("haiku.packaging_arch", arch)

        SetupBootTarget(target)
    end)

    -- Force recreation of archive to avoid stale deps
    before_link(function(target)
        local targetfile = target:targetfile()
        if os.isfile(targetfile) then
            os.rm(targetfile)
        end
    end)

    on_link(function(target)
        import("core.project.depend")

        local ar = target:data("haiku.ar") or "ar"
        local objects = target:objectfiles()
        local targetfile = target:targetfile()

        depend.on_changed(function()
            os.mkdir(path.directory(targetfile))
            os.vrunv(ar, {"-r", targetfile, table.unpack(objects)})
        end, {
            files = objects,
            dependfile = target:dependfile(targetfile)
        })
    end)

rule_end()


-- BuildMBR: build MBR with raw binary output format
rule("BuildMBR")

    on_load(function(target)
        target:set("kind", "binary")
    end)

    on_link(function(target)
        import("core.project.config")
        import("core.project.depend")

        local arch = config.get("arch") or "x86_64"
        local cc = config.get("haiku_cc_" .. arch) or "gcc"
        local linkflags = config.get("haiku_linkflags_" .. arch) or ""
        local mbrflags = target:values("mbr.flags") or {}
        local sources = target:objectfiles()
        local targetfile = target:targetfile()

        depend.on_changed(function()
            os.mkdir(path.directory(targetfile))

            local args = {}
            -- Add platform link flags
            if type(linkflags) == "string" and linkflags ~= "" then
                table.insert(args, linkflags)
            elseif type(linkflags) == "table" then
                for _, f in ipairs(linkflags) do table.insert(args, f) end
            end

            -- Add sources
            for _, src in ipairs(sources) do
                table.insert(args, src)
            end

            -- Output
            table.insert(args, "-o")
            table.insert(args, targetfile)

            -- MBR-specific flags
            if type(mbrflags) == "table" then
                for _, f in ipairs(mbrflags) do table.insert(args, f) end
            end

            -- Fixed MBR link flags
            local fixed_flags = {
                "-nostdlib", "-m32",
                "-Wl,--oformat,binary",
                "-Wl,-z,notext",
                "-Xlinker", "-S",
                "-Xlinker", "-N",
                "-Xlinker", "--entry=start",
                "-Xlinker", "-Ttext=0x600"
            }
            for _, f in ipairs(fixed_flags) do
                table.insert(args, f)
            end

            os.vrunv(cc, args)
        end, {
            files = sources,
            dependfile = target:dependfile(targetfile)
        })
    end)

rule_end()

end -- if rule
