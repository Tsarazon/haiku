--[[
    ConfigRules.lua - Hierarchical configuration variables for subdirectories

    xmake equivalent of build/jam/ConfigRules (1:1 migration)

    Provides a system to set build configuration variables (CCFLAGS, DEFINES,
    DEBUG, WARNINGS, etc.) for specific subdirectories from a central place
    (e.g., UserBuildConfig). Variables can be "global" (inherited by child
    directories) or "local" (only for the specified directory).

    Functions exported:
    - SetConfigVar(var, dir_tokens, value, scope)
    - AppendToConfigVar(var, dir_tokens, value, scope)
    - ConfigVar(var, dir_tokens, scope)
    - PrepareConfigVariables()

    Usage:
        import("rules.ConfigRules")
        ConfigRules.SetConfigVar("DEBUG", {"HAIKU_TOP", "src", "apps"}, "1", "global")
        local debug = ConfigRules.ConfigVar("DEBUG", {"HAIKU_TOP", "src", "apps"})
]]

-- Variables that are automatically set up for subdirectories
local AUTO_SET_UP_CONFIG_VARIABLES = {
    "CCFLAGS", "C++FLAGS", "DEBUG", "DEFINES", "HDRS",
    "JAMFILE", "LINKFLAGS", "OPTIM", "OPTIMIZE", "SYSHDRS", "WARNINGS"
}

-- Configuration store:
-- _configs[dir_key] = {
--     vars = { [varname] = value },
--     scopes = { [varname] = "global"|"local" },
--     inherited = { [varname] = value },  -- globally-scoped values
--     dir_tokens = {...},
--     prepared = false
-- }
local _configs = {}

-- Convert directory token list to a canonical key string
local function _dir_key(dir_tokens)
    if not dir_tokens or #dir_tokens == 0 then
        return "root"
    end
    return table.concat(dir_tokens, "/")
end

-- Get or create a config entry for a directory
local function _get_config(dir_tokens)
    local key = _dir_key(dir_tokens)
    if not _configs[key] then
        _configs[key] = {
            vars = {},
            scopes = {},
            inherited = {},
            dir_tokens = dir_tokens or {},
            prepared = false
        }
    end
    return _configs[key]
end

-- Get parent directory tokens (remove last element)
local function _parent_dir(dir_tokens)
    if not dir_tokens or #dir_tokens <= 1 then
        return {}
    end
    local parent = {}
    for i = 1, #dir_tokens - 1 do
        parent[i] = dir_tokens[i]
    end
    return parent
end

-- SetConfigVar: set a configuration variable for a directory
-- scope: "global" (default) or "local"
function SetConfigVar(var, dir_tokens, value, scope)
    if not value then
        raise(string.format("Error: no value specified for ConfigVar '%s'!", var))
    end

    scope = scope or "global"
    local config = _get_config(dir_tokens)

    config.vars[var] = value
    config.scopes[var] = scope

    -- If global, also store in inherited config
    if scope == "global" then
        config.inherited[var] = value
    end
end

-- AppendToConfigVar: append a value to a config variable
function AppendToConfigVar(var, dir_tokens, value, scope)
    local current = ConfigVar(var, dir_tokens)
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
            new_value = tostring(current) .. " " .. tostring(value)
        end
    else
        new_value = value
    end
    SetConfigVar(var, dir_tokens, new_value, scope)
end

-- ConfigVar: get the value of a config variable for a directory
-- Walks up the directory tree looking for inherited (global) values
function ConfigVar(var, dir_tokens, scope)
    local config = _get_config(dir_tokens)
    local var_scope = config.scopes[var]

    -- Check if this config has the variable set
    if var_scope then
        -- If no scope filter, or scope matches
        if not scope or scope == var_scope then
            return config.vars[var]
        end
    end

    -- Not found or scope mismatch — check parent directory
    if dir_tokens and #dir_tokens > 0 then
        return ConfigVar(var, _parent_dir(dir_tokens), "global")
    end

    -- Reached root without finding — return nil (caller can use global default)
    return nil
end

-- PrepareSubDirConfigVariables: prepare config for a single subdirectory
local function _prepare_subdir(dir_tokens)
    local config = _get_config(dir_tokens)
    if config.prepared then
        return
    end

    -- Prepare parent first (recursive)
    local parent_tokens = _parent_dir(dir_tokens)
    _prepare_subdir(parent_tokens)

    -- Inherit values from parent's inherited config
    local parent_config = _get_config(parent_tokens)
    for _, var in ipairs(AUTO_SET_UP_CONFIG_VARIABLES) do
        -- Set on config if not already set
        if config.vars[var] == nil and parent_config.inherited[var] ~= nil then
            config.vars[var] = parent_config.inherited[var]
        end
        -- Set on inherited config if not already set
        if config.inherited[var] == nil and parent_config.inherited[var] ~= nil then
            config.inherited[var] = parent_config.inherited[var]
        end
    end

    config.prepared = true
end

-- PrepareConfigVariables: initialize root config and propagate to all configured dirs
-- Call this after all SetConfigVar calls (e.g., after loading UserBuildConfig)
function PrepareConfigVariables(global_defaults)
    -- Initialize root config with global defaults
    local root = _get_config({})
    if global_defaults then
        for _, var in ipairs(AUTO_SET_UP_CONFIG_VARIABLES) do
            if global_defaults[var] ~= nil then
                root.vars[var] = global_defaults[var]
                root.inherited[var] = global_defaults[var]
            end
        end
    end
    root.prepared = true

    -- Prepare all existing subdirectory configs
    for _, config in pairs(_configs) do
        if #config.dir_tokens > 0 then
            _prepare_subdir(config.dir_tokens)
        end
    end
end

-- GetAutoSetUpConfigVariables: return the list of auto-setup variable names
function GetAutoSetUpConfigVariables()
    return AUTO_SET_UP_CONFIG_VARIABLES
end

-- AddAutoSetUpConfigVariable: add a variable name to the auto-setup list
function AddAutoSetUpConfigVariable(var)
    table.insert(AUTO_SET_UP_CONFIG_VARIABLES, var)
end
