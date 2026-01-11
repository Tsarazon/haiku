--[[
    MiscRules.lua - Miscellaneous build rules

    xmake equivalent of build/jam/MiscRules

    Rules defined:
    - SetupObjectsDir           - Setup output directories for current subdir
    - SetupFeatureObjectsDir    - Setup feature-specific output directories
    - MakeLocateCommonPlatform  - Locate files shared between platforms
    - MakeLocatePlatform        - Locate platform-specific files
    - MakeLocateArch            - Locate architecture-specific files
    - MakeLocateDebug           - Locate debug-level-specific files
    - DeferredSubInclude        - Schedule deferred subdirectory include
    - ExecuteDeferredSubIncludes - Execute all deferred includes
    - HaikuSubInclude           - Relative subdirectory include
    - NextID                    - Generate unique ID
    - NewUniqueTarget           - Create unique target name
    - RunCommandLine            - Execute command with target substitution
    - DefineBuildProfile        - Define build profile (image, cd, vmware, etc.)
]]

-- import("core.project.config")

-- ============================================================================
-- Configuration Storage
-- ============================================================================

-- Object directories for different platforms/archs/debug levels
local _object_dirs = {
    common_platform = nil,
    host_common_arch = nil,
    target_common_arch = nil,
    host_common_debug = nil,
    target_common_debug = nil,
    host_debug = {},      -- by debug level
    target_debug = {}     -- by debug level
}

-- Locate targets for current directory
local _locate_targets = {
    common_platform = nil,
    host_common_arch = nil,
    target_common_arch = nil,
    host_common_debug = nil,
    target_common_debug = nil,
    host_debug = {},
    target_debug = {}
}

-- Current directory state
local _current_locate_target = nil
local _current_locate_source = nil
local _current_search_source = {}

-- Deferred includes
local _deferred_sub_includes = {}

-- Unique ID counter
local _next_id = 0

-- Build profiles
local _build_profiles = {}
local _current_build_profile = nil
local _build_profile_action = nil

-- Debug levels
local HAIKU_DEBUG_LEVELS = {0, 1}

-- ============================================================================
-- SetupObjectsDir
-- ============================================================================

--[[
    SetupObjectsDir(subdir_tokens)

    Setup LOCATE_TARGET, LOCATE_SOURCE, SEARCH_SOURCE variables
    for the current directory.

    Equivalent to Jam:
        rule SetupObjectsDir { }

    Parameters:
        subdir_tokens - List of subdirectory tokens (relative path components)
]]
function SetupObjectsDir(subdir_tokens)
    subdir_tokens = subdir_tokens or {}
    if type(subdir_tokens) == "string" then
        subdir_tokens = subdir_tokens:split("/")
    end

    -- Skip first token (usually "src" or root marker)
    local rel_tokens = {}
    for i = 2, #subdir_tokens do
        table.insert(rel_tokens, subdir_tokens[i])
    end

    local rel_path = table.concat(rel_tokens, "/")
    if rel_path == "" or rel_path == "." then
        rel_path = nil
    end

    local buildir = config.get("buildir") or "$(buildir)"
    local arch = config.get("arch") or "x86_64"

    -- Setup common platform locate target
    local common_platform_dir = path.join(buildir, "objects", "common")
    if rel_path then
        _locate_targets.common_platform = path.join(common_platform_dir, rel_path)
    else
        _locate_targets.common_platform = common_platform_dir
    end

    -- Setup host common arch
    local host_common_arch_dir = path.join(buildir, "objects", "host", arch)
    if rel_path then
        _locate_targets.host_common_arch = path.join(host_common_arch_dir, rel_path)
    else
        _locate_targets.host_common_arch = host_common_arch_dir
    end

    -- Setup target common arch
    local target_common_arch_dir = path.join(buildir, "objects", "haiku", arch)
    if rel_path then
        _locate_targets.target_common_arch = path.join(target_common_arch_dir, rel_path)
    else
        _locate_targets.target_common_arch = target_common_arch_dir
    end

    -- Setup debug-level directories
    for _, level in ipairs(HAIKU_DEBUG_LEVELS) do
        local host_debug_dir = path.join(buildir, "objects", "host", arch, "debug_" .. level)
        local target_debug_dir = path.join(buildir, "objects", "haiku", arch, "debug_" .. level)

        if rel_path then
            _locate_targets.host_debug[level] = path.join(host_debug_dir, rel_path)
            _locate_targets.target_debug[level] = path.join(target_debug_dir, rel_path)
        else
            _locate_targets.host_debug[level] = host_debug_dir
            _locate_targets.target_debug[level] = target_debug_dir
        end
    end

    -- Common debug (level 0)
    _locate_targets.host_common_debug = _locate_targets.host_debug[0]
    _locate_targets.target_common_debug = _locate_targets.target_debug[0]

    -- Set current locate targets
    _current_locate_target = _locate_targets.common_platform
    _current_locate_source = _current_locate_target

    -- Search source includes subdir and standard output dirs
    _current_search_source = {
        path.join(os.projectdir(), table.concat(subdir_tokens, "/")),
        _current_locate_source,
        _locate_targets.host_common_debug,
        _locate_targets.target_common_debug
    }
