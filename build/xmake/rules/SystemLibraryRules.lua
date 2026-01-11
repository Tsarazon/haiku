--[[
    SystemLibraryRules.lua - System library management rules

    xmake equivalent of build/jam/SystemLibraryRules

    This module provides rules for managing GCC runtime system libraries
    (libstdc++, libsupc++, libgcc, etc.) for various build contexts:
    - Target platform (Haiku)
    - Kernel builds
    - Bootloader builds

    Rules defined:
    - Libstdc++ForImage       - Get libstdc++ for image creation
    - TargetLibstdc++         - Get libstdc++.so for target
    - TargetLibsupc++         - Get libsupc++ for target
    - TargetStaticLibsupc++   - Get static libsupc++.a
    - TargetKernelLibsupc++   - Get kernel libsupc++-kernel.a
    - TargetBootLibsupc++     - Get bootloader libsupc++
    - TargetLibgcc            - Get libgcc (shared + static)
    - TargetStaticLibgcc      - Get static libgcc.a
    - TargetKernelLibgcc      - Get kernel libgcc-kernel.a
    - TargetBootLibgcc        - Get bootloader libgcc
    - TargetStaticLibgcceh    - Get static libgcc_eh.a
    - TargetKernelLibgcceh    - Get kernel libgcc_eh-kernel.a
    - C++HeaderDirectories    - Get C++ header directories
    - GccHeaderDirectories    - Get GCC header directories

    Note: These rules rely on BuildFeatureAttribute from BuildFeatureRules.lua
    to locate libraries from the gcc_syslibs and gcc_syslibs_devel build features.
]]

-- import("core.project.config")

-- ============================================================================
-- Configuration
-- ============================================================================

--[[
    GetTargetPlatform()

    Get the current target platform.

    Returns:
        Platform name string (e.g., "haiku", "libbe_test", "host")
]]
function GetTargetPlatform()
    return config.get("TARGET_PLATFORM") or "haiku"
end

--[[
    GetTargetPackagingArch()

    Get the target packaging architecture.

    Returns:
        Architecture string (e.g., "x86_64", "arm", "arm64")
]]
function GetTargetPackagingArch()
    return config.get("TARGET_PACKAGING_ARCH") or config.get("arch") or "x86_64"
end

--[[
    GetTargetKernelArch()

    Get the target kernel architecture.

    Returns:
        Kernel architecture string
]]
function GetTargetKernelArch()
    return config.get("TARGET_KERNEL_ARCH") or GetTargetPackagingArch()
end

--[[
    GetTargetBootPlatform()

    Get the target boot platform.

    Returns:
        Boot platform string (e.g., "efi", "bios_ia32", "u-boot")
]]
function GetTargetBootPlatform()
    return config.get("TARGET_BOOT_PLATFORM") or "bios_ia32"
end

--[[
    GetPlatform()

    Get the current platform mode.

    Returns:
        Platform mode string (e.g., "haiku", "host", "bootstrap_stage0")
]]
function GetPlatform()
    return config.get("PLATFORM") or "haiku"
end

--[[
    GetGccMachine(architecture)

    Get the GCC machine identifier for an architecture.

    Parameters:
        architecture - Target architecture

    Returns:
        GCC machine string (e.g., "x86_64-unknown-haiku")
]]
function GetGccMachine(architecture)
    local machine = config.get("HAIKU_GCC_MACHINE_" .. architecture)
    if machine then
        return machine
    end

    -- Default machine strings
    local machines = {
        x86_64 = "x86_64-unknown-haiku",
        x86 = "i586-unknown-haiku",
        arm = "arm-unknown-haiku",
        arm64 = "aarch64-unknown-haiku",
        riscv64 = "riscv64-unknown-haiku",
        sparc = "sparc64-unknown-haiku",
        m68k = "m68k-unknown-haiku",
    }

    return machines[architecture] or (architecture .. "-unknown-haiku")
end

-- ============================================================================
-- Build Feature Integration
-- ============================================================================

