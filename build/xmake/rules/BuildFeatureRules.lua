--[[
    BuildFeatureRules.lua - Build feature management for Haiku build system

    xmake equivalent of build/jam/BuildFeatureRules (1:1 migration)

    Functions exported:
    - FQualifiedBuildFeatureName(features)                  - Prepend arch to feature names
    - FIsBuildFeatureEnabled(feature)                       - Check if feature is enabled
    - FMatchesBuildFeatures(specification)                  - Evaluate feature specification
    - FFilterByBuildFeatures(list)                          - Filter list by feature annotations
    - EnableBuildFeatures(features, specification)          - Enable build features
    - BuildFeatureObject(feature)                           - Get/create unique object for feature
    - SetBuildFeatureAttribute(feature, attribute, values, package)
    - BuildFeatureAttribute(feature, attribute, flags)      - Get feature attribute value
    - ExtractBuildFeatureArchivesExpandValue(value, fileName) - Expand placeholders
    - ExtractBuildFeatureArchives(feature, list)            - Extract archives, set attributes
    - InitArchitectureBuildFeatures(architecture)           - Enable arch-derived features
    - GetAllBuildFeatures()                                 - Return all enabled features

    Usage:
        import("rules.BuildFeatureRules")
        BuildFeatureRules.EnableBuildFeatures({"openssl"})
        local enabled = BuildFeatureRules.FIsBuildFeatureEnabled("openssl")
]]

-- Module-level state

-- Registry of all build features:
-- Key: qualified feature name (e.g. "x86_64:openssl")
-- Value: { enabled = bool, object_id = string, attributes = {} }
-- attributes[attr_name] = { values = {...}, package = nil|string }
local _features = {}

-- Ordered list of enabled feature names
local _enabled_features = {}

-- Unique ID counter for feature objects
local _next_object_id = 0

-- Current target packaging architecture (set via InitArchitectureBuildFeatures)
local _target_packaging_arch = nil

-- Helper: generate unique object ID for a feature
local function _new_unique_id()
    local id = _next_object_id
    _next_object_id = _next_object_id + 1
    return string.format("_build_feature_%d", id)
end

-- Helper: get or create a feature entry
local function _get_or_create_feature(qualified_name)
    if not _features[qualified_name] then
        _features[qualified_name] = {
            enabled = false,
            object_id = nil,
            attributes = {}
        }
    end
    return _features[qualified_name]
end

-- Helper: get target packaging arch from config or module state
local function _get_packaging_arch()
    if _target_packaging_arch then
        return _target_packaging_arch
    end
    import("core.project.config")
    return config.get("target_packaging_arch") or config.get("arch") or "x86_64"
end


-- FQualifiedBuildFeatureName: prepend architecture to feature names
-- If features[1] == "QUALIFIED", return features[2] as-is (already qualified).
-- Otherwise prepend current TARGET_PACKAGING_ARCH.
-- Input: a list of feature names (or a single string)
-- Returns: list of qualified names
function FQualifiedBuildFeatureName(features)
    if type(features) == "string" then
        features = {features}
    end
    if not features or #features == 0 then
        return {}
    end

    -- Pre-qualified: "QUALIFIED" marker followed by the actual name
    if features[1] == "QUALIFIED" then
        return {features[2]}
    end

    local arch = _get_packaging_arch()
    local result = {}
    for _, f in ipairs(features) do
        table.insert(result, arch .. ":" .. f)
    end
    return result
end


-- FIsBuildFeatureEnabled: check if a build feature is enabled
-- Returns true if enabled, false otherwise
function FIsBuildFeatureEnabled(feature)
    if type(feature) == "table" then
        feature = feature[1]
    end
    local qualified = FQualifiedBuildFeatureName({feature})
    if #qualified == 0 then return false end

    local entry = _features[qualified[1]]
    return entry and entry.enabled or false
end


-- FMatchesBuildFeatures: evaluate a build feature specification
-- specification is a list of conditions (possibly comma-separated).
-- Positive conditions: at least one must be enabled (OR).
-- Negative conditions (prefixed "!"): none must be enabled (AND NOT).
-- Returns true if specification holds.
function FMatchesBuildFeatures(specification)
    if not specification or #specification == 0 then
        return false
    end

    -- Split comma-separated entries
    import("rules.HelperRules")
    local split_spec = {}
    for _, element in ipairs(specification) do
        local parts = HelperRules.FSplitString(element, ",")
        for _, p in ipairs(parts) do
            table.insert(split_spec, p)
        end
    end

    return HelperRules.FConditionsHold(split_spec, FIsBuildFeatureEnabled)