end

-- ============================================================================
-- SetupFeatureObjectsDir
-- ============================================================================

--[[
    SetupFeatureObjectsDir(feature)

    Updates locate/search directories appending a feature subdirectory.

    Equivalent to Jam:
        rule SetupFeatureObjectsDir { }

    Parameters:
        feature - Feature name to append to directories
]]
function SetupFeatureObjectsDir(feature)
    if not feature then return end

    -- Append feature to all locate targets
    _locate_targets.common_platform = path.join(_locate_targets.common_platform or "", feature)

    _locate_targets.host_common_arch = path.join(_locate_targets.host_common_arch or "", feature)
    _locate_targets.target_common_arch = path.join(_locate_targets.target_common_arch or "", feature)

    _locate_targets.host_common_debug = path.join(_locate_targets.host_common_debug or "", feature)
    _locate_targets.target_common_debug = path.join(_locate_targets.target_common_debug or "", feature)

    for level, dir in pairs(_locate_targets.host_debug) do
        _locate_targets.host_debug[level] = path.join(dir, feature)
    end
    for level, dir in pairs(_locate_targets.target_debug) do
        _locate_targets.target_debug[level] = path.join(dir, feature)
    end

    -- Update current
    _current_locate_target = path.join(_current_locate_target or "", feature)
    _current_locate_source = _current_locate_target

    -- Rebuild search source
    local subdir = _current_search_source[1]
    _current_search_source = {
        subdir,
        _current_locate_source,
        _locate_targets.host_common_debug,
        _locate_targets.target_common_debug
    }
end

-- ============================================================================
-- MakeLocate Variants
-- ============================================================================

--[[
    MakeLocateCommonPlatform(files, subdir)

    Locate files in common platform directory (shared between all targets).

    Equivalent to Jam:
        rule MakeLocateCommonPlatform { }

    Parameters:
        files - Files to locate
        subdir - Optional subdirectory

    Returns:
        Output directory path
]]
function MakeLocateCommonPlatform(files, subdir)
    local dir = _locate_targets.common_platform
    if subdir then
        dir = path.join(dir, subdir)
    end
    os.mkdir(dir)
    return dir
end

--[[
    MakeLocatePlatform(files, subdir, platform)

    Locate files in platform-specific directory (arch-independent).

    Equivalent to Jam:
        rule MakeLocatePlatform { }

    Parameters:
        files - Files to locate
        subdir - Optional subdirectory
        platform - "host" or "target" (default: "target")

    Returns:
        Output directory path
]]
function MakeLocatePlatform(files, subdir, platform)
    platform = platform or "target"

    local dir
    if platform == "host" then
        dir = _locate_targets.host_common_arch
    else
        dir = _locate_targets.target_common_arch
    end

    if subdir then
        dir = path.join(dir, subdir)
    end
    os.mkdir(dir)
    return dir
end

--[[
    MakeLocateArch(files, subdir, platform)

    Locate files in architecture-specific directory (debug-independent).

    Equivalent to Jam:
        rule MakeLocateArch { }

    Parameters:
        files - Files to locate
        subdir - Optional subdirectory
        platform - "host" or "target" (default: "target")

    Returns:
        Output directory path
]]
function MakeLocateArch(files, subdir, platform)
    platform = platform or "target"

    local dir
    if platform == "host" then
        dir = _locate_targets.host_common_debug
    else
        dir = _locate_targets.target_common_debug
    end

    if subdir then
        dir = path.join(dir, subdir)
    end
    os.mkdir(dir)
    return dir
end

--[[
    MakeLocateDebug(files, subdir, platform, debug_level)

    Locate files in debug-level-specific directory.

    Equivalent to Jam:
        rule MakeLocateDebug { }

    Parameters:
        files - Files to locate
        subdir - Optional subdirectory
        platform - "host" or "target" (default: "target")
        debug_level - Debug level (default: 0)

    Returns:
        Output directory path
]]
function MakeLocateDebug(files, subdir, platform, debug_level)
    platform = platform or "target"
    debug_level = debug_level or 0

    local dir
    if platform == "host" then
        dir = _locate_targets.host_debug[debug_level] or _locate_targets.host_common_debug
    else
        dir = _locate_targets.target_debug[debug_level] or _locate_targets.target_common_debug
    end

    if subdir then
        dir = path.join(dir, subdir)
    end
    os.mkdir(dir)
    return dir
