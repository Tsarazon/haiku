--[[
    HeadersRules.lua - Haiku header file management rules

    xmake equivalent of build/jam/HeadersRules

    Functions defined:

    Include path formatters:
    - FIncludes             - Format include directories with option
    - FSysIncludes          - Format system include directories

    Subdirectory header management:
    - SubDirSysHdrs         - Add system include dirs for subdirectory
    - SubDirHdrs            - Add local include dirs for subdirectory

    Object-level header management:
    - ObjectSysHdrs         - Add system includes for specific objects
    - ObjectHdrs            - Add local includes for specific objects
    - SourceHdrs            - Wrapper for ObjectHdrs with HDRSEARCH
    - SourceSysHdrs         - Wrapper for ObjectSysHdrs with HDRSEARCH

    Header directory getters:
    - PublicHeaders         - Get public header dirs (headers/os/...)
    - PrivateHeaders        - Get private header dirs (headers/private/...)
    - PrivateBuildHeaders   - Get private build header dirs
    - LibraryHeaders        - Get library header dirs (headers/libs/...)
    - ArchHeaders           - Get architecture-specific header dirs

    Header usage rules:
    - UseHeaders            - Add header dirs to subdirectory
    - UsePublicHeaders      - Add public headers
    - UsePublicObjectHeaders - Add public headers to objects
    - UsePrivateHeaders     - Add private headers
    - UsePrivateObjectHeaders - Add private headers to objects
    - UsePrivateBuildHeaders - Add private build headers
    - UseCppUnitHeaders     - Add CppUnit headers
    - UseCppUnitObjectHeaders - Add CppUnit headers to objects
    - UseArchHeaders        - Add arch-specific headers
    - UseArchObjectHeaders  - Add arch headers to objects
    - UsePosixObjectHeaders - Add POSIX headers to objects
    - UseLibraryHeaders     - Add library headers
    - UseLegacyHeaders      - Add legacy headers
    - UseLegacyObjectHeaders - Add legacy headers to objects
    - UsePrivateKernelHeaders - Add private kernel headers
    - UsePrivateSystemHeaders - Add private system headers
    - UseBuildFeatureHeaders - Add build feature headers

    Standard header collections:
    - FStandardOSHeaders    - Get standard OS header list
    - FStandardHeaders      - Get all standard headers for architecture
]]

-- import("core.project.config")

-- ============================================================================
-- Configuration
-- ============================================================================

-- Get HAIKU_TOP (project root directory)
local function get_haiku_top()
    return "$(projectdir)"
end

-- Get target architecture
local function get_target_arch()
    return config.get("arch") or "x86_64"
end

-- Check if using legacy GCC
local function is_legacy_gcc(architecture)
    -- Legacy GCC is GCC 2.x, used for BeOS compatibility
    -- Modern Haiku uses GCC 4+ or Clang
    return config.get("legacy_gcc_" .. (architecture or get_target_arch())) or false
end

-- Check if using Clang
local function is_clang(architecture)
    return config.get("clang_" .. (architecture or get_target_arch())) or false
end

-- ============================================================================
-- Subdirectory State
-- ============================================================================

local _subdir_hdrs = {}
local _subdir_syshdrs = {}

-- ============================================================================
-- Include Path Formatters
-- ============================================================================

--[[
    FIncludes(dirs, option)

    Returns include directories formatted with the specified option.

    Equivalent to Jam:
        rule FIncludes { return $(2:E="--bad-include-option ")$(1) ; }

    Parameters:
        dirs - List of directories
        option - Include option (e.g., "-I", "-iquote ")

    Returns:
        Formatted include flags
]]
function FIncludes(dirs, option)
    if type(dirs) ~= "table" then dirs = {dirs} end
    option = option or "--bad-include-option "

    local result = {}
    for _, dir in ipairs(dirs) do
        table.insert(result, option .. dir)
    end
    return result
end

--[[
    FSysIncludes(dirs, option)

    Returns system include directories formatted with the specified option.
    Counterpart of FIncludes for system include search paths.

    Equivalent to Jam:
        rule FSysIncludes { return $(2:E="--bad-include-option ")$(1) ; }
]]
function FSysIncludes(dirs, option)
    -- Same implementation as FIncludes
    return FIncludes(dirs, option)
