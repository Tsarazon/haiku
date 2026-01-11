--[[
    Haiku OS Build System - Boot Loader Build Rules

    xmake equivalent of build/jam/BootRules

    These rules are used to build the boot loader components for various
    platforms (BIOS, EFI, U-Boot, etc.). Boot loader code has specific
    requirements similar to kernel code but with platform-specific variations.

    Boot Platforms:
    - bios_ia32: Legacy BIOS boot for x86
    - efi: UEFI boot for x86_64, arm64
    - u-boot: U-Boot for ARM, RISC-V
    - riscv: RISC-V OpenSBI

    Rules provided:
    - BootObjects: Compile sources with boot loader flags
    - BootLd: Link boot loader binary
    - BootMergeObject: Merge multiple objects for boot
    - BootStaticLibrary: Create static library for boot
    - BootStaticLibraryObjects: Create library from pre-compiled objects
    - BuildMBR: Build Master Boot Record (BIOS)

    Helper functions:
    - MultiBootSubDirSetup: Setup for multiple boot targets
    - MultiBootGristFiles: Grist files for boot target
    - SetupBoot: Configure boot compilation settings
]]

-- ============================================================================
-- Configuration Variables
-- ============================================================================

-- Boot targets (platforms)
local HAIKU_BOOT_TARGETS = {
    "bios_ia32",
    "efi",
    "pxe_ia32",
    "u-boot",
    "riscv",
}

-- Current boot platform context (set per-target)
local _boot_platform_context = {}

-- ============================================================================
-- Helper Functions
-- ============================================================================

-- Get Haiku source tree root
local function get_haiku_top()
    return os.projectdir()
end

-- Get kernel architecture (boot uses same arch as kernel)
local function get_kernel_arch()
    local arch = get_config("arch") or "x86_64"
    -- Map packaging arch to kernel arch
    local arch_map = {
        ["x86_64"] = "x86_64",
        ["x86"] = "x86",
        ["arm64"] = "arm64",
        ["arm"] = "arm",
        ["riscv64"] = "riscv64",
    }
    return arch_map[arch] or arch
end

-- Get current boot platform from context or default
local function get_boot_platform()
    if _boot_platform_context.platform then
        return _boot_platform_context.platform
    end
    -- Default based on architecture
    local arch = get_kernel_arch()
    if arch == "x86_64" or arch == "x86" then
        return "bios_ia32"  -- or "efi" for UEFI
    elseif arch == "arm64" then
        return "efi"
    elseif arch == "arm" then
        return "u-boot"
    elseif arch == "riscv64" then
        return "riscv"
    end
    return "efi"
end

-- Set boot platform context
local function set_boot_platform(platform)
    _boot_platform_context.platform = platform
end

-- Get base boot CCFLAGS (common for all platforms)
local function get_boot_ccflags()
    local arch = get_kernel_arch()
    local flags = {
        "-ffreestanding",
        "-fno-builtin",
        "-fno-exceptions",
        "-fno-asynchronous-unwind-tables",
        "-fno-rtti",
    }

    -- Architecture-specific flags
    if arch == "x86_64" then
        table.insert(flags, "-mno-red-zone")
        table.insert(flags, "-mno-mmx")
        table.insert(flags, "-mno-sse")
        table.insert(flags, "-mno-sse2")
    elseif arch == "x86" then
        table.insert(flags, "-march=pentium")
    elseif arch == "arm64" then
        table.insert(flags, "-mgeneral-regs-only")
    elseif arch == "arm" then
        table.insert(flags, "-mfloat-abi=soft")
    elseif arch == "riscv64" then
        table.insert(flags, "-mno-relax")
        table.insert(flags, "-mcmodel=medany")
    end

    return flags
end

-- Get platform-specific boot CCFLAGS
local function get_boot_platform_ccflags(platform)
    platform = platform or get_boot_platform()
    local flags = {}

    if platform == "bios_ia32" then
        -- 32-bit BIOS boot loader
        table.insert(flags, "-m32")
        table.insert(flags, "-march=pentium")
    elseif platform == "efi" then
        -- EFI boot loader
        local arch = get_kernel_arch()
        if arch == "x86_64" then
            table.insert(flags, "-DEFI_FUNCTION_WRAPPER")
            table.insert(flags, "-mno-red-zone")
        elseif arch == "arm64" then
            table.insert(flags, "-fpie")
        end
    elseif platform == "pxe_ia32" then
        -- PXE boot (32-bit)
        table.insert(flags, "-m32")
        table.insert(flags, "-march=pentium")
    elseif platform == "u-boot" then
        -- U-Boot for ARM
        table.insert(flags, "-fpic")
    elseif platform == "riscv" then
        -- RISC-V boot
        table.insert(flags, "-fpic")
    end

    return flags
