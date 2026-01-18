--[[
    HelperRules.lua - Haiku helper rules and utility functions

    xmake equivalent of build/jam/HelperRules

    Functions defined:
    - FIsPrefix            - Check if list is prefix of another
    - FFilter              - Remove items from list
    - FGetGrist            - Extract grist from target name
    - FSplitString         - Split string by delimiter
    - FSplitPath           - Decompose path into components
    - FConditionsHold      - Check if conditions are satisfied
    - SetPlatformCompatibilityFlagVariables - Set platform compat flags
    - SetIncludePropertiesVariables - Set include options for compiler
    - SetPlatformForTarget - Set platform on target
    - SetSubDirPlatform    - Set platform for subdirectory
    - SetSupportedPlatformsForTarget - Set supported platforms
    - SetSubDirSupportedPlatforms - Set subdir supported platforms
    - AddSubDirSupportedPlatforms - Add supported platforms
    - IsPlatformSupportedForTarget - Check platform support
    - InheritPlatform      - Inherit platform from parent
    - SubDirAsFlags        - Add subdirectory assembler flags

    These are utility functions without side effects, used throughout
    the build system for list manipulation, path handling, and
    platform management.
]]

-- Note: import() should be called inside functions, not at top level
-- when used in files loaded via includes()

-- ============================================================================
-- List Manipulation Functions
-- ============================================================================

--[[
    FIsPrefix(a, b)

    Returns true if list 'a' is a prefix (proper or equal) of list 'b'.

    Equivalent to Jam:
        rule FIsPrefix { }

    Examples:
        FIsPrefix({"a", "b"}, {"a", "b", "c"}) -> true
        FIsPrefix({"a", "c"}, {"a", "b", "c"}) -> false
        FIsPrefix({}, {"a", "b"}) -> true
]]
function FIsPrefix(a, b)
    if type(a) ~= "table" then a = {a} end
    if type(b) ~= "table" then b = {b} end

    local i = 1
    while i <= #a and i <= #b and a[i] == b[i] do
        i = i + 1
    end

    -- 'a' is a prefix if we consumed all of it
    return i > #a
end

--[[
    FFilter(list, excludes)

    Removes all occurrences of items in 'excludes' from 'list'.

    Equivalent to Jam:
        rule FFilter { }

    Example:
        FFilter({"a", "b", "c", "d"}, {"b", "d"}) -> {"a", "c"}
]]
function FFilter(list, excludes)
    if type(list) ~= "table" then list = {list} end
    if type(excludes) ~= "table" then excludes = {excludes} end

    local result = {}
    local exclude_set = {}

    -- Build exclude set for O(1) lookup
    for _, item in ipairs(excludes) do
        exclude_set[item] = true
    end

    for _, item in ipairs(list) do
        if not exclude_set[item] then
            table.insert(result, item)
        end
    end

    return result
end

-- ============================================================================
-- Path and String Functions
-- ============================================================================

--[[
    FGetGrist(target)

    Returns the grist of a target, not including leading "<" and trailing ">".
    In Jam, grist is a namespace prefix like <build>tool.

    Equivalent to Jam:
        rule FGetGrist { }

    Example:
        FGetGrist("<build>rc") -> "build"
        FGetGrist("plain_target") -> nil
]]
function FGetGrist(target)
    if not target then return nil end

    local grist = target:match("^<([^>]+)>")
    return grist
end

--[[
    FSplitString(str, delimiter)

    Splits a string by delimiter character.

    Equivalent to Jam:
        rule FSplitString { }

    Example:
        FSplitString("a/b/c", "/") -> {"a", "b", "c"}
]]
function FSplitString(str, delimiter)
    if not str or str == "" then return {} end

    delimiter = delimiter or " "
    local result = {}

    -- Escape special pattern characters in delimiter
    local escaped_delim = delimiter:gsub("([%.%-%+%*%?%^%$%(%)%[%]%%])", "%%%1")
    local pattern = "[^" .. escaped_delim .. "]+"

    for part in str:gmatch(pattern) do
        table.insert(result, part)
    end

    return result
end

