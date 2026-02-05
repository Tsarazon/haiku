--[[
    KernelRules.lua - Haiku kernel build rules

    xmake equivalent of build/jam/KernelRules (1:1 migration)

    Rules defined:
    - KernelObjects             - Compile objects with kernel flags (rule)
    - KernelLd                  - Link kernel binary with LD (rule)
    - KernelSo                  - Convert binary to kernel shared object (rule)
    - KernelAddon               - Build kernel addon/driver (rule)
    - KernelMergeObject         - Merge kernel objects into single .o (rule)
    - KernelStaticLibrary       - Build kernel static library (rule)
    - KernelStaticLibraryObjects - Build static library from pre-compiled kernel objects (rule)

    Utility functions:
    - SetupKernelTarget(target, extra_ccflags, include_private_headers)

    Kernel code uses freestanding compilation (no userspace includes, no standard libs)
    with kernel-specific defines, warning flags, and optimization levels.
]]

-- Apply kernel-specific compiler flags to an xmake target
function SetupKernelTarget(target, extra_ccflags, include_private_headers)
    import("core.project.config")

    local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"

    -- Kernel compiler flags
    local kernel_ccflags = config.get("target_kernel_ccflags") or {}
    local kernel_cxxflags = config.get("target_kernel_cxxflags") or {}
    local kernel_defines = config.get("target_kernel_defines") or {}
    local kernel_warning_ccflags = config.get("target_kernel_warning_ccflags") or {}
    local kernel_warning_cxxflags = config.get("target_kernel_warning_cxxflags") or {}

    -- Apply kernel flags
    if type(kernel_ccflags) == "table" and #kernel_ccflags > 0 then
        target:add("cflags", table.unpack(kernel_ccflags))
    end
    if type(kernel_cxxflags) == "table" and #kernel_cxxflags > 0 then
        target:add("cxxflags", table.unpack(kernel_cxxflags))
    end

    -- Extra caller-provided flags
    if extra_ccflags then
        if type(extra_ccflags) == "table" then
            for _, f in ipairs(extra_ccflags) do
                target:add("cflags", f)
                target:add("cxxflags", f)
            end
        else
            target:add("cflags", extra_ccflags)
            target:add("cxxflags", extra_ccflags)
        end
    end

    -- Kernel defines
    if type(kernel_defines) == "table" and #kernel_defines > 0 then
        target:add("defines", table.unpack(kernel_defines))
    end

    -- Warning flags (override regular warning flags)
    if type(kernel_warning_ccflags) == "table" and #kernel_warning_ccflags > 0 then
        target:add("cflags", table.unpack(kernel_warning_ccflags))
    end
    if type(kernel_warning_cxxflags) == "table" and #kernel_warning_cxxflags > 0 then
        target:add("cxxflags", table.unpack(kernel_warning_cxxflags))
    end

    -- Override regular TARGET_CCFLAGS/C++FLAGS (clear them)
    -- In xmake, we set kernel-specific flags and rely on rule priority

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

    -- Set kernel arch as packaging arch
    target:data_set("haiku.packaging_arch", arch)
end

-- Dual-load guard
if rule then

-- KernelObjects: compile objects with kernel flags
rule("KernelObjects")

    on_load(function(target)
        target:set("kind", "object")
        local extra = target:values("kernel.extra_ccflags")
        SetupKernelTarget(target, extra)
    end)

rule_end()


-- KernelLd: link kernel binary with LD
rule("KernelLd")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "binary")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"

        -- Use LD for linking
        local ld = config.get("target_ld_" .. arch)
        if ld then
            target:data_set("haiku.linker", ld)
        end

        -- Linker script
        local linker_script = target:values("kernel.linker_script")
        if linker_script then
            target:add("ldflags", "--script=" .. linker_script)
        end

        -- Extra linker args
        local extra_ldflags = target:values("kernel.extra_ldflags")
        if extra_ldflags then
            if type(extra_ldflags) == "table" then
                for _, f in ipairs(extra_ldflags) do target:add("ldflags", f) end
            else
                target:add("ldflags", extra_ldflags)
            end
        end

        -- Link against libgcc; optionally libsupc++
        local no_libsupcxx = target:values("kernel.no_libsupc++")
        if not no_libsupcxx then
            local libsupcxx = config.get("kernel_libsupc++")
            if libsupcxx then
                target:add("ldflags", libsupcxx)
            end
        end
        local libgcc = config.get("kernel_libgcc")
        if libgcc then
            target:add("ldflags", libgcc)
        end

        target:data_set("haiku.is_executable", true)

        SetupKernelTarget(target)
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
            local version_script = target:values("kernel.version_script")
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


