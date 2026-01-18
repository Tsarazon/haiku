--[[
    OverriddenJamRules.lua - Core build rules (overridden Jam rules)

    xmake equivalent of build/jam/OverriddenJamRules

    This module provides overridden versions of core Jam rules that were
    customized for Haiku's build system. In Jam, these rules override the
    built-in behavior. In xmake, we implement equivalent functionality
    using xmake's native mechanisms where possible.

    Rules defined:
    - Link            - Link executables with resource and version support
    - Object          - Compile source files to objects
    - As              - Assemble .s/.S/.asm files
    - Cc              - Compile C source files
    - Cxx             - Compile C++ source files (C++ in Jam)
    - Lex             - Generate C/C++ from .l files (flex)
    - Yacc            - Generate C/C++ from .y files (bison)
    - Archive         - Create static library archives
    - Library         - Build static library from sources
    - LibraryFromObjects - Build static library from objects
    - Main            - Build executable from sources
    - MainFromObjects - Build executable from objects
    - MakeLocate      - Set output directory for targets
    - MkDir           - Create directories
    - ObjectCcFlags   - Add C flags to specific objects
    - ObjectCxxFlags  - Add C++ flags to specific objects
    - ObjectDefines   - Add defines to specific objects
    - ObjectHdrs      - Add headers to specific objects
    - SubInclude      - Include subdirectory build files

    Note: Many of these rules are already handled by xmake's built-in
    functionality. This module provides compatibility functions and
    Haiku-specific extensions.
]]

-- ============================================================================
-- Module State
-- ============================================================================

-- Track per-object flags
local _object_ccflags = {}
local _object_cxxflags = {}
local _object_defines = {}
local _object_hdrs = {}

-- Track created directories to avoid duplicates
local _created_dirs = {}

-- Track located targets
local _target_locations = {}

-- ============================================================================
-- Platform Detection
-- ============================================================================

--[[
    IsHostPlatform(target)

    Check if target is being built for the host platform.

    Parameters:
        target - Target name or target object

    Returns:
        true if building for host platform
]]
function IsHostPlatform(target)
    local platform = nil
    if type(target) == "table" and target.get then
        platform = target:get("PLATFORM")
    elseif type(target) == "string" then
        platform = _target_locations[target] and _target_locations[target].platform
    end
    return platform == "host"
end

--[[
    GetTargetArchitecture(target)

    Get the packaging architecture for a target.

    Parameters:
        target - Target name or target object

    Returns:
        Architecture string (e.g., "x86_64")
]]
function GetTargetArchitecture(target)
    local arch = nil
    if type(target) == "table" and target.get then
        arch = target:get("TARGET_PACKAGING_ARCH")
    end
    return arch or get_config("arch") or "x86_64"
end

-- ============================================================================
-- Compiler/Linker Selection
-- ============================================================================

--[[
    GetLinker(target)

    Get the linker command for a target.

    Parameters:
        target - Target name or object

    Returns:
        Linker command string
]]
function GetLinker(target)
    if IsHostPlatform(target) then
        return get_config("HOST_LINK") or "$(CC)"
    else
        local arch = GetTargetArchitecture(target)
        return get_config("TARGET_LINK_" .. arch) or "$(CC)"
    end
end

--[[
    GetLinkFlags(target)

    Get the linker flags for a target.

    Parameters:
        target - Target name or object

    Returns:
        Table of linker flags
]]
function GetLinkFlags(target)
    local flags = {}
    if IsHostPlatform(target) then
        local host_flags = get_config("HOST_LINKFLAGS")
        if host_flags then
            if type(host_flags) == "table" then
                for _, f in ipairs(host_flags) do
                    table.insert(flags, f)
                end
            else
                table.insert(flags, host_flags)
            end
        end
    else
        local arch = GetTargetArchitecture(target)
        local target_flags = get_config("TARGET_LINKFLAGS_" .. arch)
        if target_flags then
            if type(target_flags) == "table" then
                for _, f in ipairs(target_flags) do
                    table.insert(flags, f)
                end
            else
                table.insert(flags, target_flags)
            end
        end
    end
    return flags
end

--[[
    GetCCompiler(target)

    Get the C compiler for a target.

    Parameters:
        target - Target name or object

    Returns:
        C compiler command string
]]
function GetCCompiler(target)
    if IsHostPlatform(target) then
        return get_config("HOST_CC") or "cc"
    else
        local arch = GetTargetArchitecture(target)
        return get_config("TARGET_CC_" .. arch) or "cc"
    end
end

--[[
    GetCxxCompiler(target)

    Get the C++ compiler for a target.

    Parameters:
        target - Target name or object

    Returns:
        C++ compiler command string
]]
function GetCxxCompiler(target)
    if IsHostPlatform(target) then
        return get_config("HOST_CXX") or "c++"
    else
        local arch = GetTargetArchitecture(target)
        return get_config("TARGET_CXX_" .. arch) or "c++"
    end
end

--[[
    GetArchiver(target)

    Get the archiver for a target.

    Parameters:
        target - Target name or object

    Returns:
        Archiver command string
]]
function GetArchiver(target)
    if IsHostPlatform(target) then
        return get_config("HOST_AR") or "ar"
    else
        local arch = GetTargetArchitecture(target)
        return get_config("TARGET_AR_" .. arch) or "ar"
    end
