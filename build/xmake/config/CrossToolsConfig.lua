--[[
    Cross-Tools Configuration for Haiku xmake build system

    This module handles:
    1. Building cross-compiler toolchain (gcc, binutils) for Haiku targets
    2. Auto-detecting existing cross-tools from generated/ or spawned/
    3. Defining xmake toolchains for each supported architecture

    Usage:
        xmake build-cross-tools -a x86_64 --buildtools=../buildtools
        xmake f -a x86_64   # Auto-detects cross-tools
        xmake
]]

-- ============================================================================
-- Supported Architectures
-- ============================================================================

local SUPPORTED_ARCHS = {
    x86_64  = { machine = "x86_64-unknown-haiku",  cpu = "x86_64" },
    x86     = { machine = "i586-unknown-haiku",    cpu = "i586" },
    arm64   = { machine = "aarch64-unknown-haiku", cpu = "aarch64" },
    arm     = { machine = "arm-unknown-haiku",     cpu = "arm" },
    riscv64 = { machine = "riscv64-unknown-haiku", cpu = "riscv64" },
    sparc   = { machine = "sparc64-unknown-haiku", cpu = "sparc64" },
    m68k    = { machine = "m68k-unknown-haiku",    cpu = "m68k" },
}

-- ============================================================================
-- Cross-Tools Detection
-- ============================================================================

-- Find cross-tools directory for an architecture
-- Checks spawned/ first (xmake output), then generated/ (jam output)
-- NOTE: Must be local to be captured by closure in toolchain callbacks
local function detect_cross_tools(arch)
    local arch_info = SUPPORTED_ARCHS[arch]
    if not arch_info then
        return nil, "Unsupported architecture: " .. tostring(arch)
    end

    local projectdir = os.projectdir()
    local search_paths = {
        path.join(projectdir, "spawned", "cross-tools-" .. arch),
        path.join(projectdir, "generated", "cross-tools-" .. arch),
    }

    for _, sdk_path in ipairs(search_paths) do
        local gcc_path = path.join(sdk_path, "bin", arch_info.machine .. "-gcc")
        if os.isfile(gcc_path) then
            return {
                sdkdir = sdk_path,
                bindir = path.join(sdk_path, "bin"),
                machine = arch_info.machine,
                gcc = gcc_path,
            }
        end
    end

    return nil, "Cross-tools not found for " .. arch .. ". Run: xmake build-cross-tools -a " .. arch
end

-- Detect GCC version from cross-tools
local function detect_gcc_version(cross_tools)
    if not cross_tools then return nil end

    local gcc = cross_tools.gcc
    local output = os.iorun(gcc .. " -dumpversion")
    if output then
        return output:trim()
    end
    return nil
end

-- Find libgcc and other GCC libraries
local function detect_gcc_libs(cross_tools, arch)
    if not cross_tools then return nil end

    local arch_info = SUPPORTED_ARCHS[arch]
    local version = detect_gcc_version(cross_tools)
    if not version then return nil end

    local lib_dir = path.join(cross_tools.sdkdir, "lib", "gcc", arch_info.machine, version)
    if os.isdir(lib_dir) then
        return {
            gcc_lib_dir = lib_dir,
            libgcc = path.join(lib_dir, "libgcc.a"),
            libsupcxx = path.join(cross_tools.sdkdir, arch_info.machine, "lib", "libsupc++.a"),
        }
    end
    return nil
end

-- ============================================================================
-- Toolchain Definitions
-- ============================================================================

-- Define toolchain for each architecture
for arch, arch_info in pairs(SUPPORTED_ARCHS) do
    toolchain("haiku-" .. arch)
        set_kind("cross")
        set_description("Haiku cross-compiler for " .. arch)

        on_check(function (toolchain)
            local cross_tools = detect_cross_tools(arch)
            return cross_tools ~= nil
        end)

        on_load(function (toolchain)
            local cross_tools = detect_cross_tools(arch)
            if not cross_tools then
                return
            end

            toolchain:set("sdkdir", cross_tools.sdkdir)
            toolchain:set("bindir", cross_tools.bindir)

            local prefix = arch_info.machine .. "-"
            toolchain:set("toolset", "cc", prefix .. "gcc")
            toolchain:set("toolset", "cxx", prefix .. "g++")
            toolchain:set("toolset", "ld", prefix .. "g++")
            toolchain:set("toolset", "ar", prefix .. "ar")
            toolchain:set("toolset", "strip", prefix .. "strip")
            toolchain:set("toolset", "ranlib", prefix .. "ranlib")
            toolchain:set("toolset", "objcopy", prefix .. "objcopy")
            toolchain:set("toolset", "as", prefix .. "as")

            -- Add library paths
            local gcc_libs = detect_gcc_libs(cross_tools, arch)
            if gcc_libs then
                toolchain:add("linkdirs", gcc_libs.gcc_lib_dir)
            end
        end)
    toolchain_end()