end

-- ============================================================================
-- Subdirectory Header Management
-- ============================================================================

--[[
    SubDirSysHdrs(dirs)

    Adds directories to the system include search paths for the current
    subdirectory. Counterpart of SubDirHdrs which adds non-system include
    search paths.

    Equivalent to Jam:
        rule SubDirSysHdrs { SUBDIRSYSHDRS += [ FDirName $(1) ] ; }
]]
function SubDirSysHdrs(dirs)
    if type(dirs) ~= "table" then dirs = {dirs} end
    for _, dir in ipairs(dirs) do
        table.insert(_subdir_syshdrs, dir)
    end
end

--[[
    SubDirHdrs(dirs)

    Adds directories to the local include search paths for the current
    subdirectory.
]]
function SubDirHdrs(dirs)
    if type(dirs) ~= "table" then dirs = {dirs} end
    for _, dir in ipairs(dirs) do
        table.insert(_subdir_hdrs, dir)
    end
end

--[[
    GetSubDirHdrs()

    Returns current subdirectory local include paths.
]]
function GetSubDirHdrs()
    return _subdir_hdrs
end

--[[
    GetSubDirSysHdrs()

    Returns current subdirectory system include paths.
]]
function GetSubDirSysHdrs()
    return _subdir_syshdrs
end

--[[
    ClearSubDirHdrs()

    Clears subdirectory include paths. Called when entering a new subdirectory.
]]
function ClearSubDirHdrs()
    _subdir_hdrs = {}
    _subdir_syshdrs = {}
end

-- ============================================================================
-- Object-Level Header Management
-- ============================================================================

--[[
    ObjectSysHdrs(sources, headers, objects)

    Adds directories to the system include search paths for the given
    sources or objects.

    Equivalent to Jam:
        rule ObjectSysHdrs { }

    Parameters:
        sources - Source files or object files
        headers - Header directories to add
        objects - Optional pre-gristed object list

    Note: In xmake, this is typically done via target:add("includedirs", ...)
    or target:add("sysincludedirs", ...) if available.
]]
function ObjectSysHdrs(sources, headers, objects)
    -- In xmake, include directories are typically set at target level
    -- This function stores the mapping for later application
    if type(sources) ~= "table" then sources = {sources} end
    if type(headers) ~= "table" then headers = {headers} end

    return {
        sources = sources,
        headers = headers,
        objects = objects,
        is_system = true
    }
end

--[[
    ObjectHdrs(sources, headers, objects)

    Adds directories to the local include search paths for the given
    sources or objects.
]]
function ObjectHdrs(sources, headers, objects)
    if type(sources) ~= "table" then sources = {sources} end
    if type(headers) ~= "table" then headers = {headers} end

    return {
        sources = sources,
        headers = headers,
        objects = objects,
        is_system = false
    }
end

--[[
    SourceHdrs(sources, headers, objects)

    Wrapper for ObjectHdrs that also adjusts HDRSEARCH.

    Equivalent to Jam:
        rule SourceHdrs { ObjectHdrs... ; HDRSEARCH on $(sources) += $(headers) ; }
]]
function SourceHdrs(sources, headers, objects)
    local result = ObjectHdrs(sources, headers, objects)
    result.adjust_hdrsearch = true
    return result
end

--[[
    SourceSysHdrs(sources, headers, objects)

    Wrapper for ObjectSysHdrs that also adjusts HDRSEARCH.

    Equivalent to Jam:
        rule SourceSysHdrs { ObjectSysHdrs... ; HDRSEARCH on $(sources) += $(headers) ; }
]]
function SourceSysHdrs(sources, headers, objects)
    local result = ObjectSysHdrs(sources, headers, objects)
    result.adjust_hdrsearch = true
    return result
end

-- ============================================================================
-- Header Directory Getters
-- ============================================================================