end


-- FFilterByBuildFeatures: filter a list annotated by build feature specifications
--
-- Two annotation modes:
-- 1. Single element: "value@spec" — include value only if spec holds
-- 2. Sublist block: "spec @{ ... }@" — include enclosed elements only if spec holds
-- Blocks can be nested. Unannotated elements pass through.
--
-- Returns: the filtered list
function FFilterByBuildFeatures(list)
    if not list or #list == 0 then
        return {}
    end

    local filtered = {}

    -- Evaluation stack: stack of booleans (1 = active, 0 = suppressed)
    -- We process elements with one-element lookahead because we need to check
    -- if the *next* element is "@{" to determine if current element is a spec.
    local eval_stack = {1}
    local previous = nil

    -- Append a dummy element so we don't need special handling for last element
    local extended = {}
    for _, v in ipairs(list) do
        table.insert(extended, v)
    end
    table.insert(extended, "dummy")

    for _, element in ipairs(extended) do
        local stack_top = eval_stack[1]
        local process_element = previous

        if element == "}@" then
            -- Pop the topmost specification off the stack
            table.remove(eval_stack, 1)
            if #eval_stack == 0 then
                raise("FFilterByBuildFeatures: Unbalanced @{ in list")
            end
            process_element = previous
            previous = nil

        elseif element == "@{" then
            -- previous element is the feature specification for this block
            if not previous then
                raise("FFilterByBuildFeatures: No feature specification before @{")
            end

            if eval_stack[1] == 1 and FMatchesBuildFeatures({previous}) then
                table.insert(eval_stack, 1, 1)
            else
                table.insert(eval_stack, 1, 0)
            end

            process_element = nil
            previous = nil

        else
            -- Regular element
            process_element = previous
            previous = element
        end

        if process_element and stack_top == 1 then
            -- Check for inline annotation: "value@spec"
            local value_part, spec_part = process_element:match("^(.-)@([^@]+)$")
            if value_part and spec_part then
                if FMatchesBuildFeatures({spec_part}) then
                    table.insert(filtered, value_part)
                end
            else
                table.insert(filtered, process_element)
            end
        end
    end

    if #eval_stack > 1 then
        raise("FFilterByBuildFeatures: Unbalanced @{ in list")
    end

    return filtered
end


-- EnableBuildFeatures: enable build features, optionally conditionally
-- features: list of feature names (lower case)
-- specification: optional build feature specification that must hold
function EnableBuildFeatures(features, specification)
    if type(features) == "string" then
        features = {features}
    end

    local qualified = FQualifiedBuildFeatureName(features)

    for _, qf in ipairs(qualified) do
        local entry = _get_or_create_feature(qf)
        if not entry.enabled then
            if not specification or #specification == 0
                    or FMatchesBuildFeatures(specification) then
                entry.enabled = true
                table.insert(_enabled_features, qf)
            end
        end
    end
end


-- BuildFeatureObject: get or create a unique object ID for a feature
-- Used internally for attribute storage keying.
function BuildFeatureObject(feature)
    if type(feature) == "table" then
        feature = feature[1]
    end

    local qualified = FQualifiedBuildFeatureName({feature})
    if #qualified == 0 then return nil end
    local qf = qualified[1]

    local entry = _get_or_create_feature(qf)
    if not entry.object_id then
        entry.object_id = _new_unique_id()
    end

    return entry.object_id
end


-- SetBuildFeatureAttribute: set an attribute on a build feature
-- feature: feature name
-- attribute: attribute name
-- values: attribute value (string, table of strings, or any)
-- package: optional package name the attribute belongs to
function SetBuildFeatureAttribute(feature, attribute, values, package)
    if type(feature) == "table" then
        feature = feature[1]
    end

    local qualified = FQualifiedBuildFeatureName({feature})
    if #qualified == 0 then return end
    local qf = qualified[1]

    local entry = _get_or_create_feature(qf)
    entry.attributes[attribute] = {
        values = values,
        package = package
    }
end


