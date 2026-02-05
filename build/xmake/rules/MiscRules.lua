--[[
    MiscRules.lua - Miscellaneous Haiku build rules

    xmake equivalent of build/jam/MiscRules (1:1 migration)

    Functions exported:
    - SetupObjectsDir(target, subdir_tokens)   - Set up output directories for a subdirectory
    - SetupFeatureObjectsDir(target, feature)  - Append feature to output directories
    - MakeLocateCommonPlatform(subdir)         - Output path for platform-shared files
    - MakeLocatePlatform(platform, subdir)     - Output path for platform-specific files
    - MakeLocateArch(platform, subdir)         - Output path for arch-specific files
    - MakeLocateDebug(platform, debug, subdir) - Output path for debug-level-specific files
    - NextID()                                 - Generate unique sequential ID
    - NewUniqueTarget(basename)                - Create unique target name
    - RunCommandLine(target, command_line)      - Run a shell command as build action
    - DefineBuildProfile(name, type, path)     - Define a build profile
    - DeferredSubInclude(params)               - Schedule deferred inclusion
    - ExecuteDeferredSubIncludes()             - Execute all deferred includes

    Usage:
        import("rules.MiscRules")
        local id = MiscRules.NextID()
        local dir = MiscRules.MakeLocateCommonPlatform("mysubdir")
]]

-- Unique ID counter
local _next_id = 0

-- Deferred includes list
local _deferred_includes = {}

-- Build profiles registry
local _build_profiles = {}

-- NextID: generate a unique sequential ID
function NextID()
    local result = _next_id
    _next_id = _next_id + 1
    return tostring(result)
end

-- NewUniqueTarget: create a unique target name
function NewUniqueTarget(basename)
    basename = basename or "_target"
    return string.format("%s_%s", basename, NextID())
end

-- SetupObjectsDir: set up output directory paths for a subdirectory
-- Returns a table with computed directory paths
function SetupObjectsDir(subdir_tokens)
    import("core.project.config")

    local haiku_top = config.get("haiku_top") or os.projectdir()
    local output_dir = config.get("build_output_dir") or path.join(haiku_top, "generated")

    -- Compute relative path from subdir tokens
    local rel_path = ""
    if subdir_tokens and #subdir_tokens > 1 then
        local parts = {}
        for i = 2, #subdir_tokens do
            table.insert(parts, subdir_tokens[i])
        end
        rel_path = table.concat(parts, "/")
    end

    local dirs = {
        common_platform = path.join(output_dir, "common_platform", rel_path),
        host_common_arch = path.join(output_dir, "host", "common", rel_path),
        target_common_arch = path.join(output_dir, "target", "common", rel_path),
        host_common_debug = path.join(output_dir, "host", "common_debug", rel_path),
        target_common_debug = path.join(output_dir, "target", "common_debug", rel_path),
    }

    -- Debug levels
    local debug_levels = config.get("haiku_debug_levels") or {0, 1}
    for _, level in ipairs(debug_levels) do
        local key = string.format("host_debug_%d", level)
        dirs[key] = path.join(output_dir, "host", "debug_" .. tostring(level), rel_path)
        key = string.format("target_debug_%d", level)
        dirs[key] = path.join(output_dir, "target", "debug_" .. tostring(level), rel_path)
    end

    -- Default locate/search targets
    dirs.locate_target = dirs.common_platform
    dirs.locate_source = dirs.locate_target

    return dirs
end

-- SetupFeatureObjectsDir: append feature subdirectory to output dirs
function SetupFeatureObjectsDir(dirs, feature)
    local result = {}
    for key, dir in pairs(dirs) do
        result[key] = path.join(dir, feature)
    end
    return result
end

-- MakeLocateCommonPlatform: output path for platform-shared files
function MakeLocateCommonPlatform(subdir)
    import("core.project.config")
    local output_dir = config.get("build_output_dir") or
        path.join(config.get("haiku_top") or os.projectdir(), "generated")
    local base = path.join(output_dir, "common_platform")
    if subdir then
        return path.join(base, subdir)
    end
    return base
end

-- MakeLocatePlatform: output path for platform-specific files
function MakeLocatePlatform(platform, subdir)
    import("core.project.config")
    local output_dir = config.get("build_output_dir") or
        path.join(config.get("haiku_top") or os.projectdir(), "generated")

    local base
    if platform == "host" then
        base = path.join(output_dir, "host", "common")
    else
        base = path.join(output_dir, "target", "common")
    end

    if subdir then
        return path.join(base, subdir)
    end
    return base
end

-- MakeLocateArch: output path for arch-specific files
function MakeLocateArch(platform, subdir)
    import("core.project.config")
    local output_dir = config.get("build_output_dir") or
        path.join(config.get("haiku_top") or os.projectdir(), "generated")

    local base
    if platform == "host" then
        base = path.join(output_dir, "host", "common_debug")
    else
        base = path.join(output_dir, "target", "common_debug")
    end

    if subdir then
        return path.join(base, subdir)
    end
    return base
