--[[
    HeadersRules.lua - Haiku header search path management

    xmake equivalent of build/jam/HeadersRules (1:1 migration)

    Functions that return header directory paths:
    - PublicHeaders(groups)          - headers/os/<group> dirs
    - PrivateHeaders(groups)         - headers/private/<group> dirs
    - PrivateBuildHeaders(groups)    - headers/build/private/<group> dirs
    - LibraryHeaders(groups)         - headers/libs/<group> dirs
    - ArchHeaders(arch)              - headers/private/kernel/arch/<arch>
    - FStandardOSHeaders()           - All standard public OS header dirs
    - FStandardHeaders(arch, lang)   - All standard headers for an architecture

    Functions that apply headers to targets:
    - UseHeaders(target, headers, system)
    - UsePublicHeaders(target, groups)
    - UsePrivateHeaders(target, groups, system)
    - UsePrivateBuildHeaders(target, groups, system)
    - UseArchHeaders(target, arch)
    - UseLibraryHeaders(target, groups)
    - UseLegacyHeaders(target, groups)
    - UsePosixHeaders(target)
    - UseCppUnitHeaders(target)
    - UsePrivateKernelHeaders(target)
    - UsePrivateSystemHeaders(target)
    - UseBuildFeatureHeaders(target, feature, attribute)

    Usage:
        import("rules.HeadersRules")
        local dirs = HeadersRules.PublicHeaders({"app", "interface", "support"})
        target:add("sysincludedirs", table.unpack(dirs))
]]

-- Haiku top directory (set once, used everywhere)
local _haiku_top = nil

local function _get_haiku_top()
    if not _haiku_top then
        local ok, config = pcall(import, "core.project.config")
        if ok and config then
            _haiku_top = config.get("haiku_top")
        end
        _haiku_top = _haiku_top or os.projectdir()
    end
    return _haiku_top
end

-- PublicHeaders: returns paths for public OS headers
-- e.g., PublicHeaders({"app", "interface"}) -> {".../headers/os/app", ".../headers/os/interface"}
function PublicHeaders(groups)
    local top = _get_haiku_top()
    local dirs = {}
    for _, group in ipairs(groups) do
        table.insert(dirs, path.join(top, "headers", "os", group))
    end
    return dirs
end

-- PrivateHeaders: returns paths for private headers
function PrivateHeaders(groups)
    local top = _get_haiku_top()
    local dirs = {}
    for _, group in ipairs(groups) do
        table.insert(dirs, path.join(top, "headers", "private", group))
    end
    return dirs
end

-- PrivateBuildHeaders: returns paths for private build headers
function PrivateBuildHeaders(groups)
    local top = _get_haiku_top()
    local dirs = {}
    for _, group in ipairs(groups) do
        table.insert(dirs, path.join(top, "headers", "build", "private", group))
    end
    return dirs
end

-- LibraryHeaders: returns paths for library headers
function LibraryHeaders(groups)
    local top = _get_haiku_top()
    local dirs = {}
    for _, group in ipairs(groups) do
        table.insert(dirs, path.join(top, "headers", "libs", group))
    end
    return dirs
end

-- ArchHeaders: returns path for architecture-specific kernel headers
function ArchHeaders(arch)
    local top = _get_haiku_top()
    return path.join(top, "headers", "private", "kernel", "arch", arch)
end

-- FStandardOSHeaders: returns all standard public OS header directories
function FStandardOSHeaders()
    local top = _get_haiku_top()
    local os_includes = {
        "add-ons", "add-ons/file_system", "add-ons/graphics",
        "add-ons/input_server", "add-ons/registrar",
        "add-ons/screen_saver", "add-ons/tracker",
        "app", "device", "drivers", "game", "interface",
        "kernel", "locale", "media", "mail", "midi", "midi2",
        "net", "render", "spektr", "storage", "support",
        "surface", "translation"
    }

    local dirs = {path.join(top, "headers", "os")}
    for _, inc in ipairs(os_includes) do
        table.insert(dirs, path.join(top, "headers", "os", inc))
    end
    return dirs
end