end

-- Get boot C++ flags
local function get_boot_cxxflags()
    local flags = get_boot_ccflags()
    -- Additional C++ specific flags
    table.insert(flags, "-fno-exceptions")
    table.insert(flags, "-fno-rtti")
    return flags
end

-- Get boot defines
local function get_boot_defines()
    local defines = {
        "_BOOT_MODE",
        "_LOADER_MODE",
    }

    local platform = get_boot_platform()
    if platform == "bios_ia32" or platform == "pxe_ia32" then
        table.insert(defines, "_BOOT_PLATFORM_BIOS")
    elseif platform == "efi" then
        table.insert(defines, "_BOOT_PLATFORM_EFI")
    elseif platform == "u-boot" then
        table.insert(defines, "_BOOT_PLATFORM_U_BOOT")
    elseif platform == "riscv" then
        table.insert(defines, "_BOOT_PLATFORM_RISCV")
    end

    -- Add kernel defines too
    local arch = get_kernel_arch()
    table.insert(defines, "_KERNEL_MODE")
    if arch == "x86_64" then
        table.insert(defines, "__x86_64__")
    elseif arch == "x86" then
        table.insert(defines, "__i386__")
    elseif arch == "arm64" then
        table.insert(defines, "__aarch64__")
    elseif arch == "arm" then
        table.insert(defines, "__arm__")
    elseif arch == "riscv64" then
        table.insert(defines, "__riscv")
        table.insert(defines, "__riscv_xlen=64")
    end

    return defines
end

-- Get boot linker flags
local function get_boot_ldflags(platform)
    platform = platform or get_boot_platform()
    local flags = {
        "-nostdlib",
    }

    if platform == "bios_ia32" then
        table.insert(flags, "-m32")
        -- Note: Actual linking uses linker script
    elseif platform == "efi" then
        local arch = get_kernel_arch()
        table.insert(flags, "-shared")
        table.insert(flags, "-Bsymbolic")
        if arch == "x86_64" then
            table.insert(flags, "-znocombreloc")
        end
    elseif platform == "pxe_ia32" then
        table.insert(flags, "-m32")
    elseif platform == "u-boot" or platform == "riscv" then
        -- Position independent
    end

    return flags
end

-- Get private kernel headers for boot
local function get_private_kernel_headers()
    local haiku_top = get_haiku_top()
    local arch = get_kernel_arch()

    return {
        path.join(haiku_top, "headers", "private", "kernel"),
        path.join(haiku_top, "headers", "private", "kernel", "arch", arch),
        path.join(haiku_top, "headers", "private", "kernel", "boot"),
        path.join(haiku_top, "headers", "private", "kernel", "boot", "platform"),
        path.join(haiku_top, "headers", "private", "system"),
        path.join(haiku_top, "headers", "private", "system", "arch", arch),
        path.join(haiku_top, "headers", "private"),
    }
end

-- Get boot platform-specific headers
local function get_boot_platform_headers(platform)
    platform = platform or get_boot_platform()
    local haiku_top = get_haiku_top()

    local headers = {}

    if platform == "bios_ia32" then
        table.insert(headers, path.join(haiku_top, "headers", "private", "kernel", "boot", "platform", "bios_ia32"))
    elseif platform == "efi" then
        table.insert(headers, path.join(haiku_top, "headers", "private", "kernel", "boot", "platform", "efi"))
        -- EFI headers
        table.insert(headers, path.join(haiku_top, "headers", "private", "kernel", "boot", "platform", "efi", "protocol"))
    elseif platform == "u-boot" then
        table.insert(headers, path.join(haiku_top, "headers", "private", "kernel", "boot", "platform", "u-boot"))
    elseif platform == "riscv" then
        table.insert(headers, path.join(haiku_top, "headers", "private", "kernel", "boot", "platform", "riscv"))
    end

    return headers
end

-- Get boot libgcc path
local function get_boot_libgcc()
    -- This would need to be determined from cross-compiler
    local arch = get_kernel_arch()
    -- Placeholder - actual path depends on toolchain
    return "libgcc.a"
end

-- Get boot libsupc++ path
local function get_boot_libsupc()
    -- Placeholder - actual path depends on toolchain
    return "libsupc++.a"