end

-- MakeLocateDebug: output path for debug-level-specific files
function MakeLocateDebug(platform, debug_level, subdir)
    import("core.project.config")
    local output_dir = config.get("build_output_dir") or
        path.join(config.get("haiku_top") or os.projectdir(), "generated")

    debug_level = debug_level or 0
    local base
    if platform == "host" then
        base = path.join(output_dir, "host", "debug_" .. tostring(debug_level))
    else
        base = path.join(output_dir, "target", "debug_" .. tostring(debug_level))
    end

    if subdir then
        return path.join(base, subdir)
    end
    return base
end

-- DeferredSubInclude: schedule a subdirectory for later inclusion
function DeferredSubInclude(params, jamfile, scope)
    table.insert(_deferred_includes, {
        params = params,
        jamfile = jamfile,
        scope = scope or "global"
    })
end

-- ExecuteDeferredSubIncludes: execute all deferred includes
-- In xmake, this maps to calling includes() for each deferred entry
function ExecuteDeferredSubIncludes()
    import("core.project.config")
    local haiku_top = config.get("haiku_top") or os.projectdir()

    for _, entry in ipairs(_deferred_includes) do
        local subdir = path.join(haiku_top, table.unpack(entry.params))
        local jamfile = entry.jamfile or "xmake.lua"
        local filepath = path.join(subdir, jamfile)
        if os.isfile(filepath) then
            includes(filepath)
        end
    end
end

-- GetDeferredSubIncludes: return list of deferred includes
function GetDeferredSubIncludes()
    return _deferred_includes
end

-- RunCommandLine: run a shell command as a build step
-- command_parts is a list of strings; entries prefixed with ":" are target paths
function RunCommandLine(target, command_parts)
    import("core.project.depend")

    -- Separate targets (prefixed with ":") from regular command parts
    local deps = {}
    local cmd_line = {}
    for _, part in ipairs(command_parts) do
        if part:sub(1, 1) == ":" then
            local dep = part:sub(2)
            table.insert(deps, dep)
            table.insert(cmd_line, dep)
        else
            table.insert(cmd_line, part)
        end
    end

    local cmd_str = table.concat(cmd_line, " ")

    depend.on_changed(function()
        os.exec(cmd_str)
    end, {
        files = deps,
        dependfile = target:dependfile(NewUniqueTarget("run"))
    })
end

-- DefineBuildProfile: define a build profile
function DefineBuildProfile(name, profile_type, profile_path)
    import("core.project.config")

    if _build_profiles[name] then
        raise(string.format("ERROR: Build profile \"%s\" defined twice!", name))
    end

    local haiku_top = config.get("haiku_top") or os.projectdir()

    -- Defaults for path
    local target_dir = profile_path and path.directory(profile_path) or nil
    local target_name = profile_path and path.filename(profile_path) or nil

    target_dir = target_dir or config.get("haiku_image_dir")
        or path.join(haiku_top, "generated")

    -- "disk" is "image" with no-clear
    local dont_clear = false
    if profile_type == "disk" then
        profile_type = "image"
        dont_clear = true
    end

    local profile = {
        name = name,
        type = profile_type,
        target_dir = target_dir,
        target_name = target_name,
        dont_clear = dont_clear,
    }

    -- Set type-specific defaults
    if profile_type == "anyboot-image" then
        profile.target_name = profile.target_name
            or config.get("haiku_anyboot_name")
            or "haiku-anyboot.iso"
        profile.build_target = "haiku-anyboot-image"
    elseif profile_type == "cd-image" then
        profile.target_name = profile.target_name
            or config.get("haiku_cd_name")
            or "haiku-cd.iso"
        profile.build_target = "haiku-cd"
    elseif profile_type == "image" then
        profile.target_name = profile.target_name
            or config.get("haiku_image_name")
            or "haiku.image"
        profile.build_target = "haiku-image"
    elseif profile_type == "haiku-mmc-image" then
        profile.target_name = profile.target_name
            or config.get("haiku_mmc_image_name")
            or "haiku-mmc.image"
        profile.build_target = "haiku-mmc-image"
    elseif profile_type == "vmware-image" then
        profile.target_name = profile.target_name
            or config.get("haiku_vmware_image_name")
            or "haiku.vmdk"
        profile.build_target = "haiku-vmware-image"
        profile.start_offset = 65536
    elseif profile_type == "install" then
        profile.install_dir = profile_path
            or config.get("haiku_install_dir")
            or "/Haiku"
        profile.build_target = "install-haiku"
    elseif profile_type == "custom" then
        -- user-defined, no defaults
    else
        raise("Unsupported build profile type: " .. tostring(profile_type))
    end

    _build_profiles[name] = profile
    return profile
end

-- GetBuildProfile: retrieve a defined build profile
function GetBuildProfile(name)
    return _build_profiles[name]
end

-- GetBuildProfiles: retrieve all build profiles
function GetBuildProfiles()
    return _build_profiles
end