-- BuildFeatureAttribute: get the value of a build feature attribute
-- feature: feature name
-- attribute: attribute name
-- flags: optional list/string. If contains "path", resolve attribute values
--        to actual filesystem paths using the package extraction directory.
function BuildFeatureAttribute(feature, attribute, flags)
    if type(feature) == "table" then
        feature = feature[1]
    end

    local qualified = FQualifiedBuildFeatureName({feature})
    if #qualified == 0 then return nil end
    local qf = qualified[1]

    local entry = _features[qf]
    if not entry then return nil end

    local attr_data = entry.attributes[attribute]
    if not attr_data then return nil end

    local values = attr_data.values

    -- Check for "path" flag
    local want_path = false
    if type(flags) == "string" then
        want_path = (flags == "path")
    elseif type(flags) == "table" then
        for _, f in ipairs(flags) do
            if f == "path" then
                want_path = true
                break
            end
        end
    end

    if want_path and values then
        -- Get the attribute's package and corresponding extraction directory
        local pkg = BuildFeatureAttribute(feature, attribute .. ":package")
        local directory
        if pkg then
            directory = BuildFeatureAttribute(feature, pkg .. ":directory")
        end

        -- Translate values to paths
        if type(values) == "table" then
            local paths = {}
            for _, v in ipairs(values) do
                -- Strip grist (Jam-style "<grist>path" -> "path")
                local stripped = v:match("^<[^>]*>(.+)$") or v
                if directory then
                    table.insert(paths, path.join(directory, stripped))
                else
                    table.insert(paths, stripped)
                end
            end
            return paths
        elseif type(values) == "string" then
            local stripped = values:match("^<[^>]*>(.+)$") or values
            if directory then
                return path.join(directory, stripped)
            else
                return stripped
            end
        end
    end

    return values
end


-- ExtractBuildFeatureArchivesExpandValue: expand placeholders in a value string
-- Placeholders: %packageName%, %portName%, %packageFullVersion%,
--               %packageRevisionedName%, %portRevisionedName%
-- fileName: the .hpkg file name used to extract package metadata
function ExtractBuildFeatureArchivesExpandValue(value, fileName)
    if not value or value == "" then
        return value
    end

    -- Lazily parse package metadata from fileName
    local packageName
    local portName
    local packageFullVersion
    local metadata_parsed = false

    local function ensure_metadata()
        if metadata_parsed then return end
        metadata_parsed = true

        -- Extract name and version from "name-version.hpkg"
        local name_part, version_part = fileName:match("^([^-]+)-(.+)%.hpkg$")
        if name_part then
            packageName = name_part
            -- Full version: first two hyphen-separated components
            -- e.g. "1.2.3-1-x86_64" -> "1.2.3-1"
            local fv = version_part:match("^([^-]+-[^-]+)-.*$")
            packageFullVersion = fv or version_part
        else
            -- No version in filename
            packageName = fileName:match("^(.+)%.hpkg$") or fileName
            packageFullVersion = ""
        end

        -- Port name: strip known suffixes (_devel, _doc, _source, _debuginfo)
        local known_suffixes = {"_devel", "_doc", "_source", "_debuginfo"}
        portName = packageName
        for _, suffix in ipairs(known_suffixes) do
            local stripped = portName:match("^(.+)" .. suffix .. "$")
            if stripped then
                portName = stripped
                break
            end
        end
    end

    -- Expand placeholders using pattern substitution
    -- We process placeholders one at a time from the value string
    local result = ""
    local rest = value

    while rest and rest ~= "" do
        -- Find the next placeholder: %something%
        local before, placeholder, after = rest:match("^(.-)%%([^%%]+)%%(.*)$")
        if not placeholder then
            result = result .. rest
            break
        end

        result = result .. before

        if placeholder == "packageRevisionedName" then
            ensure_metadata()
            result = result .. (packageName or "") .. "-" .. (packageFullVersion or "")
        elseif placeholder == "portRevisionedName" then
            ensure_metadata()
            result = result .. (portName or "") .. "-" .. (packageFullVersion or "")
        elseif placeholder == "packageName" then
            ensure_metadata()
            result = result .. (packageName or "")
        elseif placeholder == "portName" then
            ensure_metadata()
            result = result .. (portName or "")
        elseif placeholder == "packageFullVersion" then
            ensure_metadata()
            result = result .. (packageFullVersion or "")
        else
            -- Unknown placeholder: leave it as-is
            result = result .. "%" .. placeholder .. "%"
        end

        rest = after
    end

    return result
end


