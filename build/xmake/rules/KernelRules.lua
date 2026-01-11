--[[
    KernelRules.lua - Haiku kernel build rules

    xmake equivalent of build/jam/KernelRules

    Rules defined:
    - SetupKernel            - Configure kernel compilation flags
    - KernelObjects          - Compile kernel object files
    - KernelLd               - Link kernel executable
    - KernelSo               - Create kernel shared object
    - KernelAddon            - Create kernel addon (driver)
    - KernelMergeObject      - Merge kernel objects
    - KernelStaticLibrary    - Create kernel static library
    - KernelStaticLibraryObjects - Create static library from objects

    Kernel-specific variables:
    - TARGET_KERNEL_ARCH      - Kernel target architecture
    - TARGET_KERNEL_CCFLAGS   - Kernel C compiler flags
    - TARGET_KERNEL_C++FLAGS  - Kernel C++ compiler flags
    - TARGET_KERNEL_DEFINES   - Kernel preprocessor defines
    - TARGET_KERNEL_WARNING_CCFLAGS   - Kernel warning flags for C
    - TARGET_KERNEL_WARNING_C++FLAGS  - Kernel warning flags for C++
    - TARGET_KERNEL_ADDON_LINKFLAGS   - Kernel addon linker flags
    - HAIKU_KERNEL_ADDON_BEGIN_GLUE_CODE - Start glue code for addons
    - HAIKU_KERNEL_ADDON_END_GLUE_CODE   - End glue code for addons

    Kernel compilation differs from userland:
    - No standard library (-nostdlib)
    - Freestanding environment (-ffreestanding)
    - No exceptions in kernel code (-fno-exceptions)
    - No RTTI (-fno-rtti)
    - Red zone disabled on x86_64 (-mno-red-zone)
    - Kernel-specific include paths
    - Links against libgcc.a and optionally libsupc++
]]

-- import("core.project.config")

-- ============================================================================
-- Configuration
-- ============================================================================

-- Get kernel architecture (may differ from userland, e.g., 32-bit kernel on 64-bit)
local function get_kernel_arch()
    return config.get("kernel_arch") or config.get("arch") or "x86_64"
end

-- Get kernel-specific C flags
local function get_kernel_ccflags()
    local arch = get_kernel_arch()
    local flags = {
        "-ffreestanding",
        "-fno-builtin",
        "-fno-exceptions",
        "-fno-asynchronous-unwind-tables",
    }

    -- Architecture-specific kernel flags
    if arch == "x86_64" then
        table.insert(flags, "-mno-red-zone")
        table.insert(flags, "-mcmodel=kernel")
        table.insert(flags, "-mno-mmx")
        table.insert(flags, "-mno-sse")
        table.insert(flags, "-mno-sse2")
    elseif arch == "x86" then
        table.insert(flags, "-march=pentium")
    elseif arch == "arm64" then
        table.insert(flags, "-mgeneral-regs-only")
    elseif arch == "riscv64" then
        table.insert(flags, "-mcmodel=medany")
        table.insert(flags, "-mno-relax")
    end

    -- Add user-configured kernel flags
    local extra = config.get("kernel_ccflags")
    if extra then
        if type(extra) == "table" then
            for _, f in ipairs(extra) do
                table.insert(flags, f)
            end
        else
            table.insert(flags, extra)
        end
    end

    return flags
end

-- Get kernel-specific C++ flags
local function get_kernel_cxxflags()
    local flags = get_kernel_ccflags()

    -- Additional C++ specific flags
    table.insert(flags, "-fno-rtti")
    table.insert(flags, "-fno-exceptions")

    -- Add user-configured kernel C++ flags
    local extra = config.get("kernel_cxxflags")
    if extra then
        if type(extra) == "table" then
            for _, f in ipairs(extra) do
                table.insert(flags, f)
            end
        else
            table.insert(flags, extra)
        end
    end

    return flags