-- Note: These functions assume BuildFeatureAttribute is available from
-- BuildFeatureRules.lua. If not loaded, they will return nil.

--[[
    BuildFeatureLibrary(feature, library, as_path)

    Get a library from a build feature.

    Parameters:
        feature  - Build feature name (e.g., "gcc_syslibs")
        library  - Library name (e.g., "libstdc++.so")
        as_path  - If true, return full path; otherwise return library name

    Returns:
        Library path or name, or nil if not available
]]
function BuildFeatureLibrary(feature, library, as_path)
    -- Try to use BuildFeatureAttribute if available
    if BuildFeatureAttribute then
        local flags = as_path and {"path"} or {}
        return BuildFeatureAttribute(feature, library, flags)
    end

    -- Fallback: construct expected path
    local feature_dir = config.get(feature:upper() .. "_DIR")
    if feature_dir and as_path then
        return path.join(feature_dir, "lib", library)
    end

    return library
end

--[[
    QualifiedBuildFeatureLibrary(qualified_feature, library, as_path)

    Get a library from a qualified (architecture-specific) build feature.

    Parameters:
        qualified_feature - Qualified feature name (e.g., "x86_64:gcc_syslibs_devel")
        library           - Library name
        as_path           - If true, return full path

    Returns:
        Library path or name, or nil if not available
]]
function QualifiedBuildFeatureLibrary(qualified_feature, library, as_path)
    -- Try to use BuildFeatureAttribute with QUALIFIED flag if available
    if BuildFeatureAttribute then
        local flags = as_path and {"path"} or {}
        return BuildFeatureAttribute("QUALIFIED", qualified_feature, library, flags)
    end

    -- Parse the qualified feature
    local arch, feature = qualified_feature:match("^([^:]+):(.+)$")
    if not arch then
        arch = GetTargetPackagingArch()
        feature = qualified_feature
    end

    -- Fallback: construct expected path
    local feature_dir = config.get(feature:upper() .. "_DIR")
    if feature_dir and as_path then
        return path.join(feature_dir, arch, "lib", library)
    end

    return library
end

-- ============================================================================
-- libstdc++ Rules
-- ============================================================================

--[[
    Libstdc++ForImage()

    Returns the c++-standard-library to be put onto the image.

    Equivalent to Jam:
        rule Libstdc++ForImage { }

    Returns:
        nil - libstdc++.so comes with the gcc_syslibs package,
              so there's no library to put onto the image directly.
]]
function Libstdc__ForImage()
    -- libstdc++.so comes with the gcc_syslibs package,
    -- so there's no library to put onto the image directly.
    return nil
end

-- Alias with proper name
Libstdc_plus_plus_ForImage = Libstdc__ForImage

--[[
    TargetLibstdc__(as_path)

    Returns the c++-standard-library for the target.

    Equivalent to Jam:
        rule TargetLibstdc++ [ <asPath> ] { }

    Parameters:
        as_path - If true, return full library path

    Returns:
        libstdc++.so path or name, or nil for non-Haiku platforms
]]
function TargetLibstdc__(as_path)
    local platform = GetTargetPlatform()

    if platform == "haiku" or platform == "libbe_test" then
        -- Return libstdc++.so from the gcc_syslibs build feature
        return BuildFeatureLibrary("gcc_syslibs", "libstdc++.so", as_path)
    end

    -- TODO: return libstdc++.so for non-Haiku target platform if needed
    return nil
end

-- Alias with proper name
TargetLibstdc_plus_plus = TargetLibstdc__

-- ============================================================================
-- libsupc++ Rules
-- ============================================================================

--[[
    TargetLibsupc__(as_path)

    Returns the c++-support-library for the target.

    Equivalent to Jam:
        rule TargetLibsupc++ [ <asPath> ] { }

    Parameters:
        as_path - If true, return full library path

    Returns:
        libstdc++.so (which includes libsupc++) path or name
]]
function TargetLibsupc__(as_path)
    local platform = GetTargetPlatform()

    if platform == "haiku" then
        -- Return libstdc++.so (which includes libsupc++) from gcc_syslibs
        return BuildFeatureLibrary("gcc_syslibs", "libstdc++.so", as_path)
    end

    -- TODO: return libsupc++.so for non-Haiku target platform if needed
    return nil