-- FStandardHeaders: returns all standard headers for a given architecture
-- language: "C" or "C++"
function FStandardHeaders(architecture, language)
    import("core.project.config")

    local top = _get_haiku_top()
    local output_dir = config.get("output_dir") or path.join(top, "generated")
    local headers = {}

    -- Clang headers if using clang
    local is_clang = config.get("haiku_cc_is_clang_" .. architecture)
    if is_clang then
        table.insert(headers, path.join(output_dir, "clang_headers"))
    end

    -- C++ headers (ordering matters: we fill things like limits.h first)
    if language == "C++" then
        local cxx_dirs = config.get("cxx_header_dirs_" .. architecture) or {}
        if type(cxx_dirs) == "table" then
            for _, d in ipairs(cxx_dirs) do table.insert(headers, d) end
        elseif cxx_dirs ~= "" then
            table.insert(headers, cxx_dirs)
        end
    end

    -- glibc headers
    table.insert(headers, path.join(top, "headers", "glibc"))

    -- POSIX headers
    table.insert(headers, path.join(top, "headers", "posix"))

    -- GCC headers
    local gcc_dirs = config.get("gcc_header_dirs_" .. architecture) or {}
    if type(gcc_dirs) == "table" then
        for _, d in ipairs(gcc_dirs) do table.insert(headers, d) end
    elseif gcc_dirs ~= "" then
        table.insert(headers, gcc_dirs)
    end

    -- Root headers dir (allows includes like <posix/string.h>)
    table.insert(headers, path.join(top, "headers"))

    -- Standard OS headers
    local os_headers = FStandardOSHeaders()
    for _, d in ipairs(os_headers) do table.insert(headers, d) end

    -- Private headers root
    table.insert(headers, path.join(top, "headers", "private"))

    return headers
end

-- UseHeaders: add include directories to a target
-- If system is true, adds as system include dirs (-isystem),
-- otherwise as local include dirs (-I)
function UseHeaders(target, headers, system)
    if not headers or #headers == 0 then return end

    if system then
        for _, h in ipairs(headers) do
            target:add("sysincludedirs", h)
        end
    else
        for _, h in ipairs(headers) do
            target:add("includedirs", h)
        end
    end
end

-- UsePublicHeaders: add public OS headers (as system includes)
function UsePublicHeaders(target, groups)
    UseHeaders(target, PublicHeaders(groups), true)
end

-- UsePrivateHeaders: add private headers (default: system includes)
function UsePrivateHeaders(target, groups, system)
    if system == nil then system = true end
    UseHeaders(target, PrivateHeaders(groups), system)
end

-- UsePrivateBuildHeaders: add private build headers (default: system includes)
function UsePrivateBuildHeaders(target, groups, system)
    if system == nil then system = true end
    UseHeaders(target, PrivateBuildHeaders(groups), system)
end

-- UseArchHeaders: add architecture-specific kernel headers
function UseArchHeaders(target, arch)
    UseHeaders(target, {ArchHeaders(arch)}, true)
end

-- UseLibraryHeaders: add library headers (as system includes)
function UseLibraryHeaders(target, groups)
    UseHeaders(target, LibraryHeaders(groups), true)
end

-- UseLegacyHeaders: add legacy compatibility headers
function UseLegacyHeaders(target, groups)
    local top = _get_haiku_top()
    local dirs = {}
    for _, group in ipairs(groups) do
        table.insert(dirs, path.join(top, "headers", "legacy", group))
    end
    UseHeaders(target, dirs, true)
end

-- UsePosixHeaders: add POSIX headers
function UsePosixHeaders(target)
    local top = _get_haiku_top()
    UseHeaders(target, {path.join(top, "headers", "posix")}, true)
end

-- UseCppUnitHeaders: add CppUnit test framework headers
function UseCppUnitHeaders(target)
    local top = _get_haiku_top()
    UseHeaders(target, {path.join(top, "headers", "tools", "cppunit")}, true)
end

-- UsePrivateKernelHeaders: add private kernel headers
function UsePrivateKernelHeaders(target)
    import("core.project.config")
    local headers = config.get("private_kernel_headers")
    if headers then
        if type(headers) == "table" then
            UseHeaders(target, headers, true)
        else
            UseHeaders(target, {headers}, true)
        end
    end
end

-- UsePrivateSystemHeaders: add private system headers for target arch
function UsePrivateSystemHeaders(target)
    import("core.project.config")
    local arch = target:data("haiku.packaging_arch")
        or config.get("target_packaging_arch")
        or config.get("arch") or "x86_64"
    local headers = config.get("private_system_headers_" .. arch)
    if headers then
        if type(headers) == "table" then
            UseHeaders(target, headers, true)
        else
            UseHeaders(target, {headers}, true)
        end
    end
end

-- UseBuildFeatureHeaders: add headers from a build feature
function UseBuildFeatureHeaders(target, feature, attribute)
    import("core.project.config")
    attribute = attribute or "headers"
    local headers = config.get("feature_" .. feature .. "_" .. attribute)
    if headers then
        if type(headers) == "table" then
            UseHeaders(target, headers, true)
        else
            UseHeaders(target, {headers}, true)
        end
    end
end