end

-- Get kernel defines
local function get_kernel_defines()
    local defines = {
        "_KERNEL_MODE",
        "__HAIKU_KERNEL__",
    }

    local arch = get_kernel_arch()
    if arch == "x86_64" then
        table.insert(defines, "__x86_64__")
    elseif arch == "x86" then
        table.insert(defines, "__i386__")
    elseif arch == "arm64" then
        table.insert(defines, "__aarch64__")
    elseif arch == "riscv64" then
        table.insert(defines, "__riscv")
    end

    -- Add user-configured kernel defines
    local extra = config.get("kernel_defines")
    if extra then
        if type(extra) == "table" then
            for _, d in ipairs(extra) do
                table.insert(defines, d)
            end
        else
            table.insert(defines, extra)
        end
    end

    return defines
end

-- Get kernel warning flags
local function get_kernel_warning_ccflags()
    return config.get("kernel_warning_ccflags") or {
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-Werror=return-type",
    }
end

local function get_kernel_warning_cxxflags()
    return config.get("kernel_warning_cxxflags") or get_kernel_warning_ccflags()
end

-- Get private kernel headers
local function get_private_kernel_headers()
    local haiku_top = "$(projectdir)"
    local arch = get_kernel_arch()

    return {
        path.join(haiku_top, "headers", "private", "kernel"),
        path.join(haiku_top, "headers", "private", "kernel", "arch", arch),
        path.join(haiku_top, "headers", "private", "kernel", "boot"),
        path.join(haiku_top, "headers", "private", "kernel", "boot", "platform"),
        path.join(haiku_top, "headers", "private", "libroot"),
        path.join(haiku_top, "headers", "private", "system"),
        path.join(haiku_top, "headers", "private", "system", "arch", arch),
        path.join(haiku_top, "headers", "private"),
        path.join(haiku_top, "headers", "os"),
        path.join(haiku_top, "headers", "posix"),
    }
end

-- Get kernel addon glue code paths
local function get_kernel_addon_begin_glue()
    local arch = get_kernel_arch()
    return path.join("$(buildir)", "objects", "haiku", arch,
        "system", "glue", "arch", arch, "crti.o")
end

local function get_kernel_addon_end_glue()
    local arch = get_kernel_arch()
    return path.join("$(buildir)", "objects", "haiku", arch,
        "system", "glue", "arch", arch, "crtn.o")
end

-- Get kernel libgcc path
local function get_kernel_libgcc()
    local arch = get_kernel_arch()
    return config.get("kernel_libgcc_" .. arch) or
        path.join("$(buildir)", "cross-tools", arch, "lib", "gcc", arch .. "-unknown-haiku", "libgcc.a")
end

-- Get kernel libsupc++ path
local function get_kernel_libsupcxx()
    local arch = get_kernel_arch()
    return config.get("kernel_libsupcxx_" .. arch) or
        path.join("$(buildir)", "cross-tools", arch, "lib", "gcc", arch .. "-unknown-haiku", "libsupc++.a")
end

-- Get kernel linker
local function get_kernel_ld()
    local arch = get_kernel_arch()
    return config.get("kernel_ld_" .. arch) or (arch .. "-unknown-haiku-ld")
end

-- Get kernel archiver
local function get_kernel_ar()
    local arch = get_kernel_arch()
    return config.get("kernel_ar_" .. arch) or (arch .. "-unknown-haiku-ar")
end

-- Get kernel elfedit
local function get_kernel_elfedit()
    local arch = get_kernel_arch()
    return config.get("kernel_elfedit_" .. arch) or "elfedit"
end

-- ============================================================================
-- SetupKernel - Configure kernel compilation
-- ============================================================================

