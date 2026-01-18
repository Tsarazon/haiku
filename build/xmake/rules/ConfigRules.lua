--[[
    ConfigRules.lua - Haiku directory-based configuration system

    xmake equivalent of build/jam/ConfigRules

    Rules defined:
    - ConfigObject              - Get config object for directory
    - SetConfigVar              - Set config variable for directory
    - AppendToConfigVar         - Append to config variable
    - ConfigVar                 - Get config variable value (with inheritance)
    - PrepareSubDirConfigVariables - Prepare variables for subdirectory
    - PrepareConfigVariables    - Initialize all config variables

    This system allows setting configuration values (compiler flags, warnings,
    defines, etc.) for specific directories in a central place (like UserBuildConfig)
    without editing individual Jamfiles/xmake.lua files.

    Variables support two scopes:
    - "global": Value inherited by subdirectories
    - "local": Value only applies to this directory
]]

-- ============================================================================
-- Configuration Storage
-- ============================================================================

-- Storage for directory configurations
-- Key: grist (directory path), Value: config table
local _dir_configs = {}

-- List of configured directories
local _existing_subdir_configs = {}

-- Variables that are automatically set up for subdirs
local AUTO_SET_UP_CONFIG_VARIABLES = {
    "CCFLAGS", "CXXFLAGS", "DEBUG", "DEFINES", "HDRS",
    "JAMFILE", "LINKFLAGS", "OPTIM", "OPTIMIZE", "SYSHDRS", "WARNINGS"
}

-- Global default values
local _global_defaults = {}

-- ============================================================================
-- Helper Functions
-- ============================================================================

--[[
    FGrist(parts)

    Creates a grist string from path parts.
]]
local function FGrist(parts)
    if type(parts) ~= "table" then
        parts = {parts}
    end
    return "<" .. table.concat(parts, "-") .. ">"
end

--[[
    FReverse(list)

    Reverses a list.
]]
local function FReverse(list)
    local reversed = {}
    for i = #list, 1, -1 do
        table.insert(reversed, list[i])
    end
    return reversed
end

-- ============================================================================
-- ConfigObject
-- ============================================================================

--[[
    ConfigObject(dir, var_name)

    Returns the config object for a given directory.

    Equivalent to Jam:
        rule ConfigObject { }

    Parameters:
        dir - Directory tokens (list of path components)
        var_name - Optional variable name (default: "__config__")

    Returns:
        Config object identifier (grist string)
]]
function ConfigObject(dir, var_name)
    var_name = var_name or "__config__"

    if type(dir) ~= "table" then
        dir = dir and {dir} or {}
    end

    local grist = FGrist({"root", table.unpack(dir)})
    local key = grist .. ":" .. var_name

    -- Create config object if it doesn't exist
    if not _dir_configs[key] then
        _dir_configs[key] = {
            _grist = grist,
            _var_name = var_name,
            _dir_tokens = dir,
            _configured = false,
            _prepared = false,
            _variables = {},
            _scopes = {}
        }
    end

    return key
end

-- ============================================================================
-- SetConfigVar
-- ============================================================================

--[[
    SetConfigVar(var, dir, value, scope)

    Sets a config variable for a specified directory.

    Equivalent to Jam:
        rule SetConfigVar { }

    Parameters:
        var - Variable name
        dir - Directory tokens
        value - Value to set
        scope - "global" (inherited by subdirs) or "local" (this dir only)
                Default: "global"
]]
function SetConfigVar(var, dir, value, scope)
    scope = scope or "global"

    if value == nil then
        error("Error: no value specified for ConfigVar '" .. var .. "'!")
    end

    local config_key = ConfigObject(dir)
    local config = _dir_configs[config_key]

    -- Set the variable and its scope
    config._variables[var] = value
    config._scopes[var] = scope

    -- For global scope, also set on inherited config
    if scope == "global" then
        local inherited_key = ConfigObject(dir, "__inherited_config__")
        local inherited = _dir_configs[inherited_key]
        inherited._variables[var] = value
    end

    -- Mark as configured
    if not config._configured then
        config._configured = true
        table.insert(_existing_subdir_configs, config_key)
    end
end

-- ============================================================================
-- AppendToConfigVar
-- ============================================================================

--[[
    AppendToConfigVar(var, dir, value, scope)

    Appends a value to a config variable for a specified directory.

    Equivalent to Jam:
        rule AppendToConfigVar { }

    Parameters:
        var - Variable name
        dir - Directory tokens
        value - Value to append
        scope - "global" or "local" (default: "global")
]]
function AppendToConfigVar(var, dir, value, scope)
    local current = ConfigVar(var, dir)

    local new_value
    if current then
        if type(current) == "table" then
            new_value = current
            if type(value) == "table" then
                for _, v in ipairs(value) do
                    table.insert(new_value, v)
                end
            else
                table.insert(new_value, value)
            end
        else
            if type(value) == "table" then
                new_value = {current}
                for _, v in ipairs(value) do
                    table.insert(new_value, v)
                end
            else
                new_value = current .. " " .. value
            end
        end
    else
        new_value = value
    end

    SetConfigVar(var, dir, new_value, scope)