end

--[[
    GetRanlib(target)

    Get the ranlib command for a target.

    Parameters:
        target - Target name or object

    Returns:
        Ranlib command string or nil
]]
function GetRanlib(target)
    if IsHostPlatform(target) then
        return get_config("HOST_RANLIB") or "ranlib"
    else
        local arch = GetTargetArchitecture(target)
        return get_config("TARGET_RANLIB_" .. arch) or "ranlib"
    end
end

-- ============================================================================
-- Compiler Flags
-- ============================================================================

--[[
    GetCFlags(target, debug_level, warnings)

    Get C compiler flags for a target.

    Parameters:
        target      - Target name or object
        debug_level - Debug level (0, 1, etc.)
        warnings    - Warning level (0, 1, "treatAsErrors")

    Returns:
        Table of compiler flags
]]
function GetCFlags(target, debug_level, warnings)
    local flags = {}
    debug_level = debug_level or 0
    warnings = warnings or 1

    -- Optimization flags
    if debug_level == 0 then
        local optim = get_config("OPTIM") or "-O2"
        table.insert(flags, optim)
    else
        table.insert(flags, "-O0")
    end

    if IsHostPlatform(target) then
        -- Warning flags
        if warnings ~= 0 then
            local warn_flags = get_config("HOST_WARNING_CCFLAGS")
            if warn_flags then
                if type(warn_flags) == "table" then
                    for _, f in ipairs(warn_flags) do table.insert(flags, f) end
                else
                    table.insert(flags, warn_flags)
                end
            end
            if warnings == "treatAsErrors" then
                table.insert(flags, "-Werror")
                local werror_flags = get_config("HOST_WERROR_FLAGS")
                if werror_flags then
                    if type(werror_flags) == "table" then
                        for _, f in ipairs(werror_flags) do table.insert(flags, f) end
                    else
                        table.insert(flags, werror_flags)
                    end
                end
            end
        end

        -- Debug and other flags
        local host_ccflags = get_config("HOST_CCFLAGS")
        if host_ccflags then
            if type(host_ccflags) == "table" then
                for _, f in ipairs(host_ccflags) do table.insert(flags, f) end
            else
                table.insert(flags, host_ccflags)
            end
        end

        local debug_flags = get_config("HOST_DEBUG_" .. debug_level .. "_CCFLAGS")
        if debug_flags then
            if type(debug_flags) == "table" then
                for _, f in ipairs(debug_flags) do table.insert(flags, f) end
            else
                table.insert(flags, debug_flags)
            end
        end
    else
        local arch = GetTargetArchitecture(target)

        -- Warning flags
        if warnings ~= 0 then
            local warn_flags = get_config("TARGET_WARNING_CCFLAGS_" .. arch)
            if warn_flags then
                if type(warn_flags) == "table" then
                    for _, f in ipairs(warn_flags) do table.insert(flags, f) end
                else
                    table.insert(flags, warn_flags)
                end
            end
            if warnings == "treatAsErrors" then
                table.insert(flags, "-Werror")
                local werror_flags = get_config("TARGET_WERROR_FLAGS_" .. arch)
                if werror_flags then
                    if type(werror_flags) == "table" then
                        for _, f in ipairs(werror_flags) do table.insert(flags, f) end
                    else
                        table.insert(flags, werror_flags)
                    end
                end
            end
        end

        -- Debug and other flags
        local target_ccflags = get_config("TARGET_CCFLAGS_" .. arch)
        if target_ccflags then
            if type(target_ccflags) == "table" then
                for _, f in ipairs(target_ccflags) do table.insert(flags, f) end
            else
                table.insert(flags, target_ccflags)
            end
        end

        local debug_flags = get_config("TARGET_DEBUG_" .. debug_level .. "_CCFLAGS_" .. arch)
        if debug_flags then
            if type(debug_flags) == "table" then
                for _, f in ipairs(debug_flags) do table.insert(flags, f) end
            else
                table.insert(flags, debug_flags)
            end
        end
    end

    return flags
end