--[[
    SetupKernel(sources, extra_cc_flags, include_private_headers)

    Configures kernel compilation flags for source files.

    Equivalent to Jam:
        rule SetupKernel { }

    Parameters:
        sources - Source files to configure
        extra_cc_flags - Additional compiler flags (optional)
        include_private_headers - Whether to add private kernel headers
                                  (default: true, set to false to disable)

    Returns:
        Configuration table to apply to target
]]
function SetupKernel(sources, extra_cc_flags, include_private_headers)
    if type(sources) ~= "table" then sources = {sources} end
    extra_cc_flags = extra_cc_flags or {}
    if type(extra_cc_flags) ~= "table" then extra_cc_flags = {extra_cc_flags} end

    -- Default is to include private headers
    if include_private_headers == nil then
        include_private_headers = true
    end

    local config_table = {
        sources = sources,
        kernel_arch = get_kernel_arch(),
        ccflags = {},
        cxxflags = {},
        defines = get_kernel_defines(),
        includedirs = {},
    }

    -- Build ccflags
    for _, flag in ipairs(get_kernel_ccflags()) do
        table.insert(config_table.ccflags, flag)
    end
    for _, flag in ipairs(get_kernel_warning_ccflags()) do
        table.insert(config_table.ccflags, flag)
    end
    for _, flag in ipairs(extra_cc_flags) do
        table.insert(config_table.ccflags, flag)
    end

    -- Build cxxflags
    for _, flag in ipairs(get_kernel_cxxflags()) do
        table.insert(config_table.cxxflags, flag)
    end
    for _, flag in ipairs(get_kernel_warning_cxxflags()) do
        table.insert(config_table.cxxflags, flag)
    end
    for _, flag in ipairs(extra_cc_flags) do
        table.insert(config_table.cxxflags, flag)
    end

    -- Add private kernel headers if requested
    if include_private_headers then
        for _, dir in ipairs(get_private_kernel_headers()) do
            table.insert(config_table.includedirs, dir)
        end
    end

    return config_table
end

--[[
    ApplyKernelSetup(target, kernel_config)

    Applies kernel configuration to an xmake target.

    Parameters:
        target - xmake target object
        kernel_config - Configuration from SetupKernel()
]]
function ApplyKernelSetup(target, kernel_config)
    if not kernel_config then return end

    -- Apply C flags
    for _, flag in ipairs(kernel_config.ccflags or {}) do
        target:add("cflags", flag, {force = true})
    end

    -- Apply C++ flags
    for _, flag in ipairs(kernel_config.cxxflags or {}) do
        target:add("cxxflags", flag, {force = true})
    end

    -- Apply defines
    for _, def in ipairs(kernel_config.defines or {}) do
        target:add("defines", def)
    end

    -- Apply include directories
    for _, dir in ipairs(kernel_config.includedirs or {}) do
        target:add("includedirs", dir)
    end

    -- Store kernel arch on target
    target:set("kernel_arch", kernel_config.kernel_arch)
end

-- ============================================================================
-- KernelObjects rule
-- ============================================================================

--[[
    KernelObjects rule for xmake

    Compiles source files with kernel flags.

    Usage:
        target("kernel_objs")
            add_rules("KernelObjects")
            add_files("*.cpp")
            set_values("extra_kernel_flags", {"-DDEBUG"})
]]
rule("KernelObjects")
    on_load(function (target)
        local extra_flags = target:get("extra_kernel_flags") or {}
        local kernel_config = SetupKernel({}, extra_flags, true)
        ApplyKernelSetup(target, kernel_config)

        -- Set as object target (no linking)
        target:set("kind", "object")
    end)

-- ============================================================================
-- KernelLd rule - Link kernel executable
-- ============================================================================