end

-- Alias with proper name
TargetLibsupc_plus_plus = TargetLibsupc__

--[[
    TargetStaticLibsupc__(as_path)

    Returns the static c++-support-library for the target.

    Equivalent to Jam:
        rule TargetStaticLibsupc++ [ <asPath> ] { }

    Parameters:
        as_path - If true, return full library path

    Returns:
        libsupc++.a path or name
]]
function TargetStaticLibsupc__(as_path)
    local platform = GetTargetPlatform()

    if platform == "haiku" then
        -- Return libsupc++.a from the gcc_syslibs_devel build feature
        return BuildFeatureLibrary("gcc_syslibs_devel", "libsupc++.a", as_path)
    end

    -- TODO: return libsupc++.a for non-Haiku target platform if needed
    return nil
end

-- Alias with proper name
TargetStaticLibsupc_plus_plus = TargetStaticLibsupc__

--[[
    TargetKernelLibsupc__(as_path)

    Returns the static kernel c++-support-library for the target.

    Equivalent to Jam:
        rule TargetKernelLibsupc++ [ <asPath> ] { }

    Parameters:
        as_path - If true, return full library path

    Returns:
        libsupc++-kernel.a path or name
]]
function TargetKernelLibsupc__(as_path)
    local platform = GetTargetPlatform()

    if platform == "haiku" then
        -- Return libsupc++-kernel.a from the qualified gcc_syslibs_devel
        local kernel_arch = GetTargetKernelArch()
        local qualified = kernel_arch .. ":gcc_syslibs_devel"
        return QualifiedBuildFeatureLibrary(qualified, "libsupc++-kernel.a", as_path)
    end

    -- There is no libsupc++-kernel.a for non-Haiku target platform
    return nil
end

-- Alias with proper name
TargetKernelLibsupc_plus_plus = TargetKernelLibsupc__

--[[
    TargetBootLibsupc__(as_path)

    Returns the static bootloader c++-support-library for the target.

    Equivalent to Jam:
        rule TargetBootLibsupc++ [ <asPath> ] { }

    This has special handling for x86_64 architecture where different
    libraries are needed for EFI vs BIOS boot.

    Parameters:
        as_path - If true, return full library path

    Returns:
        Appropriate libsupc++ for bootloader
]]
function TargetBootLibsupc__(as_path)
    local platform = GetTargetPlatform()

    if platform == "haiku" then
        local arch = GetTargetPackagingArch()
        local boot_platform = GetTargetBootPlatform()

        if arch == "x86_64" then
            if boot_platform == "efi" then
                -- Use 64-bit libsupc++.a built by cross-compiler
                return config.get("TARGET_BOOT_LIBSUPC++")
            else
                -- Use 32-bit libsupc++.a built by cross-compiler
                return config.get("TARGET_BOOT_32_LIBSUPC++")
            end
            -- TODO: ideally, build as part of gcc_syslibs_devel
        end

        -- No special boot version needed, return libsupc++-kernel.a
        -- or libsupc++-boot.a for ARM
        if arch == "arm" then
            return BuildFeatureLibrary("gcc_syslibs_devel", "libsupc++-boot.a", as_path)
        end

        return BuildFeatureLibrary("gcc_syslibs_devel", "libsupc++-kernel.a", as_path)
    end

    -- There is no libsupc++-boot.a for non-Haiku target platform
    return nil
end

-- Alias with proper name
TargetBootLibsupc_plus_plus = TargetBootLibsupc__

-- ============================================================================
-- libgcc Rules
-- ============================================================================

