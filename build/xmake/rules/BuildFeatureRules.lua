--[[
    BuildFeatureRules.lua - Haiku build feature management

    xmake equivalent of build/jam/BuildFeatureRules

    Rules defined:
    - FQualifiedBuildFeatureName  - Prepend architecture to feature name
    - FIsBuildFeatureEnabled      - Check if feature is enabled
    - FMatchesBuildFeatures       - Check feature specification
    - FFilterByBuildFeatures      - Filter list by feature specifications
    - EnableBuildFeatures         - Enable build features
    - BuildFeatureObject          - Get unique object for feature
    - SetBuildFeatureAttribute    - Set feature attribute
    - BuildFeatureAttribute       - Get feature attribute
    - ExtractBuildFeatureArchives - Download and extract feature archives
    - InitArchitectureBuildFeatures - Initialize arch-specific features

    Build features are optional components that can be enabled/disabled
    based on availability of packages, architecture, or configuration.
    Features are qualified by architecture (e.g., "x86_64:openssl").
]]

-- Note: import() must be called inside functions, not at top level
-- import("core.project.config")

-- ============================================================================
-- Build Feature Storage
-- ============================================================================

-- Global storage for build features
local _build_features = {}           -- Set of enabled features
local _feature_objects = {}          -- Feature name -> unique object
local _feature_attributes = {}       -- Feature object -> attribute table

-- Current target packaging architecture (set during build)
local _target_packaging_arch = nil

-- ============================================================================
-- Architecture Management
-- ============================================================================

--[[
    SetTargetPackagingArch(arch)

    Sets the current target packaging architecture.
    Used by feature functions to qualify feature names.
]]
function SetTargetPackagingArch(arch)
    _target_packaging_arch = arch
end

--[[
    GetTargetPackagingArch()

    Gets the current target packaging architecture.
]]
function GetTargetPackagingArch()
    return _target_packaging_arch or config.get("arch") or "x86_64"
end

-- ============================================================================
-- Feature Name Qualification
-- ============================================================================

--[[
    FQualifiedBuildFeatureName(features)

    Prepends the name of the current target packaging architecture to the
    given feature names.

    Equivalent to Jam:
        rule FQualifiedBuildFeatureName { }

    Parameters:
        features - Feature name or list of feature names
                   If first element is "QUALIFIED", returns second element as-is

    Returns:
        List of qualified feature names (arch:feature format)
]]
function FQualifiedBuildFeatureName(features)
    if type(features) ~= "table" then
        features = {features}
    end

    -- Check for pre-qualified name
    if features[1] == "QUALIFIED" then
        return features[2]
    end

    local arch = GetTargetPackagingArch()
    local qualified = {}

    for _, feature in ipairs(features) do
        table.insert(qualified, arch .. ":" .. feature)
    end

    if #qualified == 1 then
        return qualified[1]
    end
    return qualified
end

-- ============================================================================
-- Feature State Checking
-- ============================================================================

--[[
    FIsBuildFeatureEnabled(feature)

    Returns whether the given build feature is enabled.

    Equivalent to Jam:
        rule FIsBuildFeatureEnabled { }

    Parameters:
        feature - The name of the build feature (lower case)

    Returns:
        true if enabled, false otherwise
]]
function FIsBuildFeatureEnabled(feature)
    local qualified = FQualifiedBuildFeatureName(feature)
    return _build_features[qualified:upper()] == true
end

--[[
    FMatchesBuildFeatures(specification)

    Returns whether the given build feature specification holds.
    Specification consists of positive and negative conditions.

    Conditions can be:
    - Individual list elements
    - Multiple conditions joined by "," in a single element

    A positive condition holds when the named feature is enabled.
    A negative condition (starting with "!") holds when the feature is NOT enabled.

    Specification holds when:
    - None of the negative conditions hold AND
    - If there are positive conditions, at least one holds

    Equivalent to Jam:
        rule FMatchesBuildFeatures { }

    Parameters:
        specification - Build feature specification (list or comma-separated string)

    Returns:
        true if specification holds, false otherwise
]]
function FMatchesBuildFeatures(specification)
    if not specification then
        return false
    end

    if type(specification) ~= "table" then
        specification = {specification}
    end

    -- Split comma-separated elements
    local split_spec = {}
    for _, element in ipairs(specification) do
        for part in element:gmatch("[^,]+") do
            table.insert(split_spec, part)
        end
    end

    if #split_spec == 0 then
        return false
    end

    -- Use FConditionsHold from HelperRules
    local has_positive = false
    local has_negative = false
    local positive_match = false

    for _, condition in ipairs(split_spec) do
        if condition:sub(1, 1) == "!" then
            -- Negative condition
            has_negative = true
            local feature_name = condition:sub(2)
            if FIsBuildFeatureEnabled(feature_name) then
                -- Negative condition violated
                return false
            end
        else
            -- Positive condition
            has_positive = true
            if FIsBuildFeatureEnabled(condition) then
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
-- List Filtering by Features
-- ============================================================================