--[[
    PublicHeaders(groups)

    Returns the directory names for the public header dirs identified by
    group list.

    Equivalent to Jam:
        rule PublicHeaders { dirs += [ FDirName $(HAIKU_TOP) headers os $(i) ] ; }

    Example:
        PublicHeaders({"app", "interface"})
        -> {"$(projectdir)/headers/os/app", "$(projectdir)/headers/os/interface"}
]]
function PublicHeaders(groups)
    if type(groups) ~= "table" then groups = {groups} end

    local haiku_top = get_haiku_top()
    local dirs = {}

    for _, group in ipairs(groups) do
        table.insert(dirs, path.join(haiku_top, "headers", "os", group))
    end

    return dirs
end

--[[
    PrivateHeaders(groups)

    Returns the directory names for the private header dirs identified by
    group list.

    Equivalent to Jam:
        rule PrivateHeaders { dirs += [ FDirName $(HAIKU_TOP) headers private $(i) ] ; }
]]
function PrivateHeaders(groups)
    if type(groups) ~= "table" then groups = {groups} end

    local haiku_top = get_haiku_top()
    local dirs = {}

    for _, group in ipairs(groups) do
        table.insert(dirs, path.join(haiku_top, "headers", "private", group))
    end

    return dirs
end

--[[
    PrivateBuildHeaders(groups)

    Returns the directory names for the private build header dirs identified
    by group list.

    Equivalent to Jam:
        rule PrivateBuildHeaders { dirs += [ FDirName $(HAIKU_TOP) headers build private $(i) ] ; }
]]
function PrivateBuildHeaders(groups)
    if type(groups) ~= "table" then groups = {groups} end

    local haiku_top = get_haiku_top()
    local dirs = {}

    for _, group in ipairs(groups) do
        table.insert(dirs, path.join(haiku_top, "headers", "build", "private", group))
    end

    return dirs
end

--[[
    LibraryHeaders(groups)

    Returns the directory names for the library header dirs identified by
    group list.

    Equivalent to Jam:
        rule LibraryHeaders { dirs += [ FDirName $(HAIKU_TOP) headers libs $(i) ] ; }
]]
function LibraryHeaders(groups)
    if type(groups) ~= "table" then groups = {groups} end

    local haiku_top = get_haiku_top()
    local dirs = {}

    for _, group in ipairs(groups) do
        table.insert(dirs, path.join(haiku_top, "headers", "libs", group))
    end

    return dirs
end

--[[
    ArchHeaders(arch)

    Returns the private kernel architecture-specific header directory.

    Equivalent to Jam:
        rule ArchHeaders { return [ FDirName $(HAIKU_TOP) headers private kernel arch $(1) ] ; }

    Example:
        ArchHeaders("x86_64") -> "$(projectdir)/headers/private/kernel/arch/x86_64"
]]
function ArchHeaders(arch)
    local haiku_top = get_haiku_top()
    return path.join(haiku_top, "headers", "private", "kernel", "arch", arch)
end

-- ============================================================================
-- Header Usage Rules
-- ============================================================================

--[[
    UseHeaders(headers, system)

    Adds the C header dirs to the header search dirs of the subdirectory.

    Equivalent to Jam:
        rule UseHeaders { if $(2) = true { SubDirSysHdrs } else { SubDirHdrs } }

    Parameters:
        headers - Header directories to add
        system - If true, add as system includes; otherwise as local includes

    Note: This rule must be invoked *before* the rule that builds the objects.
]]
function UseHeaders(headers, system)
    if type(headers) ~= "table" then headers = {headers} end

    if system then
        for _, header in ipairs(headers) do
            SubDirSysHdrs(header)
        end
    else
        for _, header in ipairs(headers) do
            SubDirHdrs(header)
        end
    end
end

--[[
    UsePublicHeaders(groups)

    Adds the public C header dirs given by group list to the header search
    dirs of the subdirectory.

    Equivalent to Jam:
        rule UsePublicHeaders { UseHeaders [ PublicHeaders $(1) ] : true ; }

    Note: This rule must be invoked *before* the rule that builds the objects.
]]
function UsePublicHeaders(groups)
    UseHeaders(PublicHeaders(groups), true)
end