--[[
    TargetLibgcc(as_path)

    Returns the default libgcc(s) for the target.

    Equivalent to Jam:
        rule TargetLibgcc [ <asPath> ] { }

    This returns both the shared libgcc_s and the static libgcc as
    they contain different sets of symbols.

    Parameters:
        as_path - If true, return full library paths

    Returns:
        Table with {libgcc_s.so.1, libgcc.a} or nil
]]
function TargetLibgcc(as_path)
    local platform = GetTargetPlatform()

    if platform == "haiku" then
        -- Return libgcc_s.so from gcc_syslibs and libgcc.a from gcc_syslibs_devel
        local libgcc_s = BuildFeatureLibrary("gcc_syslibs", "libgcc_s.so.1", as_path)
        local libgcc = BuildFeatureLibrary("gcc_syslibs_devel", "libgcc.a", as_path)

        if libgcc_s and libgcc then
            return {libgcc_s, libgcc}
        elseif libgcc_s then
            return {libgcc_s}
        elseif libgcc then
            return {libgcc}
        end
    end

    -- TODO: return libgcc for non-Haiku target platform if needed
    return nil
end

--[[
    TargetStaticLibgcc(as_path)

    Returns the static libgcc for the target.

    Equivalent to Jam:
        rule TargetStaticLibgcc [ <asPath> ] { }

    Parameters:
        as_path - If true, return full library path

    Returns:
        libgcc.a path or name
]]
function TargetStaticLibgcc(as_path)
    local platform = GetTargetPlatform()

    if platform == "haiku" then
        -- Return libgcc.a from the gcc_syslibs_devel build feature
        return BuildFeatureLibrary("gcc_syslibs_devel", "libgcc.a", as_path)
    end

    -- TODO: return libgcc.a for non-Haiku target platform if needed
    return nil
end

--[[
    TargetKernelLibgcc(as_path)

    Returns the static kernel libgcc for the target.

    Equivalent to Jam:
        rule TargetKernelLibgcc [ <asPath> ] { }

    Parameters:
        as_path - If true, return full library path

    Returns:
        libgcc-kernel.a path or name
]]
function TargetKernelLibgcc(as_path)
    local platform = GetTargetPlatform()

    if platform == "haiku" then
        -- Return libgcc-kernel.a from the qualified gcc_syslibs_devel
        local kernel_arch = GetTargetKernelArch()
        local qualified = kernel_arch .. ":gcc_syslibs_devel"
        return QualifiedBuildFeatureLibrary(qualified, "libgcc-kernel.a", as_path)
    end

    -- There is no libgcc-kernel.a for non-Haiku target platform
    return nil
end

--[[
    TargetBootLibgcc(architecture, as_path)

    Returns the static bootloader libgcc for the target.

    Equivalent to Jam:
        rule TargetBootLibgcc [ architecture ] : [ <asPath> ] { }

    Parameters:
        architecture - Target architecture (optional, defaults to packaging arch)
        as_path      - If true, return full library path

    Returns:
        Appropriate libgcc for bootloader
]]
function TargetBootLibgcc(architecture, as_path)
    local platform = GetTargetPlatform()
    architecture = architecture or GetTargetPackagingArch()

    if platform == "haiku" then
        local boot_platform = GetTargetBootPlatform()

        if architecture == "x86_64" then
            if boot_platform == "efi" then
                -- Use 64-bit libgcc.a built by cross-compiler
                return config.get("TARGET_BOOT_LIBGCC")
            else
                -- Use 32-bit libgcc.a built by cross-compiler
                return config.get("TARGET_BOOT_32_LIBGCC")
            end
            -- TODO: ideally, build as part of gcc_syslibs_devel
        end

        -- No special boot version needed
        local qualified = architecture .. ":gcc_syslibs_devel"

        if architecture == "arm" then
            return QualifiedBuildFeatureLibrary(qualified, "libgcc-boot.a", as_path)
        end

        return QualifiedBuildFeatureLibrary(qualified, "libgcc-kernel.a", as_path)
    end

    -- There is no libgcc-boot.a for non-Haiku target platform
    return nil
end

-- ============================================================================
-- libgcc_eh Rules (Exception Handling)
-- ============================================================================