--[[
    FSplitPath(filepath)

    Decomposes a path into its components.

    Equivalent to Jam:
        rule FSplitPath { }

    Example:
        FSplitPath("/home/user/file.txt") -> {"/", "home", "user", "file.txt"}
        FSplitPath("relative/path/file") -> {"relative", "path", "file"}
]]
function FSplitPath(filepath)
    if not filepath or filepath == "" then return {} end

    local components = {}

    -- Handle absolute paths
    if filepath:sub(1, 1) == "/" then
        table.insert(components, "/")
        filepath = filepath:sub(2)
    end

    -- Split remaining path
    for part in filepath:gmatch("[^/]+") do
        table.insert(components, part)
    end

    return components
end

-- ============================================================================
-- Condition Evaluation
-- ============================================================================

--[[
    FConditionsHold(conditions, predicate)

    Checks whether conditions are satisfied by a predicate function.
    Returns true if:
    - None of the negative conditions (starting with "!") hold
    - If there are positive conditions, at least one holds

    Equivalent to Jam:
        rule FConditionsHold { }

    Examples:
        For predicate that returns true for {"a", "b", "c"}:
        - {"a"} -> true
        - {"!d"} -> true
        - {"d"} -> false
        - {"!a"} -> false
        - {"a", "!d"} -> true

    Parameters:
        conditions - List of condition strings
        predicate - Function(condition) -> boolean
]]
function FConditionsHold(conditions, predicate)
    if type(conditions) ~= "table" then conditions = {conditions} end
    if not conditions or #conditions == 0 then return false end

    local has_positive = false
    local has_negative = false
    local positive_match = false

    for _, condition in ipairs(conditions) do
        if condition:sub(1, 1) == "!" then
            -- Negative condition
            has_negative = true
            local cond_value = condition:sub(2)
            if predicate(cond_value) then
                -- Negative condition holds -> fail
                return false
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

-- ============================================================================
-- Platform Configuration
-- ============================================================================

-- Global platform variables (set via SetSubDirPlatform)
local _subdir_platform = nil
local _subdir_supported_platforms = {}
local _subdir_asflags = {}

--[[
    SetPlatformCompatibilityFlagVariables(platform, var_prefix, platform_kind, other_platforms)

    Sets platform compatibility flag variables based on the platform.

    Equivalent to Jam:
        rule SetPlatformCompatibilityFlagVariables { }

    Parameters:
        platform - Platform name (haiku, haiku_host, host, etc.)
        var_prefix - Variable prefix for flag names
        platform_kind - Platform kind description for error messages
        other_platforms - Additional valid platforms (optional)

    Returns:
        Table with compatibility flags
]]
function SetPlatformCompatibilityFlagVariables(platform, var_prefix, platform_kind, other_platforms)
    other_platforms = other_platforms or {}

    if not platform then
        error(format("Platform not set. Please run ./configure or specify it manually."))
    end

    local flags = {}
    flags.PLATFORM_HAIKU_COMPATIBLE = false

    -- Special case: libbe_test
    if platform == "libbe_test" then
        platform = get_config("host_platform") or "host"
    end

    if platform == "haiku_host" or platform == "haiku" then
        flags.PLATFORM_HAIKU_COMPATIBLE = true
    elseif platform == "host" then
        -- Not compatible to anything
        flags.PLATFORM_HAIKU_COMPATIBLE = false
    else
        -- Check if it's in other_platforms
        local found = false
        for _, p in ipairs(other_platforms) do
            if p == platform then
                found = true
                break
            end
        end
        if not found then
            error(format("Unsupported %s platform: %s", platform_kind, platform))
        end
    end

    return flags
end

--[[
    SetIncludePropertiesVariables(prefix, is_legacy_gcc)

    Sets include path option variables based on compiler type.
    Legacy GCC uses -I- separator, modern GCC uses -iquote.

    Equivalent to Jam:
        rule SetIncludePropertiesVariables { }

    Parameters:
        prefix - Variable prefix
        is_legacy_gcc - Boolean, true if using legacy GCC (< 4.x)

    Returns:
        Table with include options
]]
function SetIncludePropertiesVariables(prefix, is_legacy_gcc)
    local options = {}

    if is_legacy_gcc then
        options.INCLUDES_SEPARATOR = "-I-"
        options.LOCAL_INCLUDES_OPTION = "-I"
        options.SYSTEM_INCLUDES_OPTION = "-I"
    else
        options.INCLUDES_SEPARATOR = nil
        options.LOCAL_INCLUDES_OPTION = "-iquote "
        options.SYSTEM_INCLUDES_OPTION = "-I "
    end

    return options