--[[
    KernelLd rule for xmake

    Links a kernel executable with linker script and kernel libraries.

    Usage:
        target("kernel_x86_64")
            add_rules("KernelLd")
            add_files("*.o")
            set_values("linker_script", "kernel.ld")
            set_values("extra_ldflags", {"-z", "max-page-size=0x1000"})
            -- set_values("no_libsupcxx", true)  -- Optional: disable libsupc++
]]
rule("KernelLd")
    on_load(function (target)
        target:set("kind", "binary")

        local arch = get_kernel_arch()

        -- Use kernel linker
        target:set("ld", get_kernel_ld())

        -- Clear default link libraries
        target:set("links", {})

        -- Build link flags
        local ldflags = {}

        -- Add linker script if specified
        local linker_script = target:get("linker_script")
        if linker_script then
            table.insert(ldflags, "--script=" .. linker_script)
            target:add("deps", linker_script)
        end

        -- Add extra ldflags
        local extra_ldflags = target:get("extra_ldflags") or {}
        for _, flag in ipairs(extra_ldflags) do
            table.insert(ldflags, flag)
        end

        target:add("ldflags", ldflags)

        -- Apply kernel setup
        local kernel_config = SetupKernel({}, {}, true)
        ApplyKernelSetup(target, kernel_config)
    end)

    on_link(function (target)
        -- Build link command
        local ld = get_kernel_ld()
        local targetfile = target:targetfile()
        local objectfiles = target:objectfiles()

        -- Collect link flags
        local ldflags = target:get("ldflags") or {}
        if type(ldflags) ~= "table" then ldflags = {ldflags} end

        -- Build library list
        local libs = {}

        -- Add libsupc++ unless disabled
        if not target:get("no_libsupcxx") then
            table.insert(libs, get_kernel_libsupcxx())
        end

        -- Always link libgcc
        table.insert(libs, get_kernel_libgcc())

        -- Version script
        local version_script = target:get("version_script")
        if version_script then
            table.insert(ldflags, "--version-script=" .. version_script)
        end

        -- Build command
        local args = {}
        for _, flag in ipairs(ldflags) do
            table.insert(args, flag)
        end
        table.insert(args, "-o")
        table.insert(args, targetfile)
        for _, obj in ipairs(objectfiles) do
            table.insert(args, obj)
        end
        for _, lib in ipairs(libs) do
            table.insert(args, lib)
        end

        os.mkdir(path.directory(targetfile))
        print("KernelLd: %s", targetfile)
        os.execv(ld, args)
    end)

    after_build(function (target)
        -- Apply BeOS rules unless disabled
        if not target:get("dont_use_beos_rules") then
            local targetfile = target:targetfile()

            -- Import resource functions if available
            local ok, res = pcall(import, "ResourceRules")
            if ok then
                local rsrc_files = target:get("_compiled_rsrc") or {}
                if #rsrc_files > 0 then
                    res.XRes(targetfile, rsrc_files)
                end
                res.SetType(targetfile)
                res.MimeSet(targetfile)
                res.SetVersion(targetfile)
            end
        end
    end)

-- ============================================================================
-- KernelSo - Create kernel shared object
-- ============================================================================

--[[
    KernelSo(target_file, source_file)

    Creates a kernel shared object by copying and converting ELF type.

    Equivalent to Jam:
        rule KernelSo { }
        actions KernelSo1 { copyattr --data ... && elfedit --output-type dyn ... }

    Parameters:
        target_file - Output .so file
        source_file - Source file to copy
]]
function KernelSo(target_file, source_file)
    local elfedit = get_kernel_elfedit()

    os.mkdir(path.directory(target_file))

    -- Copy with data attributes
    local copyattr = path.join("$(buildir)", "tools", "copyattr")
    local copy_args = {"--data", source_file, target_file}

    print("KernelSo: %s <- %s", target_file, source_file)
    os.execv(copyattr, copy_args)

    -- Convert to dynamic type
    local elfedit_args = {"--output-type", "dyn", target_file}
    os.execv(elfedit, elfedit_args)
end

-- ============================================================================
-- KernelAddon rule - Create kernel addon (driver)
-- ============================================================================