--[[
    TargetStaticLibgcceh(as_path)

    Returns the static libgcc_eh for the target.

    Equivalent to Jam:
        rule TargetStaticLibgcceh [ <asPath> ] { }

    Parameters:
        as_path - If true, return full library path

    Returns:
        libgcc_eh.a path or name
]]
function TargetStaticLibgcceh(as_path)
    local platform = GetTargetPlatform()

    if platform == "haiku" then
        -- Return libgcc_eh.a from the gcc_syslibs_devel build feature
        return BuildFeatureLibrary("gcc_syslibs_devel", "libgcc_eh.a", as_path)
    end

    -- TODO: return libgcc_eh.a for non-Haiku target platform if needed
    return nil
end

--[[
    TargetKernelLibgcceh(as_path)

    Returns the static kernel libgcc_eh for the target.

    Equivalent to Jam:
        rule TargetKernelLibgcceh [ <asPath> ] { }

    Parameters:
        as_path - If true, return full library path

    Returns:
        libgcc_eh-kernel.a path or name
]]
function TargetKernelLibgcceh(as_path)
    local platform = GetTargetPlatform()

    if platform == "haiku" then
        -- Return libgcc_eh-kernel.a from the qualified gcc_syslibs_devel
        local kernel_arch = GetTargetKernelArch()
        local qualified = kernel_arch .. ":gcc_syslibs_devel"
        return QualifiedBuildFeatureLibrary(qualified, "libgcc_eh-kernel.a", as_path)
    end

    -- There is no libgcc_eh-kernel.a for non-Haiku target platform
    return nil
end

-- ============================================================================
-- Header Directory Rules
-- ============================================================================

--[[
    CxxHeaderDirectories(architecture)

    Returns the C++ header directories for the given architecture.

    Equivalent to Jam:
        rule C++HeaderDirectories [ architecture ] { }

    Parameters:
        architecture - Target architecture

    Returns:
        Table of C++ header directory paths
]]
function CxxHeaderDirectories(architecture)
    architecture = architecture or GetTargetPackagingArch()
    local platform = GetPlatform()

    -- No C++ headers needed for bootstrap stage0
    if platform == "bootstrap_stage0" then
        return {}
    end

    local dirs = {}

    -- Get base directory from gcc_syslibs_devel
    local base_dir = nil
    if BuildFeatureAttribute then
        base_dir = BuildFeatureAttribute("gcc_syslibs_devel", "c++-headers", {"path"})
    else
        -- Fallback
        local feature_dir = config.get("GCC_SYSLIBS_DEVEL_DIR")
        if feature_dir then
            base_dir = path.join(feature_dir, "include", "c++")
        end
    end

    if base_dir then
        local gcc_machine = GetGccMachine(architecture)

        table.insert(dirs, base_dir)
        table.insert(dirs, path.join(base_dir, gcc_machine))
        table.insert(dirs, path.join(base_dir, "backward"))
        table.insert(dirs, path.join(base_dir, "ext"))
    end

    return dirs
end

-- Alias with C++ in name (for Jam compatibility)
C_plus_plus_HeaderDirectories = CxxHeaderDirectories

--[[
    GccHeaderDirectories(architecture)

    Returns the GCC header directories for the given architecture.

    Equivalent to Jam:
        rule GccHeaderDirectories [ architecture ] { }

    Parameters:
        architecture - Target architecture

    Returns:
        Table of GCC header directory paths
]]
function GccHeaderDirectories(architecture)
    architecture = architecture or GetTargetPackagingArch()
    local platform = GetPlatform()

    local dirs = {}

    if platform == "bootstrap_stage0" then
        -- Use GCC lib dir for bootstrap stage0
        local gcc_lib_dir = config.get("HAIKU_GCC_LIB_DIR_" .. architecture)
        if gcc_lib_dir then
            table.insert(dirs, path.join(gcc_lib_dir, "include"))
            table.insert(dirs, path.join(gcc_lib_dir, "include-fixed"))
        end
    else
        -- Get base directory from gcc_syslibs_devel
        local base_dir = nil
        if BuildFeatureAttribute then
            base_dir = BuildFeatureAttribute("gcc_syslibs_devel", "gcc-headers", {"path"})
        else
            -- Fallback
            local feature_dir = config.get("GCC_SYSLIBS_DEVEL_DIR")
            if feature_dir then
                base_dir = path.join(feature_dir, "lib", "gcc")
            end
        end

        if base_dir then
            table.insert(dirs, path.join(base_dir, "include"))
            table.insert(dirs, path.join(base_dir, "include-fixed"))
        end
    end

    return dirs