--[[
    GetCxxFlags(target, debug_level, warnings)

    Get C++ compiler flags for a target.

    Parameters:
        target      - Target name or object
        debug_level - Debug level (0, 1, etc.)
        warnings    - Warning level (0, 1, "treatAsErrors")

    Returns:
        Table of compiler flags
]]
function GetCxxFlags(target, debug_level, warnings)
    local flags = {}
    debug_level = debug_level or 0
    warnings = warnings or 1

    -- Optimization flags
    if debug_level == 0 then
        local optim = get_config("OPTIM") or "-O2"
        table.insert(flags, optim)
    else
        table.insert(flags, "-O0")
    end

    if IsHostPlatform(target) then
        -- Warning flags
        if warnings ~= 0 then
            local warn_flags = get_config("HOST_WARNING_CXXFLAGS")
            if warn_flags then
                if type(warn_flags) == "table" then
                    for _, f in ipairs(warn_flags) do table.insert(flags, f) end
                else
                    table.insert(flags, warn_flags)
                end
            end
            if warnings == "treatAsErrors" then
                table.insert(flags, "-Werror")
                local werror_flags = get_config("HOST_WERROR_FLAGS")
                if werror_flags then
                    if type(werror_flags) == "table" then
                        for _, f in ipairs(werror_flags) do table.insert(flags, f) end
                    else
                        table.insert(flags, werror_flags)
                    end
                end
            end
        end

        -- Debug and other flags
        local host_cxxflags = get_config("HOST_CXXFLAGS")
        if host_cxxflags then
            if type(host_cxxflags) == "table" then
                for _, f in ipairs(host_cxxflags) do table.insert(flags, f) end
            else
                table.insert(flags, host_cxxflags)
            end
        end

        local debug_flags = get_config("HOST_DEBUG_" .. debug_level .. "_CXXFLAGS")
        if debug_flags then
            if type(debug_flags) == "table" then
                for _, f in ipairs(debug_flags) do table.insert(flags, f) end
            else
                table.insert(flags, debug_flags)
            end
        end
    else
        local arch = GetTargetArchitecture(target)

        -- Warning flags
        if warnings ~= 0 then
            local warn_flags = get_config("TARGET_WARNING_CXXFLAGS_" .. arch)
            if warn_flags then
                if type(warn_flags) == "table" then
                    for _, f in ipairs(warn_flags) do table.insert(flags, f) end
                else
                    table.insert(flags, warn_flags)
                end
            end
            if warnings == "treatAsErrors" then
                table.insert(flags, "-Werror")
                local werror_flags = get_config("TARGET_WERROR_FLAGS_" .. arch)
                if werror_flags then
                    if type(werror_flags) == "table" then
                        for _, f in ipairs(werror_flags) do table.insert(flags, f) end
                    else
                        table.insert(flags, werror_flags)
                    end
                end
            end
        end

        -- Debug and other flags
        local target_cxxflags = get_config("TARGET_CXXFLAGS_" .. arch)
        if target_cxxflags then
            if type(target_cxxflags) == "table" then
                for _, f in ipairs(target_cxxflags) do table.insert(flags, f) end
            else
                table.insert(flags, target_cxxflags)
            end
        end

        local debug_flags = get_config("TARGET_DEBUG_" .. debug_level .. "_CXXFLAGS_" .. arch)
        if debug_flags then
            if type(debug_flags) == "table" then
                for _, f in ipairs(debug_flags) do table.insert(flags, f) end
            else
                table.insert(flags, debug_flags)
            end
        end
    end

    return flags
end

--[[
    GetAsFlags(target)

    Get assembler flags for a target.

    Parameters:
        target - Target name or object

    Returns:
        Table of assembler flags
]]
function GetAsFlags(target)
    local flags = {}

    if IsHostPlatform(target) then
        local host_asflags = get_config("HOST_ASFLAGS")
        if host_asflags then
            if type(host_asflags) == "table" then
                for _, f in ipairs(host_asflags) do table.insert(flags, f) end
            else
                table.insert(flags, host_asflags)
            end
        end
    else
        local arch = GetTargetArchitecture(target)
        local target_asflags = get_config("TARGET_ASFLAGS_" .. arch)
        if target_asflags then
            if type(target_asflags) == "table" then
                for _, f in ipairs(target_asflags) do table.insert(flags, f) end
            else
                table.insert(flags, target_asflags)
            end
        end
    end

    return flags
end

-- ============================================================================
-- Include Path Formatting
-- ============================================================================

--[[
    GetIncludesSeparator(target)

    Get the includes separator for a target.

    Parameters:
        target - Target name or object

    Returns:
        Separator string or empty string
]]
function GetIncludesSeparator(target)
    if IsHostPlatform(target) then
        return get_config("HOST_INCLUDES_SEPARATOR") or ""
    else
        local arch = GetTargetArchitecture(target)
        return get_config("TARGET_INCLUDES_SEPARATOR_" .. arch) or ""
    end
end

--[[
    GetLocalIncludesOption(target)

    Get the local includes option for a target.

    Parameters:
        target - Target name or object

    Returns:
        Option string (e.g., "-I")
]]
function GetLocalIncludesOption(target)
    if IsHostPlatform(target) then
        return get_config("HOST_LOCAL_INCLUDES_OPTION") or "-I"
    else
        local arch = GetTargetArchitecture(target)
        return get_config("TARGET_LOCAL_INCLUDES_OPTION_" .. arch) or "-I"
    end
end

--[[
    GetSystemIncludesOption(target)

    Get the system includes option for a target.

    Parameters:
        target - Target name or object

    Returns:
        Option string (e.g., "-isystem")
]]
function GetSystemIncludesOption(target)
    if IsHostPlatform(target) then
        return get_config("HOST_SYSTEM_INCLUDES_OPTION") or "-isystem"
    else
        local arch = GetTargetArchitecture(target)
        return get_config("TARGET_SYSTEM_INCLUDES_OPTION_" .. arch) or "-isystem"
    end
end