end

-- ============================================================================
-- MultiBootSubDirSetup - Setup for multiple boot targets
-- ============================================================================

-- Jam equivalent: MultiBootSubDirSetup bootTargets
-- Returns list of boot target context objects for iteration
function MultiBootSubDirSetup(boot_targets)
    boot_targets = boot_targets or HAIKU_BOOT_TARGETS

    local result = {}
    for _, boot_target in ipairs(boot_targets) do
        local target_context = {
            platform = boot_target,
            grist = boot_target,
        }
        table.insert(result, target_context)
    end

    return result
end

-- ============================================================================
-- MultiBootGristFiles - Add boot target grist to files
-- ============================================================================

-- Jam equivalent: MultiBootGristFiles files
function MultiBootGristFiles(files, platform)
    platform = platform or get_boot_platform()

    if type(files) ~= "table" then
        files = {files}
    end

    -- In xmake, we return files with platform prefix for organization
    local result = {}
    for _, file in ipairs(files) do
        -- Add platform context to file path
        table.insert(result, file)
    end

    return result
end

-- ============================================================================
-- SetupBoot - Configure boot compilation settings
-- ============================================================================

-- Apply boot setup to a target
local function ApplyBootSetup(target, extra_flags, include_private_headers, platform)
    platform = platform or get_boot_platform()

    -- Get flags
    local ccflags = get_boot_ccflags()
    local platform_ccflags = get_boot_platform_ccflags(platform)
    local cxxflags = get_boot_cxxflags()
    local defines = get_boot_defines()

    -- Apply base boot flags
    for _, flag in ipairs(ccflags) do
        target:add("cxflags", flag, {force = true})
    end

    -- Apply platform-specific flags
    for _, flag in ipairs(platform_ccflags) do
        target:add("cxflags", flag, {force = true})
    end

    -- Extra flags from parameter
    if extra_flags then
        if type(extra_flags) == "string" then
            target:add("cxflags", extra_flags, {force = true})
        elseif type(extra_flags) == "table" then
            for _, flag in ipairs(extra_flags) do
                target:add("cxflags", flag, {force = true})
            end
        end
    end

    -- Defines
    for _, define in ipairs(defines) do
        target:add("defines", define)
    end

    -- Headers
    if include_private_headers ~= false then
        local headers = get_private_kernel_headers()
        for _, header in ipairs(headers) do
            target:add("includedirs", header, {public = false})
        end
    end

    -- Platform-specific headers
    local platform_headers = get_boot_platform_headers(platform)
    for _, header in ipairs(platform_headers) do
        target:add("includedirs", header, {public = false})
    end

    -- Optimization for boot loader
    target:set("optimize", "smallest")  -- -Os equivalent
end

-- ============================================================================
-- BootObjects Rule - Compile sources with boot loader flags
-- ============================================================================

-- Jam equivalent: BootObjects <sources> : <extra_cc_flags>
rule("BootObjects")
    set_extensions(".c", ".cpp", ".S", ".s", ".asm")

    on_load(function (target)
        -- Get boot platform from target values
        local platform = target:values("boot_platform") or get_boot_platform()
        set_boot_platform(platform)

        -- Get extra flags
        local extra_flags = target:values("extra_ccflags")
        local include_private_headers = target:values("include_private_headers")

        ApplyBootSetup(target, extra_flags, include_private_headers, platform)
    end)

    -- Use standard compilation
    on_build_file(function (target, sourcefile, opt)
        import("core.tool.compiler")
        local objectfile = target:objectfile(sourcefile)
        os.mkdir(path.directory(objectfile))
        compiler.compile(sourcefile, objectfile, {target = target})
    end)

-- ============================================================================
-- BootLd Rule - Link boot loader binary
-- ============================================================================