--[[
    UsePublicObjectHeaders(sources, groups, objects)

    Adds the public C header dirs given by group list to the header search
    dirs of the specified source or object files.

    Equivalent to Jam:
        rule UsePublicObjectHeaders { SourceSysHdrs $(1) : [ PublicHeaders $(2) ] : $(3) ; }
]]
function UsePublicObjectHeaders(sources, groups, objects)
    return SourceSysHdrs(sources, PublicHeaders(groups), objects)
end

--[[
    UsePrivateHeaders(groups, system)

    Adds the private C header dirs given by group list to the header search
    dirs of the subdirectory.

    Equivalent to Jam:
        rule UsePrivateHeaders { UseHeaders [ PrivateHeaders $(1) ] : $(system) ; }

    Parameters:
        groups - List of private header groups
        system - Whether to add as system includes (default: true)

    Note: This rule must be invoked *before* the rule that builds the objects.
]]
function UsePrivateHeaders(groups, system)
    if system == nil then system = true end
    UseHeaders(PrivateHeaders(groups), system)
end

--[[
    UsePrivateObjectHeaders(sources, groups, objects, system)

    Adds the private C header dirs given by group list to the header search
    dirs of the specified source or object files.

    Equivalent to Jam:
        rule UsePrivateObjectHeaders { SourceSysHdrs/SourceHdrs... }

    Parameters:
        sources - Source files
        groups - List of private header groups
        objects - Optional object files
        system - Whether to add as system includes (default: true)
]]
function UsePrivateObjectHeaders(sources, groups, objects, system)
    if system == nil then system = true end

    if system then
        return SourceSysHdrs(sources, PrivateHeaders(groups), objects)
    else
        return SourceHdrs(sources, PrivateHeaders(groups), objects)
    end
end

--[[
    UsePrivateBuildHeaders(groups, system)

    Adds the private build C header dirs given by group list to the header
    search dirs of the subdirectory.

    Equivalent to Jam:
        rule UsePrivateBuildHeaders { UseHeaders [ PrivateBuildHeaders $(1) ] : $(system) ; }

    Note: This rule must be invoked *before* the rule that builds the objects.
]]
function UsePrivateBuildHeaders(groups, system)
    if system == nil then system = true end
    UseHeaders(PrivateBuildHeaders(groups), system)
end

--[[
    UseCppUnitHeaders()

    Adds CppUnit headers to subdirectory includes.

    Equivalent to Jam:
        rule UseCppUnitHeaders { SubDirSysHdrs [ FDirName $(HAIKU_TOP) headers tools cppunit ] ; }
]]
function UseCppUnitHeaders()
    local haiku_top = get_haiku_top()
    SubDirSysHdrs(path.join(haiku_top, "headers", "tools", "cppunit"))
end

--[[
    UseCppUnitObjectHeaders(sources, objects)

    Adds CppUnit headers to specific source/object files.

    Equivalent to Jam:
        rule UseCppUnitObjectHeaders { SourceSysHdrs $(1) : [ FDirName ... cppunit ] : $(2) ; }
]]
function UseCppUnitObjectHeaders(sources, objects)
    local haiku_top = get_haiku_top()
    return SourceSysHdrs(sources, {path.join(haiku_top, "headers", "tools", "cppunit")}, objects)
end

--[[
    UseArchHeaders(arch)

    Adds architecture-specific headers to subdirectory includes.

    Equivalent to Jam:
        rule UseArchHeaders { UseHeaders [ ArchHeaders $(1) ] : true ; }

    Note: This rule must be invoked *before* the rule that builds the objects.
]]
function UseArchHeaders(arch)
    UseHeaders({ArchHeaders(arch)}, true)
end

--[[
    UseArchObjectHeaders(sources, arch, objects)

    Adds architecture-specific headers to specific source/object files.

    Equivalent to Jam:
        rule UseArchObjectHeaders { SourceSysHdrs $(sources) : $(headers) : $(objects) ; }
]]
function UseArchObjectHeaders(sources, arch, objects)
    return SourceSysHdrs(sources, {ArchHeaders(arch)}, objects)
end

--[[
    UsePosixObjectHeaders(sources, objects)

    Adds the POSIX header dir to the header search dirs of the specified
    source or object files.

    Equivalent to Jam:
        rule UsePosixObjectHeaders { SourceSysHdrs $(1) : [ FDirName ... posix ] : $(2) ; }
]]
function UsePosixObjectHeaders(sources, objects)
    local haiku_top = get_haiku_top()
    return SourceSysHdrs(sources, {path.join(haiku_top, "headers", "posix")}, objects)