--[[
    FIncludes(dirs, option)

    Format include directories with the given option.

    Parameters:
        dirs   - Table of directory paths
        option - Include option (e.g., "-I")

    Returns:
        Formatted include string
]]
function FIncludes(dirs, option)
    option = option or "-I"
    if type(dirs) == "string" then
        dirs = {dirs}
    end

    local result = {}
    for _, dir in ipairs(dirs or {}) do
        table.insert(result, option .. dir)
    end
    return table.concat(result, " ")
end

--[[
    FSysIncludes(dirs, option)

    Format system include directories with the given option.

    Parameters:
        dirs   - Table of directory paths
        option - Include option (e.g., "-isystem")

    Returns:
        Formatted include string
]]
function FSysIncludes(dirs, option)
    option = option or "-isystem"
    return FIncludes(dirs, option)
end

--[[
    FDefines(defines)

    Format defines with -D prefix.

    Parameters:
        defines - Table of define strings

    Returns:
        Formatted defines string
]]
function FDefines(defines)
    if type(defines) == "string" then
        defines = {defines}
    end

    local result = {}
    for _, def in ipairs(defines or {}) do
        table.insert(result, "-D" .. def)
    end
    return table.concat(result, " ")
end

-- ============================================================================
-- Per-Object Flags Management
-- ============================================================================

--[[
    ObjectCcFlags(sources, flags)

    Add C compiler flags to specific source/object files.

    Equivalent to Jam:
        rule ObjectCcFlags { }

    Parameters:
        sources - Source file(s) (string or table)
        flags   - Flags to add (string or table)
]]
function ObjectCcFlags(sources, flags)
    if type(sources) == "string" then
        sources = {sources}
    end
    if type(flags) == "string" then
        flags = {flags}
    end

    for _, src in ipairs(sources) do
        local obj = src:gsub("%.[^%.]+$", ".o")
        _object_ccflags[obj] = _object_ccflags[obj] or {}
        for _, f in ipairs(flags) do
            table.insert(_object_ccflags[obj], f)
        end
    end
end

--[[
    ObjectCxxFlags(sources, flags)

    Add C++ compiler flags to specific source/object files.

    Equivalent to Jam:
        rule ObjectC++Flags { }

    Parameters:
        sources - Source file(s) (string or table)
        flags   - Flags to add (string or table)
]]
function ObjectCxxFlags(sources, flags)
    if type(sources) == "string" then
        sources = {sources}
    end
    if type(flags) == "string" then
        flags = {flags}
    end

    for _, src in ipairs(sources) do
        local obj = src:gsub("%.[^%.]+$", ".o")
        _object_cxxflags[obj] = _object_cxxflags[obj] or {}
        for _, f in ipairs(flags) do
            table.insert(_object_cxxflags[obj], f)
        end
    end
end

--[[
    ObjectDefines(sources, defines)

    Add defines to specific source/object files.

    Equivalent to Jam:
        rule ObjectDefines { }

    Parameters:
        sources - Source file(s) (string or table)
        defines - Defines to add (string or table)
]]
function ObjectDefines(sources, defines)
    if not defines then
        return
    end

    if type(sources) == "string" then
        sources = {sources}
    end
    if type(defines) == "string" then
        defines = {defines}
    end

    for _, src in ipairs(sources) do
        local obj = src:gsub("%.[^%.]+$", ".o")
        _object_defines[obj] = _object_defines[obj] or {}
        for _, d in ipairs(defines) do
            table.insert(_object_defines[obj], d)
        end
    end
end

--[[
    ObjectHdrs(sources, headers)

    Add include directories to specific source/object files.

    Equivalent to Jam:
        rule ObjectHdrs { }

    Parameters:
        sources - Source file(s) (string or table)
        headers - Header directories to add (string or table)
]]
function ObjectHdrs(sources, headers)
    if type(sources) == "string" then
        sources = {sources}
    end
    if type(headers) == "string" then
        headers = {headers}
    end

    for _, src in ipairs(sources) do
        local obj = src:gsub("%.[^%.]+$", ".o")
        _object_hdrs[obj] = _object_hdrs[obj] or {}
        for _, h in ipairs(headers or {}) do
            table.insert(_object_hdrs[obj], h)
        end
    end
end

--[[
    GetObjectCcFlags(obj)

    Get C flags for a specific object file.

    Parameters:
        obj - Object file name

    Returns:
        Table of flags or empty table
]]
function GetObjectCcFlags(obj)
    return _object_ccflags[obj] or {}
end

--[[
    GetObjectCxxFlags(obj)

    Get C++ flags for a specific object file.

    Parameters:
        obj - Object file name

    Returns:
        Table of flags or empty table
]]
function GetObjectCxxFlags(obj)
    return _object_cxxflags[obj] or {}
end

--[[
    GetObjectDefines(obj)

    Get defines for a specific object file.

    Parameters:
        obj - Object file name

    Returns:
        Table of defines or empty table
]]
function GetObjectDefines(obj)
    return _object_defines[obj] or {}
end

--[[
    GetObjectHdrs(obj)

    Get include directories for a specific object file.

    Parameters:
        obj - Object file name

    Returns:
        Table of directories or empty table
]]
function GetObjectHdrs(obj)
    return _object_hdrs[obj] or {}