end

-- ============================================================================
-- Build Cross-Tools Task
-- ============================================================================

task("build-cross-tools")
    set_category("build")
    set_menu {
        usage = "xmake build-cross-tools [options]",
        description = "Build cross-compiler toolchain for Haiku",
        options = {
            {'a', "arch", "kv", "x86_64", "Target architecture (x86_64, x86, arm64, arm, riscv64)"},
            {'b', "buildtools", "kv", nil, "Path to buildtools source directory"},
            {'j', "jobs", "kv", nil, "Number of parallel jobs"},
            {nil, "with-gdb", "kv", nil, "Also build GDB (path to GDB source)"},
        }
    }

    on_run(function ()
        import("core.base.option")

        local arch = option.get("arch") or "x86_64"
        local buildtools_dir = option.get("buildtools")
        local jobs = option.get("jobs")
        local gdb_source = option.get("with-gdb")

        -- Validate architecture
        local arch_info = SUPPORTED_ARCHS[arch]
        if not arch_info then
            raise("Unsupported architecture: %s\nSupported: %s",
                arch, table.concat(table.keys(SUPPORTED_ARCHS), ", "))
        end

        -- Find buildtools directory
        if not buildtools_dir then
            -- Try common locations
            local projectdir = os.projectdir()
            local search_paths = {
                path.join(projectdir, "..", "buildtools"),
                path.join(projectdir, "buildtools"),
                path.join(projectdir, "..", "haiku-buildtools"),
            }
            for _, p in ipairs(search_paths) do
                if os.isdir(path.join(p, "gcc")) and os.isdir(path.join(p, "binutils")) then
                    buildtools_dir = p
                    break
                end
            end
        end

        if not buildtools_dir or not os.isdir(buildtools_dir) then
            raise("Buildtools directory not found.\n" ..
                  "Please specify with: xmake build-cross-tools -b /path/to/buildtools\n" ..
                  "Clone from: https://github.com/nicholasbishop/buildtools")
        end

        -- Verify buildtools contents
        if not os.isdir(path.join(buildtools_dir, "gcc")) then
            raise("Invalid buildtools directory: %s\nMissing 'gcc' subdirectory", buildtools_dir)
        end

        local projectdir = os.projectdir()
        local haiku_source = projectdir
        local install_dir = path.join(projectdir, "spawned", "cross-tools-" .. arch)
        local script = path.join(projectdir, "build", "scripts", "build_cross_tools_gcc4")

        -- Build command
        local cmd = string.format("%s %s %s %s %s",
            script,
            arch_info.machine,
            haiku_source,
            buildtools_dir,
            install_dir
        )

        -- Add parallel jobs
        if jobs then
            cmd = string.format("MAKE='make -j%s' %s", jobs, cmd)
        end

        -- Add GDB if requested
        if gdb_source then
            cmd = string.format("HAIKU_USE_GDB=%s %s", gdb_source, cmd)
        end

        print("Building cross-tools for %s...", arch)
        print("  Machine:    %s", arch_info.machine)
        print("  Buildtools: %s", buildtools_dir)
        print("  Install to: %s", install_dir)
        print("")

        -- Run the build
        os.mkdir(path.directory(install_dir))
        local ok = os.execv("bash", {"-c", cmd})

        if ok == 0 then
            print("")
            print("Cross-tools built successfully!")
            print("Now run: xmake f -a %s && xmake", arch)
        else
            raise("Failed to build cross-tools")
        end
    end)
task_end()

-- ============================================================================
-- Global Configuration Helper
-- ============================================================================

-- Get cross-tools configuration for current architecture
function get_cross_tools_config()
    local arch = get_config("arch") or "x86_64"
    local cross_tools, err = detect_cross_tools(arch)

    if not cross_tools then
        -- Not an error during configuration phase, just a warning
        if is_config("cross_tools_required", true) then
            raise(err)
        end
        return nil
    end

    local gcc_libs = detect_gcc_libs(cross_tools, arch)

    return {
        arch = arch,
        machine = SUPPORTED_ARCHS[arch].machine,
        sdkdir = cross_tools.sdkdir,
        bindir = cross_tools.bindir,
        gcc_version = detect_gcc_version(cross_tools),
        gcc_lib_dir = gcc_libs and gcc_libs.gcc_lib_dir,
        libgcc = gcc_libs and gcc_libs.libgcc,
        libsupcxx = gcc_libs and gcc_libs.libsupcxx,
    }
end

-- Export for use in other modules
return {
    SUPPORTED_ARCHS = SUPPORTED_ARCHS,
    detect_cross_tools = detect_cross_tools,
    detect_gcc_version = detect_gcc_version,
    detect_gcc_libs = detect_gcc_libs,
    get_cross_tools_config = get_cross_tools_config,
}