end

-- ============================================================================
-- Deferred SubIncludes
-- ============================================================================

--[[
    DeferredSubInclude(params, jamfile, scope)

    Schedule a subdirectory for deferred inclusion.

    Equivalent to Jam:
        rule DeferredSubInclude { }

    Parameters:
        params - Subdirectory tokens
        jamfile - Alternative Jamfile name (default: "Jamfile")
        scope - "global" or "local" (default: "global")
]]
function DeferredSubInclude(params, jamfile, scope)
    if type(params) == "string" then
        params = params:split("/")
    end

    table.insert(_deferred_sub_includes, {
        tokens = params,
        jamfile = jamfile,
        scope = scope or "global"
    })
end

--[[
    ExecuteDeferredSubIncludes()

    Execute all deferred SubIncludes.

    Equivalent to Jam:
        rule ExecuteDeferredSubIncludes { }
]]
function ExecuteDeferredSubIncludes()
    for _, entry in ipairs(_deferred_sub_includes) do
        local subdir = path.join(os.projectdir(), table.concat(entry.tokens, "/"))
        local xmake_file = path.join(subdir, "xmake.lua")

        if os.isfile(xmake_file) then
            includes(xmake_file)
        end
    end

    -- Clear after execution
    _deferred_sub_includes = {}
end

--[[
    HaikuSubInclude(tokens)

    Include a subdirectory relative to current directory.

    Equivalent to Jam:
        rule HaikuSubInclude { }

    Parameters:
        tokens - Subdirectory tokens (relative to current)
]]
function HaikuSubInclude(tokens)
    if type(tokens) == "string" then
        tokens = {tokens}
    end

    if #tokens > 0 then
        local subdir = path.join(os.scriptdir(), table.concat(tokens, "/"))
        local xmake_file = path.join(subdir, "xmake.lua")

        if os.isfile(xmake_file) then
            includes(xmake_file)
        end
    end
end

-- ============================================================================
-- Unique IDs/Targets
-- ============================================================================

--[[
    NextID()

    Generate a unique ID.

    Equivalent to Jam:
        rule NextID { }

    Returns:
        Unique integer ID
]]
function NextID()
    local id = _next_id
    _next_id = _next_id + 1
    return id
end

--[[
    NewUniqueTarget(basename)

    Create a unique target name.

    Equivalent to Jam:
        rule NewUniqueTarget { }

    Parameters:
        basename - Base name for target (default: "_target")

    Returns:
        Unique target name
]]
function NewUniqueTarget(basename)
    basename = basename or "_target"
    local id = NextID()
    return string.format("%s_%d", basename, id)
end

-- ============================================================================
-- RunCommandLine
-- ============================================================================

--[[
    RunCommandLine(command_line)

    Execute a command line with target path substitution.
    Elements prefixed with ":" are treated as build targets.

    Equivalent to Jam:
        rule RunCommandLine { }
        actions RunCommandLine1 { }

    Parameters:
        command_line - Command line elements (list or string)

    Returns:
        Pseudo target name
]]
function RunCommandLine(command_line)
    if type(command_line) == "string" then
        command_line = command_line:split(" ")
    end

    -- Collect targets and substitute
    local substituted = {}
    local targets = {}
    local var_index = 0

    for _, item in ipairs(command_line) do
        local target = item:match("^:(.+)$")
        if target then
            table.insert(targets, target)
            var_index = var_index + 1
            table.insert(substituted, "${target" .. var_index .. "}")
        else
            table.insert(substituted, item)
        end
    end

    -- Create unique run target
    local run_target = NewUniqueTarget("run")

    -- Build the command with variable assignments
    local cmd_parts = {}

    for i, t in ipairs(targets) do
        -- Resolve target path (simplified - in real use would need target resolution)
        table.insert(cmd_parts, string.format('target%d="%s"', i, t))
    end

    table.insert(cmd_parts, table.concat(substituted, " "))

    local full_cmd = table.concat(cmd_parts, "; ")

    return {
        target = run_target,
        command = full_cmd,
        dependencies = targets,
        execute = function()
            os.exec(full_cmd)
        end
    }
end

-- ============================================================================
-- DefineBuildProfile
-- ============================================================================