end

--[[
    UseLibraryHeaders(groups)

    Adds the library header dirs given by group list to the header search
    dirs of the subdirectory.

    Equivalent to Jam:
        rule UseLibraryHeaders { UseHeaders [ LibraryHeaders $(1) ] : true ; }

    Note: This rule must be invoked *before* the rule that builds the objects.
]]
function UseLibraryHeaders(groups)
    UseHeaders(LibraryHeaders(groups), true)
end

--[[
    UseLegacyHeaders(groups)

    Adds legacy headers to subdirectory includes.

    Equivalent to Jam:
        rule UseLegacyHeaders { UseHeaders [ FDirName ... legacy $(1) ] : true ; }

    Note: This rule must be invoked *before* the rule that builds the objects.
]]
function UseLegacyHeaders(groups)
    if type(groups) ~= "table" then groups = {groups} end

    local haiku_top = get_haiku_top()
    local dirs = {}

    for _, group in ipairs(groups) do
        table.insert(dirs, path.join(haiku_top, "headers", "legacy", group))
    end

    UseHeaders(dirs, true)
end

--[[
    UseLegacyObjectHeaders(sources, objects)

    Adds the legacy header dir to the header search dirs of the specified
    source or object files.

    Equivalent to Jam:
        rule UseLegacyObjectHeaders { SourceSysHdrs $(1) : [ FDirName ... legacy ] : $(2) ; }
]]
function UseLegacyObjectHeaders(sources, objects)
    local haiku_top = get_haiku_top()
    return SourceSysHdrs(sources, {path.join(haiku_top, "headers", "legacy")}, objects)
end

--[[
    UsePrivateKernelHeaders()

    Adds private kernel headers to subdirectory includes.

    Equivalent to Jam:
        rule UsePrivateKernelHeaders { UseHeaders $(TARGET_PRIVATE_KERNEL_HEADERS) : true ; }
]]
function UsePrivateKernelHeaders()
    local arch = get_target_arch()
    local haiku_top = get_haiku_top()

    local headers = {
        path.join(haiku_top, "headers", "private", "kernel"),
        path.join(haiku_top, "headers", "private", "kernel", "arch", arch),
        path.join(haiku_top, "headers", "private", "kernel", "boot"),
    }

    UseHeaders(headers, true)
end

--[[
    UsePrivateSystemHeaders()

    Adds private system headers to subdirectory includes.

    Equivalent to Jam:
        rule UsePrivateSystemHeaders { UseHeaders $(TARGET_PRIVATE_SYSTEM_HEADERS_...) : true ; }
]]
function UsePrivateSystemHeaders()
    local arch = get_target_arch()
    local haiku_top = get_haiku_top()

    local headers = {
        path.join(haiku_top, "headers", "private", "system"),
        path.join(haiku_top, "headers", "private", "system", "arch", arch),
    }

    UseHeaders(headers, true)
end

--[[
    UseBuildFeatureHeaders(feature, attribute)

    Adds build feature headers to subdirectory includes.

    Equivalent to Jam:
        rule UseBuildFeatureHeaders { UseHeaders [ BuildFeatureAttribute ... ] : true ; }

    Parameters:
        feature - Build feature name
        attribute - Attribute name (default: "headers")
]]
function UseBuildFeatureHeaders(feature, attribute)
    attribute = attribute or "headers"

    -- Get headers path from build feature
    -- This would need integration with Haiku's build feature system
    local headers = config.get("build_feature_" .. feature .. "_" .. attribute)

    if headers then
        if type(headers) ~= "table" then headers = {headers} end
        UseHeaders(headers, true)
    end
end

-- ============================================================================
-- Standard Header Collections
-- ============================================================================