end

-- ============================================================================
-- Directory Management
-- ============================================================================

--[[
    MkDir(dir)

    Create a directory (with -p for parent directories).

    Equivalent to Jam:
        rule MkDir { }

    Parameters:
        dir - Directory path to create

    Returns:
        true if created or already exists
]]
function MkDir(dir)
    if not dir or dir == "" or dir == "." then
        return true
    end

    -- Check if already created
    if _created_dirs[dir] then
        return true
    end

    -- Create directory with parents
    os.mkdir(dir)
    _created_dirs[dir] = true
    return true
end

--[[
    MakeLocate(targets, dir)

    Set the output directory for targets.

    Equivalent to Jam:
        rule MakeLocate { }

    Parameters:
        targets - Target(s) to locate (string or table)
        dir     - Directory to locate targets in
]]
function MakeLocate(targets, dir)
    if not dir then
        return
    end

    if type(targets) == "string" then
        targets = {targets}
    end

    for _, target in ipairs(targets) do
        _target_locations[target] = _target_locations[target] or {}
        _target_locations[target].dir = dir

        -- Ensure directory exists
        MkDir(dir)
    end
end

--[[
    GetTargetLocation(target)

    Get the output directory for a target.

    Parameters:
        target - Target name

    Returns:
        Directory path or nil
]]
function GetTargetLocation(target)
    return _target_locations[target] and _target_locations[target].dir
end

-- ============================================================================
-- Source File Type Detection
-- ============================================================================

--[[
    GetSourceType(source)

    Determine the type of a source file based on extension.

    Parameters:
        source - Source file path

    Returns:
        One of: "c", "c++", "asm", "nasm", "lex", "yacc", "fortran", "object", "unknown"
]]
function GetSourceType(source)
    local ext = path.extension(source):lower()

    local type_map = {
        [".c"]    = "c",
        [".cc"]   = "c++",
        [".cpp"]  = "c++",
        [".cxx"]  = "c++",
        [".C"]    = "c++",
        [".s"]    = "asm",
        [".S"]    = "asm",
        [".asm"]  = "asm",
        [".nasm"] = "nasm",
        [".l"]    = "lex",
        [".y"]    = "yacc",
        [".f"]    = "fortran",
        [".o"]    = "object",
        [".obj"]  = "object",
    }

    return type_map[ext] or "unknown"
end

-- ============================================================================
-- Lex and Yacc Support
-- ============================================================================

--[[
    Lex(output, source, generate_cpp)

    Generate C/C++ source from a .l (lex/flex) file.

    Equivalent to Jam:
        rule Lex { }

    Parameters:
        output       - Output file path
        source       - Input .l file
        generate_cpp - If true, generate .cpp instead of .c

    Returns:
        Configuration table for the lex operation
]]
function Lex(output, source, generate_cpp)
    local ext = generate_cpp and ".cpp" or ".c"
    local output_file = output or source:gsub("%.l$", ext)

    return {
        type = "lex",
        input = source,
        output = output_file,
        generate_cpp = generate_cpp,
        command = function()
            local lex = get_config("LEX") or "flex"
            local lexflags = get_config("LEXFLAGS") or ""
            return string.format('%s %s -o"%s" "%s"', lex, lexflags, output_file, source)
        end
    }
end

--[[
    Yacc(source_output, header_output, source, generate_cpp)

    Generate C/C++ source and header from a .y (yacc/bison) file.

    Equivalent to Jam:
        rule Yacc { }

    Parameters:
        source_output - Output source file path
        header_output - Output header file path
        source        - Input .y file
        generate_cpp  - If true, generate .cpp/.hpp instead of .c/.h

    Returns:
        Configuration table for the yacc operation
]]
function Yacc(source_output, header_output, source, generate_cpp)
    local src_ext = generate_cpp and ".cpp" or ".c"
    local hdr_ext = generate_cpp and ".hpp" or ".h"

    local output_src = source_output or source:gsub("%.y$", src_ext)
    local output_hdr = header_output or source:gsub("%.y$", hdr_ext)

    return {
        type = "yacc",
        input = source,
        output_source = output_src,
        output_header = output_hdr,
        generate_cpp = generate_cpp,
        command = function()
            local yacc = get_config("YACC") or "bison"
            local yaccflags = get_config("YACCFLAGS") or "-d"
            -- Bison generates .tab.c/.tab.h by default, we may need to rename
            return string.format('%s %s -o "%s" "%s"', yacc, yaccflags, output_src, source)
        end
    }
end

-- ============================================================================
-- Assembly Support
-- ============================================================================