end

-- ============================================================================
-- Target Platform Functions
-- ============================================================================

--[[
    SetPlatformForTarget(target, platform)

    Sets the platform for a specific target.

    Equivalent to Jam:
        rule SetPlatformForTarget { }
]]
function SetPlatformForTarget(target, platform)
    if type(target) == "string" then
        -- Store in target values when target is created
        -- This will be applied via on_load
        return {platform = platform}
    else
        target:set("platform", platform)
    end
end

--[[
    SetSubDirPlatform(platform)

    Sets the default platform for the current subdirectory.

    Equivalent to Jam:
        rule SetSubDirPlatform { }
]]
function SetSubDirPlatform(platform)
    _subdir_platform = platform
end

--[[
    GetSubDirPlatform()

    Gets the current subdirectory platform.
]]
function GetSubDirPlatform()
    return _subdir_platform
end

--[[
    SetSupportedPlatformsForTarget(target, platforms)

    Sets the supported platforms for a target.

    Equivalent to Jam:
        rule SetSupportedPlatformsForTarget { }
]]
function SetSupportedPlatformsForTarget(target, platforms)
    if type(platforms) ~= "table" then platforms = {platforms} end

    if type(target) == "string" then
        return {supported_platforms = platforms}
    else
        target:set("supported_platforms", platforms)
    end
end

--[[
    SetSubDirSupportedPlatforms(platforms)

    Sets supported platforms for the current subdirectory.

    Equivalent to Jam:
        rule SetSubDirSupportedPlatforms { }
]]
function SetSubDirSupportedPlatforms(platforms)
    if type(platforms) ~= "table" then platforms = {platforms} end
    _subdir_supported_platforms = platforms
end

--[[
    AddSubDirSupportedPlatforms(platforms)

    Adds to supported platforms for the current subdirectory.

    Equivalent to Jam:
        rule AddSubDirSupportedPlatforms { }
]]
function AddSubDirSupportedPlatforms(platforms)
    if type(platforms) ~= "table" then platforms = {platforms} end
    for _, p in ipairs(platforms) do
        table.insert(_subdir_supported_platforms, p)
    end
end

--[[
    GetSubDirSupportedPlatforms()

    Gets the current subdirectory supported platforms.
]]
function GetSubDirSupportedPlatforms()
    return _subdir_supported_platforms
end