end

-- ============================================================================
-- ConfigVar
-- ============================================================================

--[[
    ConfigVar(var, dir, scope)

    Returns the value of a config variable for a given directory.
    If not set, recursively checks parent directories (for global scope).
    Falls back to global default value.

    Equivalent to Jam:
        rule ConfigVar { }

    Parameters:
        var - Variable name
        dir - Directory tokens
        scope - Optional scope filter

    Returns:
        Variable value or nil
]]
function ConfigVar(var, dir, scope)
    if type(dir) ~= "table" then
        dir = dir and {dir} or {}
    end

    local config_key = ConfigObject(dir)
    local config = _dir_configs[config_key]

    if config then
        local var_scope = config._scopes[var]

        -- Check if scope matches
        if (not scope and var_scope) or (scope and scope == var_scope) or #dir == 0 then
            local value = config._variables[var]
            if value ~= nil then
                return value
            end
        end
    end

    -- Try parent directory with global scope
    if #dir > 0 then
        local parent_dir = {}
        for i = 1, #dir - 1 do
            table.insert(parent_dir, dir[i])
        end
        return ConfigVar(var, parent_dir, "global")
    end

    -- Fall back to global default
    return _global_defaults[var]
end

-- ============================================================================
-- PrepareSubDirConfigVariables
-- ============================================================================

--[[
    PrepareSubDirConfigVariables(dir_tokens)

    Prepares config variables for a subdirectory by inheriting
    from parent directories.

    Equivalent to Jam:
        rule PrepareSubDirConfigVariables { }

    Parameters:
        dir_tokens - Directory tokens
]]
function PrepareSubDirConfigVariables(dir_tokens)
    if type(dir_tokens) ~= "table" then
        dir_tokens = dir_tokens and {dir_tokens} or {}
    end

    local config_key = ConfigObject(dir_tokens)
    local config = _dir_configs[config_key]

    if not config or config._prepared then
        return
    end

    -- Prepare parent directory first
    if #dir_tokens > 0 then
        local parent_dir = {}
        for i = 1, #dir_tokens - 1 do
            table.insert(parent_dir, dir_tokens[i])
        end
        PrepareSubDirConfigVariables(parent_dir)
    end

    -- Get inherited config for this directory
    local inherited_key = ConfigObject(dir_tokens, "__inherited_config__")
    local inherited = _dir_configs[inherited_key]

    -- Get parent's inherited config
    local parent_dir = {}
    for i = 1, #dir_tokens - 1 do
        table.insert(parent_dir, dir_tokens[i])
    end
    local parent_inherited_key = ConfigObject(parent_dir, "__inherited_config__")
    local parent_inherited = _dir_configs[parent_inherited_key]

    -- Inherit variables from parent
    if parent_inherited then
        for _, var in ipairs(AUTO_SET_UP_CONFIG_VARIABLES) do
            local parent_value = parent_inherited._variables[var]
            if parent_value ~= nil then
                -- Only set if not already set
                if config._variables[var] == nil then
                    config._variables[var] = parent_value
                end
                if inherited._variables[var] == nil then
                    inherited._variables[var] = parent_value
                end
            end
        end
    end

    config._inherited_config = inherited_key
    config._prepared = true
end

-- ============================================================================
-- PrepareConfigVariables
-- ============================================================================

--[[
    PrepareConfigVariables()

    Initializes all config variables by setting up root config
    and preparing all configured subdirectories.

    Equivalent to Jam:
        rule PrepareConfigVariables { }
]]
function PrepareConfigVariables()
    -- Initialize root config objects
    local root_config_key = ConfigObject({})
    local root_config = _dir_configs[root_config_key]

    local inherited_root_key = ConfigObject({}, "__inherited_config__")
    local inherited_root = _dir_configs[inherited_root_key]

    -- Set global defaults on root configs
    for _, var in ipairs(AUTO_SET_UP_CONFIG_VARIABLES) do
        local global_value = _global_defaults[var]
        if global_value ~= nil then
            root_config._variables[var] = global_value
            inherited_root._variables[var] = global_value
        end
    end

    root_config._prepared = true

    -- Prepare all configured subdirectories
    for _, config_key in ipairs(_existing_subdir_configs) do
        local config = _dir_configs[config_key]
        if config and config._dir_tokens then
            PrepareSubDirConfigVariables(config._dir_tokens)
        end
    end
end

-- ============================================================================
-- Global Defaults Management
-- ============================================================================

--[[
    SetGlobalDefault(var, value)

    Sets a global default value for a config variable.

    Parameters:
        var - Variable name
        value - Default value
]]
function SetGlobalDefault(var, value)
    _global_defaults[var] = value
end

--[[
    GetGlobalDefault(var)

    Gets a global default value.

    Parameters:
        var - Variable name

    Returns:
        Default value or nil
]]
function GetGlobalDefault(var)
    return _global_defaults[var]
end

-- ============================================================================
-- Convenience Functions
-- ============================================================================