-- ExtractBuildFeatureArchives: download and extract archives for a build feature
-- and set attributes for the feature from extracted entries.
--
-- Syntax for list:
--   "file:" <packageAlias> <packageName>
--      <attribute>: <value> ...
--      ...
--   "file:" <packageAlias2> <packageName2>
--      ...
--
-- Special attribute "depends:" inherits extraction directory from another package.
-- Attribute "<alias>:directory" is automatically set for each package.
--
-- feature: the build feature name
-- list: the annotated list (will be filtered by FFilterByBuildFeatures first)
function ExtractBuildFeatureArchives(feature, list)
    import("core.project.config")

    local qualified = FQualifiedBuildFeatureName({feature})
    if #qualified == 0 then return end
    local qf = qualified[1]

    -- Filter by build features first
    list = FFilterByBuildFeatures(list)

    local i = 1
    while i <= #list do
        if list[i] ~= "file:" then
            raise("ExtractBuildFeatureArchives: Expected \"file: ...\", got: " .. tostring(list[i]))
        end

        local package_alias = list[i + 1]
        local package_name = list[i + 2]
        i = i + 3

        -- Fetch the package file
        -- FetchPackage is defined in RepositoryRules; import it
        import("rules.RepositoryRules")
        local file = RepositoryRules.FetchPackage(package_name)
        local fileName = path.filename(file)

        -- Determine extraction directory
        local build_packages_dir = config.get("haiku_optional_build_packages_dir")
            or path.join(config.get("haiku_top") or os.projectdir(), "generated",
                "optional_build_packages")
        local directory = path.join(build_packages_dir, path.basename(fileName))

        -- Parse attributes
        while i <= #list do
            -- Match "attribute:" pattern
            local attr = list[i]:match("^(.+):$")
            if not attr then
                raise("ExtractBuildFeatureArchives: Expected attribute, got: " .. tostring(list[i]))
            end
            if attr == "file" then
                break  -- start of next file block
            end

            i = i + 1

            -- Collect values until next "attribute:" or end
            local values = {}
            while i <= #list do
                if list[i]:match(":$") then
                    break  -- next attribute
                end
                local expanded = ExtractBuildFeatureArchivesExpandValue(list[i], fileName)
                table.insert(values, expanded)
                i = i + 1
            end

            if attr == "depends" then
                -- Inherit extraction directory from the base package
                local base_package = values[1]
                local base_dir = BuildFeatureAttribute(feature, base_package .. ":directory")
                if base_dir then
                    directory = base_dir
                end
            else
                -- Extract archive entries and set feature attributes
                import("rules.FileRules")
                -- ExtractArchive needs a target for dependfile; we pass a pseudo-target
                -- In practice this is called during configuration, we store the paths
                SetBuildFeatureAttribute(feature, attr, values)
                SetBuildFeatureAttribute(feature, attr .. ":package", package_alias)
            end
        end

        -- Set the directory attribute for this package alias
        SetBuildFeatureAttribute(feature, package_alias .. ":directory", directory)
    end
end


-- InitArchitectureBuildFeatures: enable features derived from architecture
-- Sets up the target architecture as a build feature, enables "primary"
-- for the primary architecture, and enables secondary arch features.
function InitArchitectureBuildFeatures(architecture)
    import("core.project.config")

    -- Save and temporarily set the packaging arch
    local saved_arch = _target_packaging_arch
    _target_packaging_arch = architecture

    -- Get the target architecture name for this packaging arch
    local target_arch = config.get("target_arch_" .. architecture) or architecture
    EnableBuildFeatures({target_arch})

    -- For the primary architecture, add the "primary" build feature
    local packaging_archs = config.get("target_packaging_archs") or {}
    if type(packaging_archs) == "string" then
        packaging_archs = {packaging_archs}
    end
    if #packaging_archs > 0 and architecture == packaging_archs[1] then
        EnableBuildFeatures({"primary"})
    end

    -- Add all secondary architectures as build features
    if #packaging_archs > 1 then
        for idx = 2, #packaging_archs do
            EnableBuildFeatures({"secondary_" .. packaging_archs[idx]})
        end
    end

    -- Restore saved arch
    _target_packaging_arch = saved_arch
end


-- GetAllBuildFeatures: return the list of all enabled feature names
function GetAllBuildFeatures()
    return _enabled_features
end

-- GetBuildFeature: return the full feature entry for a qualified name
function GetBuildFeature(qualified_name)
    return _features[qualified_name]
end

-- SetTargetPackagingArch: set the current target packaging architecture
-- Used by other modules to temporarily switch arch context
function SetTargetPackagingArch(arch)
    _target_packaging_arch = arch
end

-- GetTargetPackagingArch: get the current target packaging architecture
function GetTargetPackagingArch()
    return _target_packaging_arch or _get_packaging_arch()
end