--[[
    IsPlatformSupportedForTarget(target, platform)

    Checks if a platform is supported for the given target.

    Equivalent to Jam:
        rule IsPlatformSupportedForTarget { }

    Parameters:
        target - Target object or target settings table
        platform - Platform to check (optional, uses target's platform if not specified)

    Returns:
        true if platform is supported, false otherwise
]]
function IsPlatformSupportedForTarget(target, platform)
    local target_platform
    local supported_platforms

    if type(target) == "table" and not target.targetfile then
        -- Settings table from SetPlatformForTarget
        target_platform = target.platform or platform
        supported_platforms = target.supported_platforms or {}
    else
        -- xmake target object
        target_platform = platform or target:get("platform")
        supported_platforms = target:get("supported_platforms") or {}
    end

    if not target_platform then
        return false
    end

    for _, p in ipairs(supported_platforms) do
        if p == target_platform then
            return true
        end
    end

    return false
end

--[[
    InheritPlatform(children, parent)

    Inherits PLATFORM and SUPPORTED_PLATFORMS from parent to children.

    Equivalent to Jam:
        rule InheritPlatform { }

    Parameters:
        children - List of child targets (names or objects)
        parent - Parent target (name or object)
]]
function InheritPlatform(children, parent)
    if type(children) ~= "table" then children = {children} end

    local parent_platform
    local parent_supported

    if type(parent) == "string" then
        -- Will need to resolve when targets are loaded
        return {
            inherit_from = parent,
            children = children
        }
    else
        parent_platform = parent:get("platform")
        parent_supported = parent:get("supported_platforms")
    end

    for _, child in ipairs(children) do
        if type(child) ~= "string" then
            if parent_platform then
                child:set("platform", parent_platform)
            end
            if parent_supported then
                child:set("supported_platforms", parent_supported)
            end
        end
    end
end

-- ============================================================================
-- Assembler Flags
-- ============================================================================

--[[
    SubDirAsFlags(flags)

    Adds assembler flags for the current subdirectory.

    Equivalent to Jam:
        rule SubDirAsFlags { }
]]
function SubDirAsFlags(flags)
    if type(flags) ~= "table" then flags = {flags} end
    for _, flag in ipairs(flags) do
        table.insert(_subdir_asflags, flag)
    end
end

--[[
    GetSubDirAsFlags()

    Gets the current subdirectory assembler flags.
]]
function GetSubDirAsFlags()
    return _subdir_asflags
end

--[[
    ClearSubDirAsFlags()

    Clears subdirectory assembler flags.
]]
function ClearSubDirAsFlags()
    _subdir_asflags = {}
end

-- ============================================================================
-- Additional Utility Functions
-- ============================================================================

--[[
    LocalClean(targets)

    Marks targets for cleanup. In xmake, this is handled automatically
    by the clean command, but provided for Jam compatibility.

    Equivalent to Jam:
        rule LocalClean { }
]]
function LocalClean(clean_target, targets)
    -- In xmake, cleaning is automatic based on build outputs
    -- This function exists for API compatibility
end

--[[
    LocalDepends(target, deps)

    Declares dependencies. In xmake, use add_deps() instead.

    Equivalent to Jam:
        rule LocalDepends { }
]]
function LocalDepends(target, deps)
    -- In xmake, dependencies are declared via add_deps()
    -- This function exists for API compatibility
    if type(target) ~= "string" and target.add_deps then
        if type(deps) ~= "table" then deps = {deps} end
        for _, dep in ipairs(deps) do
            target:add_deps(dep)
        end
    end
end

-- ============================================================================
-- Path Utilities (additional)
-- ============================================================================

--[[
    JoinPath(...)

    Joins path components, handling separators correctly.

    Example:
        JoinPath("a", "b", "c") -> "a/b/c"
        JoinPath("/a", "b/", "/c") -> "/a/b/c"
]]
function JoinPath(...)
    local parts = {...}
    local result = ""

    for i, part in ipairs(parts) do
        if part and part ~= "" then
            -- Remove trailing slashes except for root
            if part:sub(-1) == "/" and #part > 1 then
                part = part:sub(1, -2)
            end

            if result == "" then
                result = part
            else
                -- Remove leading slash from part (we'll add separator)
                if part:sub(1, 1) == "/" then
                    part = part:sub(2)
                end
                result = result .. "/" .. part
            end
        end
    end

    return result
end

--[[
    BaseName(filepath)

    Returns the base name (filename without directory) of a path.

    Example:
        BaseName("/home/user/file.txt") -> "file.txt"
]]
function BaseName(filepath)
    if not filepath then return nil end
    return filepath:match("([^/]+)$") or filepath
end

--[[
    DirName(filepath)

    Returns the directory portion of a path.

    Example:
        DirName("/home/user/file.txt") -> "/home/user"
]]
function DirName(filepath)
    if not filepath then return nil end
    local dir = filepath:match("(.+)/[^/]+$")
    if not dir then
        if filepath:sub(1, 1) == "/" then
            return "/"
        else
            return "."
        end
    end
    return dir
end

--[[
    FileExtension(filepath)

    Returns the file extension (with dot).

    Example:
        FileExtension("file.txt") -> ".txt"
        FileExtension("file.tar.gz") -> ".gz"
]]
function FileExtension(filepath)
    if not filepath then return nil end
    return filepath:match("(%.[^./]+)$")
end

--[[
    StripExtension(filepath)

    Returns path without extension.

    Example:
        StripExtension("file.txt") -> "file"
        StripExtension("/path/to/file.cpp") -> "/path/to/file"
]]
function StripExtension(filepath)
    if not filepath then return nil end
    local ext = FileExtension(filepath)
    if ext then
        return filepath:sub(1, -(#ext + 1))
    end
    return filepath
end