--[[
    FFilterByBuildFeatures(list)

    Filters a list annotated by build feature specifications.

    Annotation methods:
    1. Single element: "value@specification" - included if specification holds
    2. Sublist: "specification @{ ... }@" - sublist included if specification holds

    Sublist annotations can be nested.

    Equivalent to Jam:
        rule FFilterByBuildFeatures { }

    Parameters:
        list - A list annotated with build feature specifications

    Returns:
        Filtered list with annotations removed
]]
function FFilterByBuildFeatures(list)
    if type(list) ~= "table" then
        list = {list}
    end

    local filtered = {}
    local evaluation_stack = {true}  -- Stack of boolean values
    local previous_element = nil

    -- Append dummy element for processing last real element
    local processing_list = {}
    for _, v in ipairs(list) do
        table.insert(processing_list, v)
    end
    table.insert(processing_list, "dummy")

    for _, element in ipairs(processing_list) do
        local stack_top = evaluation_stack[1]
        local process_element = previous_element

        if element == "}@" then
            -- Pop the topmost specification off the stack
            if #evaluation_stack <= 1 then
                error("FFilterByBuildFeatures: Unbalanced @{ in list")
            end
            table.remove(evaluation_stack, 1)
            process_element = previous_element
            previous_element = nil

        elseif element == "@{" then
            -- Start of conditional block
            if not previous_element then
                error("FFilterByBuildFeatures: No feature specification before @{")
            end

            -- Check if current context is true and specification matches
            if evaluation_stack[1] and FMatchesBuildFeatures(previous_element) then
                table.insert(evaluation_stack, 1, true)
            else
                table.insert(evaluation_stack, 1, false)
            end

            process_element = nil
            previous_element = nil

        else
            -- Regular element
            process_element = previous_element
            previous_element = element
        end

        -- Process the element if we're in an enabled context
        if process_element and stack_top then
            -- Check for @annotation on single element
            local base, annotation = process_element:match("(.*)@([^@]*)$")
            if base and annotation and annotation ~= "" and annotation ~= "{" then
                -- Has annotation - check if specification holds
                if FMatchesBuildFeatures(annotation) then
                    table.insert(filtered, base)
                end
            else
                -- No annotation - include as-is
                table.insert(filtered, process_element)
            end
        end
    end

    if #evaluation_stack > 1 then
        error("FFilterByBuildFeatures: Unbalanced }@ in list")
    end

    return filtered
end

-- ============================================================================
-- Feature Enabling
-- ============================================================================

--[[
    EnableBuildFeatures(features, specification)

    Enables the build features if the specification holds.
    If specification is omitted, features are enabled unconditionally.

    Enabling a feature:
    - Adds its name to HAIKU_BUILD_FEATURES
    - Sets HAIKU_BUILD_FEATURE_<FEATURE>_ENABLED = true

    Equivalent to Jam:
        rule EnableBuildFeatures { }

    Parameters:
        features - A list of build feature names (lower case)
        specification - Optional build features specification
]]
function EnableBuildFeatures(features, specification)
    if type(features) ~= "table" then
        features = {features}
    end

    features = FQualifiedBuildFeatureName(features)
    if type(features) ~= "table" then
        features = {features}
    end

    for _, feature in ipairs(features) do
        local upper_feature = feature:upper()

        if not _build_features[upper_feature] then
            -- Check specification if provided
            if not specification or FMatchesBuildFeatures(specification) then
                _build_features[upper_feature] = true
            end
        end
    end
end

--[[
    DisableBuildFeatures(features)

    Disables the specified build features.

    Parameters:
        features - A list of build feature names
]]
function DisableBuildFeatures(features)
    if type(features) ~= "table" then
        features = {features}
    end

    features = FQualifiedBuildFeatureName(features)
    if type(features) ~= "table" then
        features = {features}
    end

    for _, feature in ipairs(features) do
        _build_features[feature:upper()] = nil
    end
end

--[[
    GetEnabledBuildFeatures()

    Returns list of all enabled build features.
]]
function GetEnabledBuildFeatures()
    local features = {}
    for feature, enabled in pairs(_build_features) do
        if enabled then
            table.insert(features, feature)
        end
    end
    return features