--[[
    As(object, source)

    Assemble a source file.

    Equivalent to Jam:
        rule As { }

    Parameters:
        object - Output object file
        source - Input assembly source

    Returns:
        Configuration table for the assembly operation
]]
function As(object, source)
    return {
        type = "as",
        input = source,
        output = object,
        get_command = function(target)
            local cc = GetCCompiler(target)
            local flags = GetAsFlags(target)
            local defines = GetObjectDefines(object)
            local hdrs = GetObjectHdrs(object)

            local cmd = string.format('%s -c "%s" -O2', cc, source)

            -- Add flags
            for _, f in ipairs(flags) do
                cmd = cmd .. " " .. f
            end

            -- Add assembler define
            cmd = cmd .. " -D_ASSEMBLER"

            -- Add defines
            if #defines > 0 then
                cmd = cmd .. " " .. FDefines(defines)
            end

            -- Add headers
            if #hdrs > 0 then
                cmd = cmd .. " " .. FIncludes(hdrs)
            end

            cmd = cmd .. string.format(' -o "%s"', object)
            return cmd
        end
    }
end

-- ============================================================================
-- C/C++ Compilation
-- ============================================================================

--[[
    Cc(object, source)

    Compile a C source file.

    Equivalent to Jam:
        rule Cc { }

    Parameters:
        object - Output object file
        source - Input C source

    Returns:
        Configuration table for the compilation
]]
function Cc(object, source)
    return {
        type = "cc",
        input = source,
        output = object,
        get_command = function(target, debug_level, warnings)
            local cc = GetCCompiler(target)
            local flags = GetCFlags(target, debug_level, warnings)
            local obj_flags = GetObjectCcFlags(object)
            local defines = GetObjectDefines(object)
            local hdrs = GetObjectHdrs(object)

            local cmd = string.format('%s -c "%s"', cc, source)

            -- Add base flags
            for _, f in ipairs(flags) do
                cmd = cmd .. " " .. f
            end

            -- Add per-object flags
            for _, f in ipairs(obj_flags) do
                cmd = cmd .. " " .. f
            end

            -- Add defines
            if #defines > 0 then
                cmd = cmd .. " " .. FDefines(defines)
            end

            -- Add headers
            if #hdrs > 0 then
                cmd = cmd .. " " .. FIncludes(hdrs)
            end

            cmd = cmd .. string.format(' -o "%s"', object)
            return cmd
        end
    }
end

--[[
    Cxx(object, source)

    Compile a C++ source file.

    Equivalent to Jam:
        rule C++ { }

    Parameters:
        object - Output object file
        source - Input C++ source

    Returns:
        Configuration table for the compilation
]]
function Cxx(object, source)
    return {
        type = "cxx",
        input = source,
        output = object,
        get_command = function(target, debug_level, warnings)
            local cxx = GetCxxCompiler(target)
            local flags = GetCxxFlags(target, debug_level, warnings)
            local obj_flags = GetObjectCxxFlags(object)
            local defines = GetObjectDefines(object)
            local hdrs = GetObjectHdrs(object)

            local cmd = string.format('%s -c "%s"', cxx, source)

            -- Add base flags
            for _, f in ipairs(flags) do
                cmd = cmd .. " " .. f
            end

            -- Add per-object flags
            for _, f in ipairs(obj_flags) do
                cmd = cmd .. " " .. f
            end

            -- Add defines
            if #defines > 0 then
                cmd = cmd .. " " .. FDefines(defines)
            end

            -- Add headers
            if #hdrs > 0 then
                cmd = cmd .. " " .. FIncludes(hdrs)
            end

            cmd = cmd .. string.format(' -o "%s"', object)
            return cmd
        end
    }
end

-- ============================================================================
-- Object Rule
-- ============================================================================

--[[
    Object(object, source)

    Compile a source file to an object file.
    Automatically selects the appropriate compiler based on source extension.

    Equivalent to Jam:
        rule Object { }

    Parameters:
        object - Output object file
        source - Input source file

    Returns:
        Configuration table for the compilation
]]
function Object(object, source)
    local src_type = GetSourceType(source)

    if src_type == "c" then
        return Cc(object, source)
    elseif src_type == "c++" then
        return Cxx(object, source)
    elseif src_type == "asm" then
        return As(object, source)
    elseif src_type == "nasm" then
        -- NASM assembly
        return {
            type = "nasm",
            input = source,
            output = object,
            command = function()
                local nasm = get_config("NASM") or "nasm"
                return string.format('%s -f elf64 -o "%s" "%s"', nasm, object, source)
            end
        }
    elseif src_type == "lex" then
        -- Generate C/C++ from lex, then compile
        -- Use .ll extension for C++ output, .l for C output
        local generate_cpp = source:match("%.ll$") ~= nil
        local c_source = source:gsub("%.ll?$", generate_cpp and ".cpp" or ".c")
        return {
            type = "lex_compile",
            lex = Lex(c_source, source, generate_cpp),
            compile = generate_cpp and Cxx(object, c_source) or Cc(object, c_source)
        }
    elseif src_type == "yacc" then
        -- Generate C/C++ from yacc, then compile
        -- Use .yy extension for C++ output, .y for C output
        local generate_cpp = source:match("%.yy$") ~= nil
        local c_source = source:gsub("%.yy?$", generate_cpp and ".cpp" or ".c")
        local h_header = source:gsub("%.yy?$", generate_cpp and ".hpp" or ".h")
        return {
            type = "yacc_compile",
            yacc = Yacc(c_source, h_header, source, generate_cpp),
            compile = generate_cpp and Cxx(object, c_source) or Cc(object, c_source)
        }
    elseif src_type == "object" then
        -- Already an object file, nothing to do
        return {
            type = "object",
            input = source,
            output = source
        }
    else
        -- Unknown type
        return {
            type = "unknown",
            input = source,
            output = object,
            error = "Unknown source file type: " .. source
        }
    end