-- Jam equivalent: BootLd <name> : <objs> : <linkerscript> : <args>
rule("BootLd")
    on_load(function (target)
        -- Set as binary output
        target:set("kind", "binary")

        -- Get boot platform
        local platform = target:values("boot_platform") or get_boot_platform()
        set_boot_platform(platform)

        -- Apply boot setup
        ApplyBootSetup(target, nil, true, platform)

        -- Get linker script
        local linker_script = target:values("linker_script")
        if linker_script then
            target:add("ldflags", "-T" .. linker_script, {force = true})
        end

        -- Apply boot ldflags
        local ldflags = get_boot_ldflags(platform)
        for _, flag in ipairs(ldflags) do
            target:add("ldflags", flag, {force = true})
        end

        -- Additional linker args
        local extra_ldflags = target:values("extra_ldflags")
        if extra_ldflags then
            if type(extra_ldflags) == "table" then
                for _, flag in ipairs(extra_ldflags) do
                    target:add("ldflags", flag, {force = true})
                end
            else
                target:add("ldflags", extra_ldflags, {force = true})
            end
        end

        -- Version script
        local version_script = target:values("version_script")
        if version_script then
            target:add("ldflags", "--version-script=" .. version_script, {force = true})
        end

        -- Link against libgcc (always)
        target:add("links", get_boot_libgcc())

        -- Link against libsupc++ unless disabled
        if not target:values("no_libsupc") then
            target:add("links", get_boot_libsupc())
        end
    end)

    on_link(function (target)
        import("core.tool.linker")
        local targetfile = target:targetfile()
        local objectfiles = target:objectfiles()
        os.mkdir(path.directory(targetfile))
        linker.link("binary", objectfiles, targetfile, {target = target})
    end)

-- ============================================================================
-- BootMergeObject Rule - Merge multiple objects for boot
-- ============================================================================

-- Jam equivalent: BootMergeObject <name> : <sources> : <extra CFLAGS> : <other objects>
rule("BootMergeObject")
    on_load(function (target)
        -- Set as object output (merged)
        target:set("kind", "object")

        -- Get boot platform
        local platform = target:values("boot_platform") or get_boot_platform()
        set_boot_platform(platform)

        -- Get extra flags
        local extra_flags = target:values("extra_ccflags")

        ApplyBootSetup(target, extra_flags, true, platform)

        -- Add ldflags for merged object
        local ldflags = get_boot_ldflags(platform)
        for _, flag in ipairs(ldflags) do
            target:add("ldflags", flag, {force = true})
        end
    end)

    on_link(function (target)
        -- Merge objects using ld -r
        local targetfile = target:targetfile()
        local objectfiles = target:objectfiles()

        -- Get other objects to merge
        local other_objects = target:values("other_objects") or {}
        if type(other_objects) == "string" then
            other_objects = {other_objects}
        end

        -- Combine all objects
        local all_objects = {}
        for _, obj in ipairs(objectfiles) do
            table.insert(all_objects, obj)
        end
        for _, obj in ipairs(other_objects) do
            table.insert(all_objects, obj)
        end

        os.mkdir(path.directory(targetfile))

        -- Use ld -r to create relocatable object
        local arch = get_kernel_arch()
        local ld = "ld"
        if arch ~= os.arch() then
            ld = arch .. "-unknown-haiku-ld"
        end

        local args = {"-r", "-o", targetfile}
        for _, obj in ipairs(all_objects) do
            table.insert(args, obj)
        end

        os.runv(ld, args)
    end)

-- ============================================================================
-- BootStaticLibrary Rule - Create static library for boot
-- ============================================================================

-- Jam equivalent: BootStaticLibrary <name> : <sources> : <extra cc flags>
rule("BootStaticLibrary")
    on_load(function (target)
        -- Set as static library
        target:set("kind", "static")

        -- Get boot platform
        local platform = target:values("boot_platform") or get_boot_platform()
        set_boot_platform(platform)

        -- Get extra flags
        local extra_flags = target:values("extra_ccflags") or {}
        if type(extra_flags) == "string" then
            extra_flags = {extra_flags}
        end

        -- Add -fvisibility=hidden unless disabled
        if not target:values("no_hidden_visibility") then
            table.insert(extra_flags, "-fvisibility=hidden")
        end

        ApplyBootSetup(target, extra_flags, false, platform)
    end)

-- ============================================================================
-- BootStaticLibraryObjects Rule - Create library from pre-compiled objects
-- ============================================================================

-- Jam equivalent: BootStaticLibraryObjects <name> : <objects>
rule("BootStaticLibraryObjects")
    on_load(function (target)
        -- Set as static library
        target:set("kind", "static")

        -- Get boot platform
        local platform = target:values("boot_platform") or get_boot_platform()
        set_boot_platform(platform)

        ApplyBootSetup(target, nil, true, platform)
    end)

    on_link(function (target)
        local targetfile = target:targetfile()
        local objectfiles = target:objectfiles()

        -- Get pre-compiled objects
        local precompiled = target:values("precompiled_objects") or {}
        if type(precompiled) == "string" then
            precompiled = {precompiled}
        end

        -- Combine all objects
        local all_objects = {}
        for _, obj in ipairs(objectfiles) do
            table.insert(all_objects, obj)
        end
        for _, obj in ipairs(precompiled) do
            table.insert(all_objects, obj)
        end

        os.mkdir(path.directory(targetfile))

        -- Force recreation of archive
        os.rm(targetfile)

        -- Use ar to create static library
        local arch = get_kernel_arch()
        local ar = "ar"
        if arch ~= os.arch() then
            ar = arch .. "-unknown-haiku-ar"
        end

        local args = {"-r", targetfile}
        for _, obj in ipairs(all_objects) do
            table.insert(args, obj)
        end

        os.runv(ar, args)
    end)

