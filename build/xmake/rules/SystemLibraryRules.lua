--[[
    SystemLibraryRules.lua - Haiku system library resolution rules

    xmake equivalent of build/jam/SystemLibraryRules (1:1 migration)

    Functions that resolve system library paths from build features:
    - Libstdc++ForImage()                  - libstdc++ for image (empty, from gcc_syslibs pkg)
    - TargetLibstdc++(as_path)             - libstdc++.so for target
    - TargetLibsupc++(as_path)             - libsupc++ for target
    - TargetStaticLibsupc++(as_path)       - libsupc++.a for target
    - TargetKernelLibsupc++(as_path)       - libsupc++-kernel.a for target
    - TargetBootLibsupc++(as_path)         - libsupc++ for bootloader
    - TargetLibgcc(as_path)                - libgcc_s.so + libgcc.a for target
    - TargetStaticLibgcc(as_path)          - libgcc.a for target
    - TargetKernelLibgcc(as_path)          - libgcc-kernel.a for target
    - TargetBootLibgcc(arch, as_path)      - libgcc for bootloader
    - TargetStaticLibgcceh(as_path)        - libgcc_eh.a for target
    - TargetKernelLibgcceh(as_path)        - libgcc_eh-kernel.a for target
    - C++HeaderDirectories(arch)           - C++ header dirs from gcc_syslibs_devel
    - GccHeaderDirectories(arch)           - GCC header dirs from gcc_syslibs_devel

    Usage:
        import("rules.SystemLibraryRules")
        local libs = SystemLibraryRules.TargetLibgcc(true)
]]

-- Helper: get a build feature attribute value
local function _build_feature_attribute(feature, attribute, flags)
    import("core.project.config")

    local key = "feature_" .. feature .. "_" .. attribute
    if flags and type(flags) == "table" then
        for _, f in ipairs(flags) do
            if f == "path" then
                key = key .. "_path"
            end
        end
    elseif flags == "path" then
        key = key .. "_path"
    end

    return config.get(key)
end

-- Helper: get a qualified (architecture-specific) build feature attribute
local function _qualified_feature_attribute(arch, feature, attribute, flags)
    import("core.project.config")

    local key = "feature_" .. arch .. "_" .. feature .. "_" .. attribute
    if flags == "path" or (type(flags) == "table" and flags[1] == "path") then
        key = key .. "_path"
    end

    return config.get(key)
end

-- Libstdc++ForImage: returns nothing (comes from gcc_syslibs package)
function Libstdc_ForImage()
    return nil
end

