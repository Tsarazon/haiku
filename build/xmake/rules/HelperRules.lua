--[[
    HelperRules.lua - Pure utility functions for Haiku build system

    xmake equivalent of build/jam/HelperRules (1:1 migration)

    Functions exported (no build rules - pure helpers):
    - FIsPrefix(a, b)                    - Check if list a is a prefix of list b
    - FFilter(list, excludes)            - Remove excludes from list
    - FGetGrist(name)                    - Extract grist tag from "<grist>name"
    - FSplitString(str, delim)           - Split string by delimiter
    - FSplitPath(filepath)               - Decompose path into components
    - FConditionsHold(conditions, pred)  - Evaluate conditions with predicate
    - SetPlatformCompatibilityFlagVariables(platform, varPrefix, platformKind, otherPlatforms)
    - SetIncludePropertiesVariables(settings, suffix)
    - SetPlatformForTarget(target, platform)
    - SetSubDirPlatform(platform)
    - SetSupportedPlatformsForTarget(target, platforms)
    - SetSubDirSupportedPlatforms(platforms)
    - AddSubDirSupportedPlatforms(platforms)
    - IsPlatformSupportedForTarget(target)
    - InheritPlatform(children, parent)

    Usage from other rules:
        import("rules.HelperRules")
        local filtered = HelperRules.FFilter({"a", "b", "c"}, {"b"})
]]

-- Module-level state (equivalent of Jam global variables for current subdir)
local _current_platform = nil
local _supported_platforms = {}
local _subdir_asflags = {}

-- Platform compatibility flags storage
-- Key: varPrefix, Value: table of compatibility flags
local _platform_compat = {}

-- Include properties storage
-- Key: prefix .. suffix, Value: table {separator, local_option, system_option}
local _include_properties = {}

-- FIsPrefix: check if list a is a prefix of list b
-- Returns true if a is a prefix (proper or equal) of b, false otherwise
function FIsPrefix(a, b)
    if type(a) ~= "table" then a = {a} end
    if type(b) ~= "table" then b = {b} end
    for i, v in ipairs(a) do
        if v ~= b[i] then
            return false
        end
    end
    return true
end

-- FFilter: remove all occurrences of excludes from list
function FFilter(list, excludes)
    if not list then return {} end
    if not excludes then return list end

    local exclude_set = {}
    for _, e in ipairs(excludes) do
        exclude_set[e] = true
    end

    local result = {}
    for _, item in ipairs(list) do
        if not exclude_set[item] then
            table.insert(result, item)
        end
    end
    return result
end

-- FGetGrist: extract grist from Jam-style "<grist>name" string
-- Returns the grist without angle brackets, or nil if no grist
function FGetGrist(name)
    if not name then return nil end
    local grist = name:match("^<(.-)>")
    return grist
end

-- FSplitString: split a string by delimiter character
function FSplitString(str, delim)
    if not str or str == "" then return {} end
    delim = delim or "/"
    local result = {}
    -- Escape delimiter for pattern matching
    local pattern = "[^" .. delim:gsub("([%.%+%-%*%?%[%]%^%$%(%)%%])", "%%%1") .. "]+"
    for part in str:gmatch(pattern) do
        table.insert(result, part)
    end
    return result
end

-- FSplitPath: decompose a path into its components
function FSplitPath(filepath)
    if not filepath then return {} end
    -- Normalize separators
    filepath = filepath:gsub("\\", "/")
    local components = {}
    for part in filepath:gmatch("[^/]+") do
        table.insert(components, part)
    end
    -- Preserve leading "/" as first component
    if filepath:sub(1, 1) == "/" then
        if #components > 0 then
            components[1] = "/" .. components[1]
        else
            components = {"/"}
        end
    end
    return components
end

-- FConditionsHold: check whether conditions are satisfied by a predicate function
-- Conditions is a list of strings. Entries starting with "!" are negative conditions.
-- Positive conditions: at least one must hold (OR logic)
-- Negative conditions: none must hold (AND logic)
-- predicate(condition) should return true/false
function FConditionsHold(conditions, predicate)
    if not conditions or #conditions == 0 then
        return false
    end

    local has_positive = false
    local has_negative = false
    local positive_match = false

    for _, condition in ipairs(conditions) do
        if condition:sub(1, 1) == "!" then
            -- Negative condition
            has_negative = true
            local stripped = condition:sub(2)
            if predicate(stripped) then
                return false  -- negative condition holds -> fail
            end
        else
            -- Positive condition
            has_positive = true
            if predicate(condition) then
                positive_match = true
            end
        end
    end

    if has_positive then
        return positive_match
    end
    return has_negative
end