end

-- ============================================================================
-- Feature Objects and Attributes
-- ============================================================================

-- Counter for unique objects
local _next_unique_id = 1

--[[
    BuildFeatureObject(feature)

    Returns a unique object for the given build feature.
    Used for attaching attribute values.

    Equivalent to Jam:
        rule BuildFeatureObject { }

    Parameters:
        feature - Build feature name

    Returns:
        Unique object identifier for the feature
]]
function BuildFeatureObject(feature)
    local qualified = FQualifiedBuildFeatureName(feature)
    local upper_feature = qualified:upper()

    if not _feature_objects[upper_feature] then
        _feature_objects[upper_feature] = "feature_object_" .. _next_unique_id
        _next_unique_id = _next_unique_id + 1
        _feature_attributes[_feature_objects[upper_feature]] = {}
    end

    return _feature_objects[upper_feature]
end

--[[
    SetBuildFeatureAttribute(feature, attribute, values, package)

    Sets an attribute of a build feature.

    Equivalent to Jam:
        rule SetBuildFeatureAttribute { }

    Parameters:
        feature - Build feature name
        attribute - Attribute name
        values - Attribute value(s)
        package - Optional package name the attribute belongs to
]]
function SetBuildFeatureAttribute(feature, attribute, values, package)
    local feature_object = BuildFeatureObject(feature)
    local attrs = _feature_attributes[feature_object]

    attrs[attribute] = values

    if package then
        attrs[attribute .. ":package"] = package
    end
end

--[[
    BuildFeatureAttribute(feature, attribute, flags)

    Returns the value of an attribute of a build feature.

    Equivalent to Jam:
        rule BuildFeatureAttribute { }

    Parameters:
        feature - Build feature name
        attribute - Attribute name
        flags - Optional flags:
                "path" - Convert targets to paths relative to extraction dir

    Returns:
        Attribute value(s)
]]
function BuildFeatureAttribute(feature, attribute, flags)
    local feature_object = BuildFeatureObject(feature)
    local attrs = _feature_attributes[feature_object]
    local values = attrs[attribute]

    if not values then
        return nil
    end

    flags = flags or {}
    if type(flags) ~= "table" then
        flags = {flags}
    end

    -- Check for "path" flag
    local convert_to_path = false
    for _, flag in ipairs(flags) do
        if flag == "path" then
            convert_to_path = true
            break
        end
    end

    if convert_to_path then
        -- Get the attribute's package and extraction directory
        local package = attrs[attribute .. ":package"]
        local directory = nil

        if package then
            directory = attrs[package .. ":directory"]
        end

        -- Convert values to paths
        if type(values) ~= "table" then
            values = {values}
        end

        local paths = {}
        for _, value in ipairs(values) do
            -- Remove grist if present
            local clean_value = value:gsub("^<[^>]+>", "")
            if directory then
                table.insert(paths, path.join(directory, clean_value))
            else
                table.insert(paths, clean_value)
            end
        end

        return paths
    end

    return values
end

-- ============================================================================
-- Archive Extraction
-- ============================================================================

--[[
    ExtractBuildFeatureArchivesExpandValue(value, fileName)

    Expands placeholder values in archive paths.

    Placeholders:
    - %packageName% - Package name from file
    - %portName% - Port name (package name without suffix)
    - %packageFullVersion% - Full version string
    - %packageRevisionedName% - packageName-packageFullVersion
    - %portRevisionedName% - portName-packageFullVersion

    Equivalent to Jam:
        rule ExtractBuildFeatureArchivesExpandValue { }
]]
function ExtractBuildFeatureArchivesExpandValue(value, fileName)
    if not value then
        return value
    end

    -- Extract package info from filename
    local packageName, packageFullVersion, portName

    -- Try to match name-version.hpkg pattern
    local name, version = fileName:match("^([^%-]+)%-(.+)%.hpkg$")
    if name then
        packageName = name
        -- Extract full version (including revision)
        local full_ver = version:match("^([^%-]+-[^%-]+)%-")
        packageFullVersion = full_ver or version
    else
        -- Just .hpkg
        packageName = fileName:match("^(.+)%.hpkg$") or fileName
        packageFullVersion = ""
    end

    -- Get port name (remove common suffixes)
    portName = packageName:gsub("_devel$", ""):gsub("_source$", ""):gsub("_debuginfo$", "")

    -- Perform substitutions
    value = value:gsub("%%packageRevisionedName%%", packageName .. "-" .. packageFullVersion)
    value = value:gsub("%%portRevisionedName%%", portName .. "-" .. packageFullVersion)
    value = value:gsub("%%packageName%%", packageName)
    value = value:gsub("%%portName%%", portName)
    value = value:gsub("%%packageFullVersion%%", packageFullVersion)

    return value