end

-- ============================================================================
-- Convenience Functions
-- ============================================================================

--[[
    GetAllSystemLibraries(context, as_path)

    Get all system libraries needed for a given build context.

    Parameters:
        context - Build context: "target", "kernel", or "boot"
        as_path - If true, return full paths

    Returns:
        Table with library paths/names
]]
function GetAllSystemLibraries(context, as_path)
    context = context or "target"

    local libs = {}

    if context == "target" then
        -- Standard target libraries
        local libstdcxx = TargetLibstdc__(as_path)
        if libstdcxx then table.insert(libs, libstdcxx) end

        local libgcc = TargetLibgcc(as_path)
        if libgcc then
            if type(libgcc) == "table" then
                for _, l in ipairs(libgcc) do
                    table.insert(libs, l)
                end
            else
                table.insert(libs, libgcc)
            end
        end

    elseif context == "kernel" then
        -- Kernel libraries (static only)
        local libsupcxx = TargetKernelLibsupc__(as_path)
        if libsupcxx then table.insert(libs, libsupcxx) end

        local libgcc = TargetKernelLibgcc(as_path)
        if libgcc then table.insert(libs, libgcc) end

        local libgcceh = TargetKernelLibgcceh(as_path)
        if libgcceh then table.insert(libs, libgcceh) end

    elseif context == "boot" then
        -- Bootloader libraries
        local libsupcxx = TargetBootLibsupc__(as_path)
        if libsupcxx then table.insert(libs, libsupcxx) end

        local libgcc = TargetBootLibgcc(nil, as_path)
        if libgcc then table.insert(libs, libgcc) end
    end

    return libs
end

--[[
    GetAllSystemHeaders(architecture)

    Get all system header directories for an architecture.

    Parameters:
        architecture - Target architecture

    Returns:
        Table of header directory paths
]]
function GetAllSystemHeaders(architecture)
    architecture = architecture or GetTargetPackagingArch()

    local dirs = {}

    -- C++ headers
    local cxx_dirs = CxxHeaderDirectories(architecture)
    for _, d in ipairs(cxx_dirs) do
        table.insert(dirs, d)
    end

    -- GCC headers
    local gcc_dirs = GccHeaderDirectories(architecture)
    for _, d in ipairs(gcc_dirs) do
        table.insert(dirs, d)
    end

    return dirs
end

-- ============================================================================
-- xmake Integration
-- ============================================================================

--[[
    Rule: system_libraries

    xmake rule for automatically adding system libraries to targets.
]]
rule("system_libraries")
    on_load(function (target)
        local context = target:get("system_library_context") or "target"
        local libs = GetAllSystemLibraries(context, true)

        for _, lib in ipairs(libs) do
            if lib:match("%.a$") then
                -- Static library - add as link
                target:add("links", lib)
            elseif lib:match("%.so") then
                -- Shared library - add as link
                local libname = path.basename(lib):gsub("%.so.*$", "")
                target:add("links", libname)
            end
        end
    end)
rule_end()

--[[
    Rule: system_headers

    xmake rule for automatically adding system headers to targets.
]]
rule("system_headers")
    on_load(function (target)
        local arch = target:get("architecture") or GetTargetPackagingArch()
        local dirs = GetAllSystemHeaders(arch)

        for _, dir in ipairs(dirs) do
            target:add("includedirs", dir, {public = false})
        end
    end)
rule_end()