--[[
    DefineBuildProfile(name, profile_type, path_value)

    Define a build profile for image/installation creation.

    Equivalent to Jam:
        rule DefineBuildProfile { }

    Parameters:
        name - Profile name
        profile_type - One of: "image", "anyboot-image", "cd-image",
                       "vmware-image", "haiku-mmc-image", "disk", "install", "custom"
        path_value - Path to image/installation directory

    Returns:
        true if profile is active, false otherwise
]]
function DefineBuildProfile(name, profile_type, path_value)
    -- Check for duplicate
    if _build_profiles[name] then
        error(string.format('Build profile "%s" defined twice!', name))
    end

    _build_profiles[name] = {
        name = name,
        type = profile_type,
        path = path_value
    }

    -- If this isn't the current profile, skip setup
    if _current_build_profile ~= name then
        return false
    end

    -- Get default directories
    local buildir = config.get("buildir") or "$(buildir)"
    local arch = config.get("arch") or "x86_64"

    -- Split path into directory and name
    local target_dir = path_value and path.directory(path_value)
    local target_name = path_value and path.basename(path_value)

    if target_dir == "" then target_dir = nil end
    if target_name == "" then target_name = nil end

    -- Default image directory
    target_dir = target_dir or path.join(buildir, "images")

    -- Handle "disk" as "image" with special flag
    local dont_clear_image = false
    if profile_type == "disk" then
        profile_type = "image"
        dont_clear_image = true
    end

    local build_target
    local start_offset

    -- Configure based on type
    if profile_type == "anyboot-image" then
        target_name = target_name or "haiku-anyboot.image"
        build_target = "haiku-anyboot-image"

    elseif profile_type == "cd-image" then
        target_name = target_name or "haiku.iso"
        build_target = "haiku-cd"

    elseif profile_type == "image" then
        target_name = target_name or "haiku.image"
        build_target = "haiku-image"

    elseif profile_type == "haiku-mmc-image" then
        target_name = target_name or "haiku-mmc.image"
        build_target = "haiku-mmc-image"

    elseif profile_type == "vmware-image" then
        target_name = target_name or "haiku.vmdk"
        build_target = "haiku-vmware-image"
        start_offset = 65536

    elseif profile_type == "install" then
        target_dir = path_value or path.join(buildir, "install")
        build_target = "install-haiku"

    elseif profile_type == "custom" then
        return true

    else
        error(string.format("Unsupported build profile type: %s", profile_type))
    end

    -- Store configuration
    _build_profiles[name].target_dir = target_dir
    _build_profiles[name].target_name = target_name
    _build_profiles[name].build_target = build_target
    _build_profiles[name].dont_clear_image = dont_clear_image
    _build_profiles[name].start_offset = start_offset

    return true
end

-- ============================================================================
-- Build Profile Accessors
-- ============================================================================

--[[
    SetBuildProfile(name)

    Set the current build profile.

    Parameters:
        name - Profile name
]]
function SetBuildProfile(name)
    _current_build_profile = name
end

--[[
    GetBuildProfile()

    Get the current build profile name.

    Returns:
        Current profile name or nil
]]
function GetBuildProfile()
    return _current_build_profile
end

--[[
    SetBuildProfileAction(action)

    Set the build profile action.

    Parameters:
        action - One of: "build", "update", "update-all", "update-packages",
                 "build-package-list", "mount"
]]
function SetBuildProfileAction(action)
    _build_profile_action = action
end

--[[
    GetBuildProfileAction()

    Get the current build profile action.

    Returns:
        Current action or nil
]]
function GetBuildProfileAction()
    return _build_profile_action
end

--[[
    GetBuildProfileConfig(name)

    Get configuration for a build profile.

    Parameters:
        name - Profile name (default: current profile)

    Returns:
        Profile configuration table or nil
]]
function GetBuildProfileConfig(name)
    name = name or _current_build_profile
    return _build_profiles[name]
end

-- ============================================================================
-- Accessor Functions for Directories
-- ============================================================================

--[[
    GetLocateTarget()

    Get current LOCATE_TARGET directory.
]]
function GetLocateTarget()
    return _current_locate_target
end

--[[
    GetLocateSource()

    Get current LOCATE_SOURCE directory.
]]
function GetLocateSource()
    return _current_locate_source
end

--[[
    GetSearchSource()

    Get current SEARCH_SOURCE directories.
]]
function GetSearchSource()
    return _current_search_source
end

--[[
    GetLocateTargets()

    Get all locate target directories.
]]
function GetLocateTargets()
    return _locate_targets
end

-- ============================================================================
-- Reset Functions (for testing)
-- ============================================================================

--[[
    ResetMiscRules()

    Reset all state (for testing).
]]
function ResetMiscRules()
    _locate_targets = {
        common_platform = nil,
        host_common_arch = nil,
        target_common_arch = nil,
        host_common_debug = nil,
        target_common_debug = nil,
        host_debug = {},
        target_debug = {}
    }
    _current_locate_target = nil
    _current_locate_source = nil
    _current_search_source = {}
    _deferred_sub_includes = {}
    _next_id = 0
    _build_profiles = {}
    _current_build_profile = nil
    _build_profile_action = nil
end