end

--[[
    ExtractBuildFeatureArchives(feature, list)

    Downloads and extracts archives for a build feature.

    Syntax for list:
        "file:" <packageAlias> <packageName>
           <attribute>: <value> ...
           ...
        "file:" <packageAlias2> <packageName2>
        ...

    Equivalent to Jam:
        rule ExtractBuildFeatureArchives { }

    Parameters:
        feature - Build feature name
        list - Archive specification list
]]
function ExtractBuildFeatureArchives(feature, list)
    if type(list) ~= "table" then
        list = {list}
    end

    local qualified_feature = FQualifiedBuildFeatureName(feature)

    -- Filter by build features first
    list = FFilterByBuildFeatures(list)

    local i = 1
    while i <= #list do
        if list[i] ~= "file:" then
            error("ExtractBuildFeatureArchives: Expected 'file: ...', got: " .. tostring(list[i]))
        end

        local package_alias = list[i + 1]
        local package_name = list[i + 2]
        i = i + 3

        -- Get package file (would call FetchPackage in real implementation)
        local file_name = package_name .. ".hpkg"

        -- Determine extraction directory
        local optional_packages_dir = config.get("haiku_optional_build_packages_dir")
            or path.join("$(buildir)", "optional_packages")
        local base_name = file_name:gsub("%.hpkg$", "")
        local directory = path.join(optional_packages_dir, base_name)

        -- Process attributes
        while i <= #list do
            local attr_match = list[i]:match("^(.+):$")
            if not attr_match then
                error("ExtractBuildFeatureArchives: Expected attribute, got: " .. tostring(list[i]))
            end

            local attribute = attr_match
            if attribute == "file" then
                -- Next file: block
                break
            end

            i = i + 1

            -- Collect values until next attribute
            local values = {}
            while i <= #list do
                if list[i]:match(":$") then
                    break
                end
                local expanded = ExtractBuildFeatureArchivesExpandValue(list[i], file_name)
                table.insert(values, expanded)
                i = i + 1
            end

            if attribute == "depends" then
                -- Dependency on another package - use its directory
                local base_package = values[1]
                local base_directory = BuildFeatureAttribute(feature, base_package .. ":directory")
                if base_directory then
                    directory = base_directory
                end
            else
                -- Set the attribute
                SetBuildFeatureAttribute(feature, attribute, values, package_alias)
            end
        end

        -- Set directory attribute for this package
        SetBuildFeatureAttribute(feature, package_alias .. ":directory", directory)
    end
end

-- ============================================================================
-- Architecture Feature Initialization
-- ============================================================================

--[[
    InitArchitectureBuildFeatures(architecture)

    Enables build features derived from the architecture.

    Enables:
    - The target architecture itself as a feature
    - "primary" for the primary architecture
    - "secondary_<arch>" for secondary architectures

    Equivalent to Jam:
        rule InitArchitectureBuildFeatures { }

    Parameters:
        architecture - Target architecture name
]]
function InitArchitectureBuildFeatures(architecture)
    -- Save and set architecture
    local saved_arch = _target_packaging_arch
    _target_packaging_arch = architecture

    -- Get target arch for this packaging arch
    local target_arch = config.get("target_arch_" .. architecture) or architecture

    -- Enable the architecture as a feature
    EnableBuildFeatures(target_arch)

    -- Check if this is the primary architecture
    local packaging_archs = config.get("target_packaging_archs") or {architecture}
    if type(packaging_archs) ~= "table" then
        packaging_archs = {packaging_archs}
    end

    if packaging_archs[1] == architecture then
        EnableBuildFeatures("primary")
    end

    -- Enable secondary architecture features
    for i = 2, #packaging_archs do
        EnableBuildFeatures("secondary_" .. packaging_archs[i])
    end

    -- Restore architecture
    _target_packaging_arch = saved_arch
end

-- ============================================================================
-- Utility Functions
-- ============================================================================

--[[
    PrintEnabledFeatures()

    Debug function to print all enabled build features.
]]
function PrintEnabledFeatures()
    print("Enabled build features:")
    for feature, enabled in pairs(_build_features) do
        if enabled then
            print("  " .. feature)
        end
    end
end

--[[
    ClearBuildFeatures()

    Clears all build features (for testing).
]]
function ClearBuildFeatures()
    _build_features = {}
    _feature_objects = {}
    _feature_attributes = {}
    _next_unique_id = 1
end