end

-- ============================================================================
-- Archive (Static Library)
-- ============================================================================

--[[
    Archive(library, objects)

    Create a static library archive.

    Equivalent to Jam:
        actions together Archive { }

    Parameters:
        library - Output library file
        objects - Input object files

    Returns:
        Configuration table for the archive operation
]]
function Archive(library, objects)
    if type(objects) == "string" then
        objects = {objects}
    end

    return {
        type = "archive",
        output = library,
        inputs = objects,
        get_command = function(target)
            local ar = GetArchiver(target)
            local arflags = get_config("ARFLAGS") or "rcs"

            -- Force recreation to avoid stale dependencies
            local cmds = {}
            table.insert(cmds, string.format('rm -f "%s"', library))

            local obj_list = table.concat(objects, '" "')
            table.insert(cmds, string.format('%s %s "%s" "%s"', ar, arflags, library, obj_list))

            -- Ranlib if available
            local ranlib = GetRanlib(target)
            if ranlib then
                table.insert(cmds, string.format('%s "%s"', ranlib, library))
            end

            return table.concat(cmds, " && ")
        end
    }
end

--[[
    Library(name, sources)

    Build a static library from source files.

    Equivalent to Jam:
        rule Library { }

    Parameters:
        name    - Library name
        sources - Source files

    Returns:
        Configuration table for the library
]]
function Library(name, sources)
    if type(sources) == "string" then
        sources = {sources}
    end

    -- Generate object file names
    local objects = {}
    for _, src in ipairs(sources) do
        local obj = src:gsub("%.[^%.]+$", ".o")
        table.insert(objects, obj)
    end

    return {
        type = "library",
        name = name,
        sources = sources,
        objects = objects
    }
end

--[[
    LibraryFromObjects(name, objects)

    Build a static library from object files.

    Equivalent to Jam:
        rule LibraryFromObjects { }

    Parameters:
        name    - Library name
        objects - Object files

    Returns:
        Configuration table for the library
]]
function LibraryFromObjects(name, objects)
    if type(objects) == "string" then
        objects = {objects}
    end

    local lib_name = name
    if not lib_name:match("%.a$") then
        lib_name = lib_name .. ".a"
    end

    return {
        type = "library_from_objects",
        name = lib_name,
        objects = objects,
        archive = Archive(lib_name, objects)
    }
end

-- ============================================================================
-- Link (Executable)
-- ============================================================================

--[[
    Link(target, objects)

    Link an executable from object files.

    Equivalent to Jam:
        rule Link { }

    This is the overridden Link rule that handles:
    - Files with spaces in names
    - Resources (RESFILES)
    - Version information
    - MIME type setting

    Parameters:
        target  - Output executable
        objects - Input object files

    Returns:
        Configuration table for the link operation
]]
function Link(target, objects)
    if type(objects) == "string" then
        objects = {objects}
    end

    return {
        type = "link",
        output = target,
        inputs = objects,
        get_command = function(tgt, needlibs, linklibs, version_script, link_begin_glue, link_end_glue)
            local linker = GetLinker(tgt)
            local flags = GetLinkFlags(tgt)

            local cmd_parts = {linker}

            -- Add link flags
            for _, f in ipairs(flags) do
                table.insert(cmd_parts, f)
            end

            -- Output
            table.insert(cmd_parts, '-o "' .. target .. '"')

            -- Link begin glue (if any)
            if link_begin_glue then
                table.insert(cmd_parts, '"' .. link_begin_glue .. '"')
            end

            -- Objects
            for _, obj in ipairs(objects) do
                table.insert(cmd_parts, '"' .. obj .. '"')
            end

            -- Needed libraries
            if needlibs then
                if type(needlibs) == "string" then
                    table.insert(cmd_parts, '"' .. needlibs .. '"')
                else
                    for _, lib in ipairs(needlibs) do
                        table.insert(cmd_parts, '"' .. lib .. '"')
                    end
                end
            end

            -- Link libraries
            if linklibs then
                if type(linklibs) == "string" then
                    table.insert(cmd_parts, linklibs)
                else
                    for _, lib in ipairs(linklibs) do
                        table.insert(cmd_parts, lib)
                    end
                end
            end

            -- Link end glue (if any)
            if link_end_glue then
                table.insert(cmd_parts, '"' .. link_end_glue .. '"')
            end

            -- Version script
            if version_script then
                table.insert(cmd_parts, '-Wl,--version-script,' .. version_script)
            end

            return table.concat(cmd_parts, " ")
        end
    }
end

--[[
    Main(name, sources)

    Build an executable from source files.

    Equivalent to Jam:
        rule Main { }

    Parameters:
        name    - Executable name
        sources - Source files

    Returns:
        Configuration table for the executable
]]
function Main(name, sources)
    if type(sources) == "string" then
        sources = {sources}
    end

    -- Generate object file names
    local objects = {}
    for _, src in ipairs(sources) do
        local obj = src:gsub("%.[^%.]+$", ".o")
        table.insert(objects, obj)
    end

    return {
        type = "main",
        name = name,
        sources = sources,
        objects = objects
    }