--[[
    FStandardOSHeaders()

    Returns list of standard OS header directories.

    Equivalent to Jam:
        rule FStandardOSHeaders { return [ FDirName ... headers os ] [ PublicHeaders ... ] ; }
]]
function FStandardOSHeaders()
    local haiku_top = get_haiku_top()

    local os_includes = {
        "add-ons", "add-ons/file_system", "add-ons/graphics",
        "add-ons/input_server", "add-ons/registrar",
        "add-ons/screen_saver",
        "add-ons/tracker", "app", "device", "drivers", "game", "interface",
        "kernel", "locale", "media", "mail", "midi", "midi2", "net", "storage",
        "support", "translation"
    }

    local headers = {path.join(haiku_top, "headers", "os")}

    for _, inc in ipairs(os_includes) do
        table.insert(headers, path.join(haiku_top, "headers", "os", inc))
    end

    return headers
end

--[[
    FStandardHeaders(architecture, language)

    Returns all standard headers for the specified architecture.

    Equivalent to Jam:
        rule FStandardHeaders { ... }

    Parameters:
        architecture - Target architecture (e.g., "x86_64")
        language - "C" or "C++" (optional)

    Returns:
        List of all standard header directories in correct order
]]
function FStandardHeaders(architecture, language)
    architecture = architecture or get_target_arch()
    local haiku_top = get_haiku_top()
    local headers = {}

    -- Clang headers (if using Clang)
    if is_clang(architecture) then
        table.insert(headers, path.join("$(buildir)", "clang_headers"))
    end

    -- C++ headers (if C++ language)
    if language == "C++" then
        -- C++ header directories would come from GCC/Clang installation
        -- This is architecture-specific
        local cxx_headers = config.get("cxx_headers_" .. architecture)
        if cxx_headers then
            if type(cxx_headers) == "table" then
                for _, h in ipairs(cxx_headers) do
                    table.insert(headers, h)
                end
            else
                table.insert(headers, cxx_headers)
            end
        end
    end

    -- Glibc headers
    table.insert(headers, path.join(haiku_top, "headers", "glibc"))

    -- POSIX headers
    table.insert(headers, path.join(haiku_top, "headers", "posix"))

    -- GCC headers (from GCC installation)
    local gcc_headers = config.get("gcc_headers_" .. architecture)
    if gcc_headers then
        if type(gcc_headers) == "table" then
            for _, h in ipairs(gcc_headers) do
                table.insert(headers, h)
            end
        else
            table.insert(headers, gcc_headers)
        end
    end

    -- Headers directory (for paths like <posix/string.h>)
    table.insert(headers, path.join(haiku_top, "headers"))

    -- Public OS headers
    for _, h in ipairs(FStandardOSHeaders()) do
        table.insert(headers, h)
    end

    -- Private headers root
    table.insert(headers, path.join(haiku_top, "headers", "private"))

    return headers
end

-- ============================================================================
-- xmake Rule Integration
-- ============================================================================

--[[
    HaikuHeaders rule for xmake targets

    Enables automatic header directory setup for Haiku targets.

    Usage:
        target("myapp")
            add_rules("HaikuHeaders")
            -- Headers will be automatically configured
]]
rule("HaikuHeaders")
    on_load(function (target)
        local haiku_top = get_haiku_top()
        local arch = get_target_arch()

        -- Add standard OS headers
        for _, dir in ipairs(FStandardOSHeaders()) do
            target:add("includedirs", dir)
        end

        -- Add POSIX headers
        target:add("includedirs", path.join(haiku_top, "headers", "posix"))

        -- Add private headers root
        target:add("includedirs", path.join(haiku_top, "headers", "private"))

        -- Apply subdirectory headers if any
        for _, dir in ipairs(GetSubDirHdrs()) do
            target:add("includedirs", dir)
        end

        for _, dir in ipairs(GetSubDirSysHdrs()) do
            target:add("includedirs", dir)
        end
    end)

--[[
    ApplyHeadersToTarget(target, header_config)

    Applies header configuration returned by SourceHdrs/SourceSysHdrs
    to an xmake target.

    Parameters:
        target - xmake target object
        header_config - Configuration from Source*Hdrs functions
]]
function ApplyHeadersToTarget(target, header_config)
    if not header_config or not header_config.headers then
        return
    end

    for _, dir in ipairs(header_config.headers) do
        target:add("includedirs", dir)
    end
end