-- ============================================================================
-- BuildMBR Rule - Build Master Boot Record
-- ============================================================================

-- Jam equivalent: BuildMBR binary : source
-- MBR is built as raw binary with specific entry point and text address
rule("BuildMBR")
    set_extensions(".S", ".s", ".asm")

    on_load(function (target)
        -- MBR is a special binary
        target:set("kind", "binary")

        -- MBR-specific flags
        target:add("cxflags", "-m32", {force = true})
        target:add("cxflags", "-nostdlib", {force = true})

        -- Linker flags for raw binary output
        target:add("ldflags", "-nostdlib", {force = true})
        target:add("ldflags", "-m32", {force = true})
        target:add("ldflags", "-Wl,--oformat,binary", {force = true})
        target:add("ldflags", "-Wl,-z,notext", {force = true})
        target:add("ldflags", "-Xlinker", "-S", {force = true})
        target:add("ldflags", "-Xlinker", "-N", {force = true})
        target:add("ldflags", "-Xlinker", "--entry=start", {force = true})
        target:add("ldflags", "-Xlinker", "-Ttext=0x600", {force = true})

        -- Additional MBR flags from target
        local mbrflags = target:values("mbrflags")
        if mbrflags then
            if type(mbrflags) == "table" then
                for _, flag in ipairs(mbrflags) do
                    target:add("ldflags", flag, {force = true})
                end
            else
                target:add("ldflags", mbrflags, {force = true})
            end
        end
    end)

-- ============================================================================
-- Convenience Functions for Jamfile Translation
-- ============================================================================

-- Create boot objects target
function boot_objects(name, config)
    target(name)
        add_rules("BootObjects")
        if config.sources then
            add_files(config.sources)
        end
        if config.platform then
            set_values("boot_platform", config.platform)
        end
        if config.extra_ccflags then
            set_values("extra_ccflags", config.extra_ccflags)
        end
end

-- Create boot ld target
function boot_ld(name, config)
    target(name)
        add_rules("BootLd")
        if config.sources then
            add_files(config.sources)
        end
        if config.objects then
            add_deps(config.objects)
        end
        if config.platform then
            set_values("boot_platform", config.platform)
        end
        if config.linker_script then
            set_values("linker_script", config.linker_script)
        end
        if config.extra_ldflags then
            set_values("extra_ldflags", config.extra_ldflags)
        end
        if config.version_script then
            set_values("version_script", config.version_script)
        end
        if config.no_libsupc then
            set_values("no_libsupc", true)
        end
end

-- Create boot merge object target
function boot_merge_object(name, config)
    target(name)
        add_rules("BootMergeObject")
        if config.sources then
            add_files(config.sources)
        end
        if config.platform then
            set_values("boot_platform", config.platform)
        end
        if config.extra_ccflags then
            set_values("extra_ccflags", config.extra_ccflags)
        end
        if config.other_objects then
            set_values("other_objects", config.other_objects)
        end
end

-- Create boot static library target
function boot_static_library(name, config)
    target(name)
        add_rules("BootStaticLibrary")
        if config.sources then
            add_files(config.sources)
        end
        if config.platform then
            set_values("boot_platform", config.platform)
        end
        if config.extra_ccflags then
            set_values("extra_ccflags", config.extra_ccflags)
        end
        if config.no_hidden_visibility then
            set_values("no_hidden_visibility", true)
        end
end

-- Create boot static library from objects
function boot_static_library_objects(name, config)
    target(name)
        add_rules("BootStaticLibraryObjects")
        if config.objects then
            set_values("precompiled_objects", config.objects)
        end
        if config.platform then
            set_values("boot_platform", config.platform)
        end
end

-- Create MBR target
function build_mbr(name, config)
    target(name)
        add_rules("BuildMBR")
        if config.source then
            add_files(config.source)
        end
        if config.mbrflags then
            set_values("mbrflags", config.mbrflags)
        end
end