--[[
    SetDirFlags(dir, flags)

    Sets compiler flags for a directory.

    Parameters:
        dir - Directory path (string or table of tokens)
        flags - Table with optional keys: cflags, cxxflags, defines, includes, ldflags
]]
function SetDirFlags(dir, flags)
    if type(dir) == "string" then
        dir = dir:split("/")
    end

    if flags.cflags then
        SetConfigVar("CCFLAGS", dir, flags.cflags)
    end
    if flags.cxxflags then
        SetConfigVar("CXXFLAGS", dir, flags.cxxflags)
    end
    if flags.defines then
        SetConfigVar("DEFINES", dir, flags.defines)
    end
    if flags.includes then
        SetConfigVar("HDRS", dir, flags.includes)
    end
    if flags.ldflags then
        SetConfigVar("LINKFLAGS", dir, flags.ldflags)
    end
    if flags.warnings then
        SetConfigVar("WARNINGS", dir, flags.warnings)
    end
    if flags.debug then
        SetConfigVar("DEBUG", dir, flags.debug)
    end
    if flags.optimize then
        SetConfigVar("OPTIMIZE", dir, flags.optimize)
    end
end

--[[
    EnableDebugForDir(dir)

    Enables debug mode for a directory.
]]
function EnableDebugForDir(dir)
    if type(dir) == "string" then
        dir = dir:split("/")
    end
    SetConfigVar("DEBUG", dir, true)
end

--[[
    EnableWarningsForDir(dir, level)

    Sets warning level for a directory.

    Parameters:
        dir - Directory path
        level - Warning level (0, 1, 2 or "off", "on", "error")
]]
function EnableWarningsForDir(dir, level)
    if type(dir) == "string" then
        dir = dir:split("/")
    end
    SetConfigVar("WARNINGS", dir, level)
end

--[[
    GetDirConfig(dir)

    Gets all configuration for a directory (for debugging).

    Parameters:
        dir - Directory path

    Returns:
        Table with all config values
]]
function GetDirConfig(dir)
    if type(dir) == "string" then
        dir = dir:split("/")
    end

    local result = {}
    for _, var in ipairs(AUTO_SET_UP_CONFIG_VARIABLES) do
        result[var] = ConfigVar(var, dir)
    end
    return result
end

--[[
    ClearConfigVariables()

    Clears all configuration (for testing).
]]
function ClearConfigVariables()
    _dir_configs = {}
    _existing_subdir_configs = {}
    _global_defaults = {}
end

-- ============================================================================
-- Integration with xmake targets
-- ============================================================================

--[[
    ApplyDirConfigToTarget(target)

    Applies directory configuration to an xmake target.
    Call this in on_load or before_build.

    Parameters:
        target - xmake target object
]]
function ApplyDirConfigToTarget(target)
    local scriptdir = target:scriptdir()
    local projectdir = os.projectdir()

    -- Convert script directory to tokens relative to project
    local rel_path = path.relative(scriptdir, projectdir)
    local dir_tokens = rel_path:split("/")

    -- Prepare config variables for this directory
    PrepareSubDirConfigVariables(dir_tokens)

    -- Apply configuration
    local ccflags = ConfigVar("CCFLAGS", dir_tokens)
    if ccflags then
        if type(ccflags) == "table" then
            target:add("cflags", ccflags)
        else
            target:add("cflags", ccflags:split(" "))
        end
    end

    local cxxflags = ConfigVar("CXXFLAGS", dir_tokens)
    if cxxflags then
        if type(cxxflags) == "table" then
            target:add("cxxflags", cxxflags)
        else
            target:add("cxxflags", cxxflags:split(" "))
        end
    end

    local defines = ConfigVar("DEFINES", dir_tokens)
    if defines then
        if type(defines) == "table" then
            target:add("defines", defines)
        else
            target:add("defines", defines:split(" "))
        end
    end

    local hdrs = ConfigVar("HDRS", dir_tokens)
    if hdrs then
        if type(hdrs) == "table" then
            target:add("includedirs", hdrs)
        else
            target:add("includedirs", hdrs:split(" "))
        end
    end

    local linkflags = ConfigVar("LINKFLAGS", dir_tokens)
    if linkflags then
        if type(linkflags) == "table" then
            target:add("ldflags", linkflags)
        else
            target:add("ldflags", linkflags:split(" "))
        end
    end

    local warnings = ConfigVar("WARNINGS", dir_tokens)
    if warnings then
        if warnings == 2 or warnings == "error" then
            target:add("cxflags", "-Werror")
        elseif warnings == 0 or warnings == "off" then
            target:add("cxflags", "-w")
        end
    end

    local debug_mode = ConfigVar("DEBUG", dir_tokens)
    if debug_mode then
        target:add("cxflags", "-g")
        target:add("defines", "DEBUG=1")
    end
end

-- ============================================================================
-- Rule for automatic config application
-- ============================================================================

--[[
    DirConfig rule

    Automatically applies directory configuration to targets.

    Usage:
        target("mylib")
            add_rules("DirConfig")
]]
rule("DirConfig")
    on_load(function (target)
        ApplyDirConfigToTarget(target)
    end)