-- TargetLibstdc++: returns libstdc++.so for the target platform
function TargetLibstdc(as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform == "haiku" or platform == "libbe_test" then
        local flags = as_path and "path" or nil
        return _build_feature_attribute("gcc_syslibs", "libstdc++.so", flags)
    end
    return nil
end

-- TargetLibsupc++: returns libsupc++ (actually libstdc++.so which includes it)
function TargetLibsupc(as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform == "haiku" then
        local flags = as_path and "path" or nil
        return _build_feature_attribute("gcc_syslibs", "libstdc++.so", flags)
    end
    return nil
end

-- TargetStaticLibsupc++: returns libsupc++.a
function TargetStaticLibsupc(as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform == "haiku" then
        local flags = as_path and "path" or nil
        return _build_feature_attribute("gcc_syslibs_devel", "libsupc++.a", flags)
    end
    return nil
end

-- TargetKernelLibsupc++: returns libsupc++-kernel.a for the kernel arch
function TargetKernelLibsupc(as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform == "haiku" then
        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        local flags = as_path and "path" or nil
        return _qualified_feature_attribute(
            arch, "gcc_syslibs_devel", "libsupc++-kernel.a", flags)
    end
    return nil
end

-- TargetBootLibsupc++: returns the bootloader libsupc++
function TargetBootLibsupc(as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform ~= "haiku" then return nil end

    local arch = config.get("target_packaging_arch") or config.get("arch") or "x86_64"
    local boot_platform = config.get("target_boot_platform") or "efi"

    if arch == "x86_64" then
        if boot_platform == "efi" then
            return config.get("target_boot_libsupc++")
        else
            return config.get("target_boot_32_libsupc++")
        end
    end

    -- No special boot version needed; use kernel variant
    local flags = as_path and "path" or nil
    if arch == "arm" then
        return _build_feature_attribute("gcc_syslibs_devel", "libsupc++-boot.a", flags)
    end
    return _build_feature_attribute("gcc_syslibs_devel", "libsupc++-kernel.a", flags)
end

-- TargetLibgcc: returns libgcc_s.so + libgcc.a
function TargetLibgcc(as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform == "haiku" then
        local flags = as_path and "path" or nil
        local libgcc_s = _build_feature_attribute("gcc_syslibs", "libgcc_s.so.1", flags)
        local libgcc = _build_feature_attribute("gcc_syslibs_devel", "libgcc.a", flags)
        return {libgcc_s, libgcc}
    end
    return nil
end

-- TargetStaticLibgcc: returns libgcc.a
function TargetStaticLibgcc(as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform == "haiku" then
        local flags = as_path and "path" or nil
        return _build_feature_attribute("gcc_syslibs_devel", "libgcc.a", flags)
    end
    return nil
end

-- TargetKernelLibgcc: returns libgcc-kernel.a for the kernel arch
function TargetKernelLibgcc(as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform == "haiku" then
        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        local flags = as_path and "path" or nil
        return _qualified_feature_attribute(
            arch, "gcc_syslibs_devel", "libgcc-kernel.a", flags)
    end
    return nil
end

-- TargetBootLibgcc: returns the bootloader libgcc
function TargetBootLibgcc(architecture, as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform ~= "haiku" then return nil end

    architecture = architecture or config.get("target_packaging_arch")
        or config.get("arch") or "x86_64"
    local boot_platform = config.get("target_boot_platform") or "efi"

    if architecture == "x86_64" then
        if boot_platform == "efi" then
            return config.get("target_boot_libgcc")
        else
            return config.get("target_boot_32_libgcc")
        end
    end

    local flags = as_path and "path" or nil
    if architecture == "arm" then
        return _qualified_feature_attribute(
            architecture, "gcc_syslibs_devel", "libgcc-boot.a", flags)
    end
    return _qualified_feature_attribute(
        architecture, "gcc_syslibs_devel", "libgcc-kernel.a", flags)
end

-- TargetStaticLibgcceh: returns libgcc_eh.a
function TargetStaticLibgcceh(as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform == "haiku" then
        local flags = as_path and "path" or nil
        return _build_feature_attribute("gcc_syslibs_devel", "libgcc_eh.a", flags)
    end
    return nil
end

-- TargetKernelLibgcceh: returns libgcc_eh-kernel.a for the kernel arch
function TargetKernelLibgcceh(as_path)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform == "haiku" then
        local arch = config.get("target_kernel_arch") or config.get("arch") or "x86_64"
        local flags = as_path and "path" or nil
        return _qualified_feature_attribute(
            arch, "gcc_syslibs_devel", "libgcc_eh-kernel.a", flags)
    end
    return nil
end

-- C++HeaderDirectories: returns C++ header dirs for an architecture
function CppHeaderDirectories(architecture)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"
    if platform == "bootstrap_stage0" then
        return {}
    end

    local base_dir = _build_feature_attribute("gcc_syslibs_devel", "c++-headers", "path")
    if not base_dir then return {} end

    local gcc_machine = config.get("haiku_gcc_machine_" .. architecture) or ""

    return {
        base_dir,
        path.join(base_dir, gcc_machine),
        path.join(base_dir, "backward"),
        path.join(base_dir, "ext"),
    }
end

-- GccHeaderDirectories: returns GCC header dirs for an architecture
function GccHeaderDirectories(architecture)
    import("core.project.config")

    local platform = config.get("target_platform") or "haiku"

    if platform == "bootstrap_stage0" then
        local gcc_lib_dir = config.get("haiku_gcc_lib_dir_" .. architecture)
        if gcc_lib_dir then
            return {
                path.join(gcc_lib_dir, "include"),
                path.join(gcc_lib_dir, "include-fixed"),
            }
        end
        return {}
    end

    local base_dir = _build_feature_attribute("gcc_syslibs_devel", "gcc-headers", "path")
    if not base_dir then return {} end

    return {
        path.join(base_dir, "include"),
        path.join(base_dir, "include-fixed"),
    }
end