--[[
    KernelAddon rule for xmake

    Creates a kernel addon (driver) with proper glue code and linking.

    Usage:
        target("my_driver")
            add_rules("KernelAddon")
            add_files("driver.cpp")
            add_deps("static_lib")  -- optional static libraries
            add_files("driver.rdef")  -- optional resources
]]
rule("KernelAddon")
    on_load(function (target)
        target:set("kind", "shared")

        local arch = get_kernel_arch()
        local platform = target:get("platform") or "haiku"

        -- Check platform support
        local supported = target:get("supported_platforms") or {"haiku", "haiku_host"}
        local is_supported = false
        for _, p in ipairs(supported) do
            if p == platform then
                is_supported = true
                break
            end
        end
        if not is_supported then
            target:set("enabled", false)
            return
        end

        -- Determine kernel and glue code
        local kernel_so
        local begin_glue = get_kernel_addon_begin_glue()
        local end_glue = get_kernel_addon_end_glue()

        if platform == "haiku" then
            kernel_so = "kernel.so"
        elseif platform == "haiku_host" then
            kernel_so = "/boot/develop/lib/x86/kernel.so"
        else
            kernel_so = "/boot/develop/lib/x86/_KERNEL_"
        end

        -- Store glue code paths
        target:set("link_begin_glue", begin_glue)
        target:set("link_end_glue", end_glue)
        target:set("kernel_so", kernel_so)

        -- Add dependencies on glue code
        target:add("deps", begin_glue, {inherit = false})
        target:add("deps", end_glue, {inherit = false})

        -- Apply kernel setup (without private headers - they come from sources)
        local kernel_config = SetupKernel({}, {}, false)
        ApplyKernelSetup(target, kernel_config)

        -- Set link flags
        local target_name = target:name()
        local ldflags = {
            "-shared",
            "-nostdlib",
            "-Xlinker", "--no-undefined",
            "-Xlinker", "-soname=" .. target_name,
        }

        -- Add kernel addon link flags
        local addon_ldflags = config.get("kernel_addon_linkflags") or {}
        for _, flag in ipairs(addon_ldflags) do
            table.insert(ldflags, flag)
        end

        target:add("ldflags", ldflags)

        -- Link against kernel
        target:add("links", kernel_so)
    end)

    before_link(function (target)
        -- Add glue code to link
        local begin_glue = target:get("link_begin_glue")
        local end_glue = target:get("link_end_glue")

        if begin_glue then
            target:add("objects", begin_glue, {order = true})
        end
        -- end_glue and libgcc are added at the end in after_link
    end)

    after_link(function (target)
        local end_glue = target:get("link_end_glue")
        local libgcc = get_kernel_libgcc()

        -- These need to come at the end of link line
        -- In xmake this is handled differently; here we note that
        -- the custom link rule should add these at the end
    end)

-- ============================================================================
-- KernelMergeObject rule
-- ============================================================================

--[[
    KernelMergeObject rule for xmake

    Compiles sources with kernel flags and merges into a single object.

    Usage:
        target("kernel_merged")
            add_rules("KernelMergeObject")
            add_files("*.cpp")
            set_values("extra_kernel_flags", {"-DDEBUG"})
            set_values("other_objects", {"lib.a"})
]]
rule("KernelMergeObject")
    on_load(function (target)
        -- Set as object target
        target:set("kind", "object")

        local extra_flags = target:get("extra_kernel_flags") or {}
        local kernel_config = SetupKernel({}, extra_flags, true)
        ApplyKernelSetup(target, kernel_config)
    end)

    after_build(function (target)
        local objectfiles = target:objectfiles()
        local other_objects = target:get("other_objects") or {}

        if type(other_objects) ~= "table" then
            other_objects = {other_objects}
        end

        -- Merge all objects
        local all_objects = {}
        for _, obj in ipairs(objectfiles) do
            table.insert(all_objects, obj)
        end
        for _, obj in ipairs(other_objects) do
            table.insert(all_objects, obj)
        end

        if #all_objects > 1 then
            local arch = get_kernel_arch()
            local ld = get_kernel_ld()
            local targetfile = target:targetfile()

            -- Use ld -r to create relocatable object
            local args = {"-r", "-o", targetfile}
            for _, obj in ipairs(all_objects) do
                table.insert(args, obj)
            end

            print("KernelMergeObject: %s", targetfile)
            os.execv(ld, args)
        end
    end)