-- SetPlatformCompatibilityFlagVariables: determine platform compatibility
-- In Jam, this sets global variables like HOST_PLATFORM_HAIKU_COMPATIBLE.
-- In xmake, we store them in module-level tables.
function SetPlatformCompatibilityFlagVariables(platform, var_prefix, platform_kind, other_platforms)
    if not platform then
        raise("Platform variable not set. Please run configure or specify it manually.")
    end

    -- Special case: libbe_test uses host platform
    if platform == "libbe_test" then
        import("core.project.config")
        platform = config.get("host_platform") or "linux"
    end

    local compat = {
        haiku_compatible = false
    }

    if platform == "haiku_host" or platform == "haiku" then
        compat.haiku_compatible = true
    elseif platform == "host" then
        -- not compatible to anything
    else
        if other_platforms then
            local found = false
            for _, p in ipairs(other_platforms) do
                if p == platform then
                    found = true
                    break
                end
            end
            if not found then
                raise(string.format("Unsupported %s platform: %s", platform_kind, platform))
            end
        end
    end

    _platform_compat[var_prefix] = compat
    return compat
end

-- GetPlatformCompatibility: retrieve stored compatibility flags
function GetPlatformCompatibility(var_prefix)
    return _platform_compat[var_prefix]
end

-- SetIncludePropertiesVariables: set include path options based on GCC version
-- In Jam, sets variables like HOST_INCLUDES_SEPARATOR, HOST_LOCAL_INCLUDES_OPTION, etc.
-- In xmake, we store them for later use by compilation rules.
function SetIncludePropertiesVariables(settings, suffix)
    suffix = suffix or ""
    local key = (settings.prefix or "HAIKU") .. suffix

    local is_legacy_gcc = settings.is_legacy_gcc or false

    if is_legacy_gcc then
        _include_properties[key] = {
            separator = "-I-",
            local_option = "-I",
            system_option = "-I"
        }
    else
        _include_properties[key] = {
            separator = nil,
            local_option = "-iquote",
            system_option = "-I"
        }
    end

    return _include_properties[key]
end

-- GetIncludeProperties: retrieve stored include properties
function GetIncludeProperties(key)
    return _include_properties[key]
end

-- SetPlatformForTarget: set the platform for a specific target
function SetPlatformForTarget(target, platform)
    if type(target) == "string" then
        -- Deferred: will be applied when target is loaded
        return
    end
    target:data_set("haiku.platform", platform)
end

-- SetSubDirPlatform: set the platform for the current subdirectory context
function SetSubDirPlatform(platform)
    _current_platform = platform
end

-- GetSubDirPlatform: get current subdirectory platform
function GetSubDirPlatform()
    return _current_platform
end

-- SetSupportedPlatformsForTarget: set supported platforms for a target
function SetSupportedPlatformsForTarget(target, platforms)
    if type(target) == "string" then
        return
    end
    target:data_set("haiku.supported_platforms", platforms)
end

-- SetSubDirSupportedPlatforms: set supported platforms for current subdirectory
function SetSubDirSupportedPlatforms(platforms)
    _supported_platforms = platforms or {}
end

-- AddSubDirSupportedPlatforms: add to supported platforms for current subdirectory
function AddSubDirSupportedPlatforms(platforms)
    if platforms then
        for _, p in ipairs(platforms) do
            table.insert(_supported_platforms, p)
        end
    end
end

-- GetSubDirSupportedPlatforms: get supported platforms for current subdirectory
function GetSubDirSupportedPlatforms()
    return _supported_platforms
end

-- IsPlatformSupportedForTarget: check if target's platform is in its supported list
function IsPlatformSupportedForTarget(target)
    if type(target) == "string" then
        return true  -- can't check string targets
    end

    local platform = target:data("haiku.platform") or _current_platform
    local supported = target:data("haiku.supported_platforms") or _supported_platforms

    if not platform or not supported or #supported == 0 then
        return true  -- no restrictions
    end

    for _, p in ipairs(supported) do
        if p == platform then
            return true
        end
    end
    return false
end

-- InheritPlatform: copy platform and supported_platforms from parent to children
function InheritPlatform(children, parent)
    if type(parent) == "string" or type(children) ~= "table" then
        return
    end

    local platform = parent:data("haiku.platform")
    local supported = parent:data("haiku.supported_platforms")

    for _, child in ipairs(children) do
        if type(child) ~= "string" then
            if platform then
                child:data_set("haiku.platform", platform)
            end
            if supported then
                child:data_set("haiku.supported_platforms", supported)
            end
        end
    end
end

-- SubDirAsFlags: add assembly flags for current subdirectory
function SubDirAsFlags(flags)
    if type(flags) == "table" then
        for _, f in ipairs(flags) do
            table.insert(_subdir_asflags, f)
        end
    else
        table.insert(_subdir_asflags, flags)
    end
end

-- GetSubDirAsFlags: retrieve stored assembly flags
function GetSubDirAsFlags()
    return _subdir_asflags
end