end

--[[
    MainFromObjects(name, objects)

    Build an executable from object files.

    Equivalent to Jam:
        rule MainFromObjects { }

    Parameters:
        name    - Executable name
        objects - Object files

    Returns:
        Configuration table for the executable
]]
function MainFromObjects(name, objects)
    if type(objects) == "string" then
        objects = {objects}
    end

    return {
        type = "main_from_objects",
        name = name,
        objects = objects,
        link = Link(name, objects)
    }
end

-- ============================================================================
-- SubInclude
-- ============================================================================

--[[
    SubInclude(tokens)

    Include a subdirectory's build file.

    Equivalent to Jam:
        rule SubInclude { }

    In xmake, this is typically handled by includes() or add_subdirs().
    This function provides compatibility.

    Parameters:
        tokens - Directory tokens (e.g., {"HAIKU_TOP", "src", "apps"})
]]
function SubInclude(tokens)
    if type(tokens) == "string" then
        tokens = {tokens}
    end

    -- Convert tokens to path
    local dir_parts = {}
    local start_idx = 1

    -- Skip the first token if it's a variable name like HAIKU_TOP
    if tokens[1] and tokens[1]:match("^[A-Z_]+$") then
        start_idx = 2
    end

    for i = start_idx, #tokens do
        table.insert(dir_parts, tokens[i])
    end

    local subdir = table.concat(dir_parts, "/")

    -- In xmake, we would use includes() or add_subdirs()
    -- This is a placeholder that records the intention
    return {
        type = "subinclude",
        tokens = tokens,
        path = subdir
    }
end

-- ============================================================================
-- xmake Rule Integration
-- ============================================================================

--[[
    Rule: haiku_object

    xmake rule for compiling source files with Haiku-specific handling.
]]
rule("haiku_object")
    set_extensions(".c", ".cpp", ".cc", ".cxx", ".s", ".S", ".asm", ".nasm", ".l", ".y")

    on_load(function (target)
        -- Apply per-object flags from ObjectCcFlags, ObjectC++Flags, etc.
        -- This is handled during compilation
    end)

    on_build_file(function (target, sourcefile, opt)
        -- Get object file path
        local objectfile = target:objectfile(sourcefile)
        local sourcekind = path.extension(sourcefile):lower()

        -- Apply per-object customizations
        local obj_basename = path.basename(objectfile)

        local extra_cflags = GetObjectCcFlags(obj_basename)
        local extra_cxxflags = GetObjectCxxFlags(obj_basename)
        local extra_defines = GetObjectDefines(obj_basename)
        local extra_hdrs = GetObjectHdrs(obj_basename)

        -- Add to target's compile flags for this file
        if #extra_cflags > 0 then
            target:add("cflags", extra_cflags, {force = true})
        end
        if #extra_cxxflags > 0 then
            target:add("cxxflags", extra_cxxflags, {force = true})
        end
        if #extra_defines > 0 then
            target:add("defines", extra_defines)
        end
        if #extra_hdrs > 0 then
            target:add("includedirs", extra_hdrs)
        end
    end)
rule_end()

--[[
    Rule: haiku_library

    xmake rule for building static libraries with Haiku-specific handling.
]]
rule("haiku_library")
    on_load(function (target)
        target:set("kind", "static")
    end)

    before_build(function (target)
        -- Force recreation to avoid stale dependencies
        local targetfile = target:targetfile()
        if os.isfile(targetfile) then
            os.rm(targetfile)
        end
    end)

    after_build(function (target)
        -- Run ranlib if needed
        local ranlib = GetRanlib(target)
        if ranlib then
            local targetfile = target:targetfile()
            os.exec(ranlib .. ' "' .. targetfile .. '"')
        end
    end)
rule_end()

--[[
    Rule: haiku_executable

    xmake rule for building executables with Haiku-specific handling.
    Includes resource compilation, version setting, and MIME type handling.
]]
rule("haiku_executable")
    on_load(function (target)
        target:set("kind", "binary")
        target:set("HAIKU_TARGET_IS_EXECUTABLE", true)
    end)

    after_link(function (target)
        local targetfile = target:targetfile()
        if not os.isfile(targetfile) then
            return
        end

        -- Handle resources (XRes) - from BeOSRules
        local resfiles = target:get("RESFILES")
        if resfiles and #resfiles > 0 then
            XRes(target, resfiles)
        end

        -- Set type, MIME, version (if not DONT_USE_BEOS_RULES) - from BeOSRules
        if not target:get("DONT_USE_BEOS_RULES") then
            local mime_type = target:get("TARGET_MIME_TYPE")
            SetType(target, mime_type)
            SetVersion(target)
            MimeSet(target)

            -- Create app MIME entries if target has resources
            if resfiles and #resfiles > 0 then
                CreateAppMimeDBEntries(target)
            end
        end

        -- Set executable permissions
        os.exec('chmod +x "' .. targetfile .. '"')
    end)
rule_end()