-- ============================================================================
-- KernelStaticLibrary rule
-- ============================================================================

--[[
    KernelStaticLibrary rule for xmake

    Creates a static library compiled with kernel flags.

    Usage:
        target("kernel_lib")
            add_rules("KernelStaticLibrary")
            add_files("*.cpp")
            set_values("extra_kernel_flags", {"-DDEBUG"})
            -- set_values("no_hidden_visibility", true)  -- Optional
]]
rule("KernelStaticLibrary")
    on_load(function (target)
        target:set("kind", "static")

        local extra_flags = {}

        -- Add -fvisibility=hidden unless disabled
        if not target:get("no_hidden_visibility") then
            table.insert(extra_flags, "-fvisibility=hidden")
        end

        -- Add user extra flags
        local user_flags = target:get("extra_kernel_flags") or {}
        if type(user_flags) ~= "table" then user_flags = {user_flags} end
        for _, flag in ipairs(user_flags) do
            table.insert(extra_flags, flag)
        end

        -- Apply kernel setup (without private headers for library)
        local kernel_config = SetupKernel({}, extra_flags, false)
        ApplyKernelSetup(target, kernel_config)
    end)

-- ============================================================================
-- KernelStaticLibraryObjects rule
-- ============================================================================

--[[
    KernelStaticLibraryObjects rule for xmake

    Creates a static library from pre-compiled object files.

    Usage:
        target("kernel_lib_from_objs")
            add_rules("KernelStaticLibraryObjects")
            add_files("*.o")
]]
rule("KernelStaticLibraryObjects")
    on_load(function (target)
        target:set("kind", "static")

        local kernel_config = SetupKernel({}, {}, true)
        ApplyKernelSetup(target, kernel_config)
    end)

    on_build(function (target)
        local ar = get_kernel_ar()
        local targetfile = target:targetfile()
        local objectfiles = target:objectfiles()

        -- Force recreation to avoid stale dependencies
        os.rm(targetfile)
        os.mkdir(path.directory(targetfile))

        local args = {"-r", targetfile}
        for _, obj in ipairs(objectfiles) do
            table.insert(args, obj)
        end

        print("KernelStaticLibraryObjects: %s", targetfile)
        os.execv(ar, args)
    end)

-- ============================================================================
-- Helper functions for external use
-- ============================================================================

--[[
    GetKernelArch()

    Returns the kernel target architecture.
]]
function GetKernelArch()
    return get_kernel_arch()
end

--[[
    GetKernelCCFlags()

    Returns kernel C compiler flags.
]]
function GetKernelCCFlags()
    return get_kernel_ccflags()
end

--[[
    GetKernelCXXFlags()

    Returns kernel C++ compiler flags.
]]
function GetKernelCXXFlags()
    return get_kernel_cxxflags()
end

--[[
    GetKernelDefines()

    Returns kernel preprocessor defines.
]]
function GetKernelDefines()
    return get_kernel_defines()
end

--[[
    GetPrivateKernelHeaders()

    Returns private kernel header directories.
]]
function GetPrivateKernelHeaders()
    return get_private_kernel_headers()
end

--[[
    GetKernelLibgcc()

    Returns path to kernel libgcc.a.
]]
function GetKernelLibgcc()
    return get_kernel_libgcc()
end

--[[
    GetKernelLibsupcxx()

    Returns path to kernel libsupc++.a.
]]
function GetKernelLibsupcxx()
    return get_kernel_libsupcxx()
end