-- KernelSo: convert binary to kernel shared object
-- Copies with attributes then changes ELF type to DYN
rule("KernelSo")

    on_load(function(target)
        target:set("kind", "binary")
    end)

    after_link(function(target)
        import("core.project.config")
        import("core.project.depend")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        local targetfile = target:targetfile()
        local source = target:values("kernel.so_source")
        if not source then return end

        local copyattr = config.get("build_copyattr") or "copyattr"
        local elfedit = config.get("haiku_elfedit_" .. arch) or "elfedit"

        depend.on_changed(function()
            os.vrunv(copyattr, {"--data", source, targetfile})
            os.vrunv(elfedit, {"--output-type", "dyn", targetfile})
        end, {
            files = {source},
            dependfile = target:dependfile(targetfile .. ".so")
        })
    end)

rule_end()


-- KernelAddon: build a kernel addon (driver, file system, etc.)
rule("KernelAddon")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "shared")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        target:data_set("haiku.packaging_arch", arch)

        -- Platform check
        local platform = target:data("haiku.platform") or "haiku"

        -- Determine kernel.so and glue code
        if platform == "haiku" then
            target:add("links", "kernel")
            local begin_glue = config.get("kernel_addon_begin_glue")
            local end_glue = config.get("kernel_addon_end_glue")
            if begin_glue then
                target:data_set("haiku.link_begin_glue", begin_glue)
            end
            if end_glue then
                local libgcc = config.get("kernel_libgcc")
                target:data_set("haiku.link_end_glue",
                    (libgcc and (libgcc .. " ") or "") .. (end_glue or ""))
            end
        end

        -- Kernel addon link flags
        local soname = target:name()
        target:add("ldflags",
            "-shared", "-nostdlib",
            "-Xlinker", "--no-undefined",
            "-Xlinker", "-soname=" .. soname)

        local addon_linkflags = config.get("target_kernel_addon_linkflags")
        if addon_linkflags then
            if type(addon_linkflags) == "table" then
                for _, f in ipairs(addon_linkflags) do target:add("ldflags", f) end
            else
                target:add("ldflags", addon_linkflags)
            end
        end

        SetupKernelTarget(target, nil, false)
    end)

rule_end()


-- KernelMergeObject: merge kernel objects into a single relocatable object
rule("KernelMergeObject")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "object")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        target:data_set("haiku.packaging_arch", arch)

        local extra = target:values("kernel.extra_ccflags")
        SetupKernelTarget(target, extra)
    end)

rule_end()


-- KernelStaticLibrary: build a static library from kernel-compiled sources
rule("KernelStaticLibrary")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "static")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        target:data_set("haiku.packaging_arch", arch)

        local extra_flags = target:values("kernel.extra_ccflags") or {}
        if type(extra_flags) == "string" then extra_flags = {extra_flags} end

        -- Add -fvisibility=hidden unless opted out
        local no_hidden = target:values("kernel.no_hidden_visibility")
        if not no_hidden then
            table.insert(extra_flags, "-fvisibility=hidden")
        end

        SetupKernelTarget(target, extra_flags, false)
    end)

rule_end()


-- KernelStaticLibraryObjects: build static library from pre-compiled kernel objects
rule("KernelStaticLibraryObjects")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "static")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        target:data_set("haiku.packaging_arch", arch)

        SetupKernelTarget(target)
    end)

    -- Force recreation of archive
    before_link(function(target)
        local targetfile = target:targetfile()
        if os.isfile(targetfile) then
            os.rm(targetfile)
        end
    end)

    on_link(function(target)
        import("core.project.config")
        import("core.project.depend")

        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        local ar = config.get("haiku_ar_" .. arch) or "ar"
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

end -- if rule
