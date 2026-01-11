--[[
    MainBuildRules.lua - Core build rules for Haiku OS

    xmake equivalent of build/jam/MainBuildRules

    Rules defined:
    - AddSharedObjectGlueCode
    - Application
    - StdBinCommands
    - Addon
    - Translator
    - ScreenSaver
    - StaticLibrary
    - SharedLibrary
    - MergeObject
    - LinkAgainst
    - AddResources
    - SetVersionScript
    - Ld
    - AssembleNasm
    - BuildPlatform* variants
]]

-- Note: import() must be called inside functions, not at top level
-- import("core.project.config")
-- import("core.project.project")

-- ============================================================================
-- Configuration and Platform Detection
-- ============================================================================

-- Get target platform (haiku, host, etc.)
function get_platform()
    local plat = config.get("plat")
    if plat == "haiku" then
        return "haiku"
    end
    return "host"
end

-- Get target architecture
function get_target_arch()
    return config.get("arch") or "x86_64"
end

-- Check if platform is supported for target
function is_platform_supported(target)
    local supported = target:get("supported_platforms")
    if not supported then
        return true  -- default: all platforms supported
    end
    local platform = get_platform()
    if type(supported) == "table" then
        for _, p in ipairs(supported) do
            if p == platform then
                return true
            end
        end
        return false
    end
    return supported == platform
end

-- ============================================================================
-- Glue Code Support
-- ============================================================================

--[[
    AddSharedObjectGlueCode(target, is_executable)

    Adds Haiku-specific glue code for shared objects.
    Links with -nostdlib and adds required libs manually.

    Equivalent to Jam rule:
        rule AddSharedObjectGlueCode { }
]]
function AddSharedObjectGlueCode(target, is_executable)
    local platform = get_platform()
    local arch = get_target_arch()

    if platform == "haiku" then
        local target_type = is_executable and "EXECUTABLE" or "LIBRARY"

        -- Add glue code objects
        local begin_glue = path.join("$(HAIKU_OUTPUT_DIR)",
            "objects", arch, "system", "glue",
            target_type == "EXECUTABLE" and "crti.o" or "crti.o")
        local end_glue = path.join("$(HAIKU_OUTPUT_DIR)",
            "objects", arch, "system", "glue",
            target_type == "EXECUTABLE" and "crtn.o" or "crtn.o")

        target:add("ldflags", "-nostdlib", {force = true})
        target:add("ldflags", "-Xlinker", "--no-undefined", {force = true})

        -- Standard libs (unless DONT_LINK_AGAINST_LIBROOT is set)
        if not target:get("dont_link_against_libroot") then
            target:add("deps", "libroot")
            target:add("links", "gcc")
        end

        -- Add glue code files
        target:add("linkobjects", begin_glue, end_glue)
    end

    -- Link against compatibility libraries if needed
    local compat_libs = target:get("haiku_compatibility_libs")
    if platform ~= "host" and compat_libs then
        for _, lib in ipairs(compat_libs) do
            target:add("links", lib)
        end
    end
end

-- ============================================================================
-- Application Build Rules
-- ============================================================================

--[[
    Application(name, sources, libraries, resources)

    Creates a Haiku application executable.

    Equivalent to Jam rule:
        rule Application { }

    Usage:
        Application("MyApp", {"main.cpp", "window.cpp"}, {"be", "tracker"}, {"app.rdef"})
]]
rule("Application")
    set_extensions(".cpp", ".c", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "binary")

        -- Haiku-specific: set soname to _APP_
        target:add("ldflags", "-Xlinker", "-soname=_APP_", {force = true})
    end)

    on_config(function (target)
        -- Add shared object glue code
        AddSharedObjectGlueCode(target, true)
    end)

    -- Handle resources after build
    after_build(function (target)
        local resfiles = target:get("resources")
        if resfiles then
            -- Process resources (calls ResComp and XRes)
            import("ResourceRules", {alias = "res"})
            res.AddResources(target, resfiles)
        end
    end)

--[[
    StdBinCommands(sources, libraries, resources)

    Creates multiple executables from individual source files.
    Each source file becomes a separate executable.

    Equivalent to Jam rule:
        rule StdBinCommands { }

    Usage in xmake.lua:
        for _, src in ipairs({"cat.cpp", "ls.cpp", "mkdir.cpp"}) do
            target(path.basename(src))
                add_rules("StdBinCommands")
                add_files(src)
                add_deps("be")
        end
]]
rule("StdBinCommands")
    add_deps("Application")

    on_load(function (target)
        -- Inherits everything from Application rule
    end)

-- ============================================================================
-- Addon/Plugin Build Rules
-- ============================================================================

--[[
    Addon(target, sources, libraries, is_executable)

    Creates a Haiku add-on (shared library that can optionally be executable).

    Equivalent to Jam rule:
        rule Addon { }
]]
rule("Addon")
    set_extensions(".cpp", ".c", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        local is_executable = target:get("is_executable")

        -- Determine kind based on is_executable flag
        if is_executable then
            target:set("kind", "binary")
        else
            target:set("kind", "shared")
            target:add("ldflags", "-shared", {force = true})
        end

        -- Set soname
        local soname = target:basename()
        target:add("ldflags", "-Xlinker", format('-soname="%s"', soname), {force = true})
    end)

    on_config(function (target)
        local is_executable = target:get("is_executable") or false
        AddSharedObjectGlueCode(target, is_executable)
    end)

--[[
    Translator(target, sources, libraries, is_executable)

    Creates a Haiku translator (format converter add-on).
    Wrapper around Addon rule.

    Equivalent to Jam rule:
        rule Translator { }
]]
rule("Translator")
    add_deps("Addon")

--[[
    ScreenSaver(target, sources, libraries)

    Creates a Haiku screen saver (non-executable add-on).

    Equivalent to Jam rule:
        rule ScreenSaver { }
]]
rule("ScreenSaver")
    add_deps("Addon")

    on_load(function (target)
        target:set("is_executable", false)
    end)

-- ============================================================================
-- Library Build Rules
-- ============================================================================

--[[
    StaticLibrary(name, sources, other_objects)

    Creates a static library from sources.

    Equivalent to Jam rule:
        rule StaticLibrary { }
]]
rule("StaticLibrary")
    set_extensions(".cpp", ".c", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "static")

        -- Add hidden visibility unless NO_HIDDEN_VISIBILITY is set
        if not target:get("no_hidden_visibility") then
            target:add("cxflags", "-fvisibility=hidden", {force = true})
        end
    end)

--[[
    SharedLibrary(name, sources, libraries, abi_version)

    Creates a shared library with optional ABI versioning.

    Equivalent to Jam rule:
        rule SharedLibrary { }
]]
rule("SharedLibrary")
    set_extensions(".cpp", ".c", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "shared")

        -- Handle ABI version
        local abi_version = target:get("abi_version")
        local basename = target:basename()
        local soname = basename

        if abi_version then
            soname = format("%s.%s", basename, abi_version)
        end

        target:add("ldflags", "-shared", {force = true})
        target:add("ldflags", "-Xlinker", format('-soname="%s"', soname), {force = true})
    end)

    on_config(function (target)
        AddSharedObjectGlueCode(target, false)
    end)

-- ============================================================================
-- Object Merge Rules
-- ============================================================================

--[[
    MergeObject(name, sources, other_objects)

    Compiles sources and merges object files into a single relocatable object.

    Equivalent to Jam rule:
        rule MergeObject { }
]]
rule("MergeObject")
    set_extensions(".cpp", ".c", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "object")
    end)

    after_build(function (target)
        local platform = get_platform()
        local ld

        if platform == "host" then
            ld = "ld"
        else
            local arch = get_target_arch()
            ld = format("%s-ld", arch)
        end

        -- Merge all object files into one
        local objectfiles = target:objectfiles()
        local other_objects = target:get("other_objects") or {}

        -- Combine object files
        local all_objects = {}
        for _, obj in ipairs(objectfiles) do
            table.insert(all_objects, obj)
        end
        for _, obj in ipairs(other_objects) do
            table.insert(all_objects, obj)
        end

        if #all_objects > 0 then
            local targetfile = target:targetfile()
            local args = {"-r", "-o", targetfile}
            for _, obj in ipairs(all_objects) do
                table.insert(args, obj)
            end
            os.execv(ld, args)
        end
    end)

-- ============================================================================
-- Linking Rules
-- ============================================================================

--[[
    LinkAgainst(target, libs)

    Links target against specified libraries.
    Handles library name mapping for Haiku platform.

    Equivalent to Jam rule:
        rule LinkAgainst { }
]]
function LinkAgainst(target, libs)
    if type(libs) ~= "table" then
        libs = {libs}
    end

    local platform = get_platform()

    -- Library name mapping for Haiku platform
    local lib_map = {
        be = "libbe.so",
        root = "libroot.so",
        network = "libnetwork.so",
        bnetapi = "libbnetapi.so",
        tracker = "libtracker.so",
        translation = "libtranslation.so",
        locale = "liblocale.so",
        media = "libmedia.so",
        game = "libgame.so",
        device = "libdevice.so",
        textencoding = "libtextencoding.so",
        mail = "libmail.so",
        package = "libpackage.so",
        shared = "libshared.a",
    }

    for _, lib in ipairs(libs) do
        local mapped = lib

        -- Map library names on Haiku platform
        if platform == "haiku" and lib_map[lib] then
            mapped = lib_map[lib]
        end

        -- Determine if it's a file path or library name
        local dirname = path.directory(mapped)
        local is_file = dirname ~= "" or mapped:match("^lib") or
                        mapped:match("%.so$") or mapped:match("%.a$")

        if is_file then
            -- Add as direct dependency
            if mapped:match("%.a$") then
                target:add("links", mapped, {public = true})
            else
                target:add("links", mapped)
            end
        else
            -- Add as -l flag
            target:add("links", lib)
        end
    end
end

--[[
    Ld(target, objects, linker_script, flags)

    Links objects with a custom linker script.

    Equivalent to Jam rule:
        rule Ld { }
]]
rule("Ld")
    on_load(function (target)
        target:set("kind", "binary")

        local linker_script = target:get("linker_script")
        if linker_script then
            target:add("ldflags", format("--script=%s", linker_script), {force = true})
        end

        local platform = get_platform()
        if platform == "host" then
            -- Use host linker
        else
            local arch = get_target_arch()
            target:set("ld", format("%s-ld", arch))
        end
    end)

    after_build(function (target)
        -- Apply resources and MIME type if not disabled
        if not target:get("dont_use_beos_rules") then
            import("ResourceRules", {alias = "res", try = true})
            if res then
                res.SetType(target)
                res.MimeSet(target)
                res.SetVersion(target)
            end
        end
    end)

--[[
    SetVersionScript(target, version_script)

    Sets the version script for symbol versioning.

    Equivalent to Jam rule:
        rule SetVersionScript { }
]]
function SetVersionScript(target, version_script)
    target:add("ldflags", format("--version-script=%s", version_script), {force = true})
    target:add("deps", version_script)
end

-- ============================================================================
-- Resource Rules
-- ============================================================================

--[[
    AddResources(target, resource_files)

    Adds resource files (.rdef or .rsrc) to a target.

    Equivalent to Jam rule:
        rule AddResources { }
]]
function AddResources(target, resource_files)
    if type(resource_files) ~= "table" then
        resource_files = {resource_files}
    end

    local rsrc_files = {}

    for _, file in ipairs(resource_files) do
        if file:match("%.rdef$") then
            -- Compile .rdef to .rsrc
            local rsrc = file:gsub("%.rdef$", ".rsrc")
            -- ResComp will be called during build
            target:add("files", file, {rule = "ResComp"})
            table.insert(rsrc_files, rsrc)
        else
            table.insert(rsrc_files, file)
        end
    end

    target:set("rsrc_files", rsrc_files)
end

-- ============================================================================
-- Assembly Rules
-- ============================================================================

--[[
    AssembleNasm(target, source)

    Assembles a NASM source file.

    Equivalent to Jam rule:
        rule AssembleNasm { }
]]
rule("AssembleNasm")
    set_extensions(".asm", ".nasm")

    on_build_file(function (target, sourcefile, opt)
        local objectfile = target:objectfile(sourcefile)
        local nasmflags = target:get("nasmflags") or {"-f", "elf64"}
        local asflags = target:get("asflags") or {}

        local args = {}

        -- Add defines from asflags
        for _, flag in ipairs(asflags) do
            table.insert(args, "-d")
            table.insert(args, flag)
        end

        -- Add nasmflags
        for _, flag in ipairs(nasmflags) do
            table.insert(args, flag)
        end

        -- Add include directory
        table.insert(args, format("-I%s/", path.directory(sourcefile)))

        -- Output and input
        table.insert(args, "-o")
        table.insert(args, objectfile)
        table.insert(args, sourcefile)

        os.mkdir(path.directory(objectfile))
        os.execv("nasm", args)
    end)

-- ============================================================================
-- Build Platform Rules (Host Tools)
-- ============================================================================

--[[
    BuildPlatformMain(target, sources, libraries)

    Builds an executable for the host platform (build tools).

    Equivalent to Jam rule:
        rule BuildPlatformMain { }
]]
rule("BuildPlatformMain")
    set_extensions(".cpp", ".c", ".S", ".s")

    on_load(function (target)
        target:set("kind", "binary")
        target:set("platform", "host")
        target:set("supported_platforms", {"host"})
        target:set("dont_use_beos_rules", true)
    end)

--[[
    BuildPlatformSharedLibrary(target, sources, libraries)

    Builds a shared library for the host platform.

    Equivalent to Jam rule:
        rule BuildPlatformSharedLibrary { }
]]
rule("BuildPlatformSharedLibrary")
    add_deps("BuildPlatformMain")

    on_load(function (target)
        target:set("kind", "shared")

        -- Platform-specific flags
        if is_host("macosx") then
            target:add("ldflags", "-dynamic", "-dynamiclib", {force = true})
            target:add("ldflags", "-Xlinker", "-flat_namespace", {force = true})
        else
            target:add("ldflags", "-shared", {force = true})
            local soname = target:basename()
            target:add("ldflags", "-Xlinker", format('-soname="%s"', soname), {force = true})
        end

        -- Position independent code
        target:add("cxflags", "-fPIC", {force = true})
    end)

--[[
    BuildPlatformStaticLibrary(target, sources, other_objects)

    Builds a static library for the host platform.

    Equivalent to Jam rule:
        rule BuildPlatformStaticLibrary { }
]]
rule("BuildPlatformStaticLibrary")
    set_extensions(".cpp", ".c", ".S", ".s")

    on_load(function (target)
        target:set("kind", "static")
        target:set("platform", "host")
        target:set("supported_platforms", {"host"})
    end)

--[[
    BuildPlatformMergeObject(target, sources, other_objects)

    Builds a merged object file for the host platform.

    Equivalent to Jam rule:
        rule BuildPlatformMergeObject { }
]]
rule("BuildPlatformMergeObject")
    add_deps("MergeObject")

    on_load(function (target)
        target:set("platform", "host")
        target:set("supported_platforms", {"host"})
    end)

--[[
    BuildPlatformMergeObjectPIC(target, sources, other_objects)

    Builds a position-independent merged object for the host platform.

    Equivalent to Jam rule:
        rule BuildPlatformMergeObjectPIC { }
]]
rule("BuildPlatformMergeObjectPIC")
    add_deps("BuildPlatformMergeObject")

    on_load(function (target)
        target:add("cxflags", "-fPIC", {force = true})
    end)

--[[
    BuildPlatformStaticLibraryPIC(target, sources, other_objects)

    Builds a position-independent static library for the host platform.

    Equivalent to Jam rule:
        rule BuildPlatformStaticLibraryPIC { }
]]
rule("BuildPlatformStaticLibraryPIC")
    add_deps("BuildPlatformStaticLibrary")

    on_load(function (target)
        target:add("cxflags", "-fPIC", {force = true})
    end)

-- ============================================================================
-- Utility Functions
-- ============================================================================

--[[
    CreateAsmStructOffsetsHeader(header, source, architecture)

    Creates assembly header with structure offsets.

    Equivalent to Jam rule:
        rule CreateAsmStructOffsetsHeader { }
]]
function CreateAsmStructOffsetsHeader(header, source, architecture)
    -- This is a complex rule that generates assembly headers
    -- from C++ source by extracting #define lines from compiler output

    local arch = architecture or get_target_arch()
    local cxx = format("%s-g++", arch)

    local includes = {}
    -- Add standard headers
    table.insert(includes, format("-I%s", path.directory(source)))

    local defines = {"-Wno-invalid-offsetof"}

    -- Generate assembly and extract defines
    local cmd = format(
        "%s -S %s %s -o - | sed 's/@define/#define/g' | grep '#define' | sed -e 's/[$#]\\([0-9]\\)/\\1/' > %s",
        cxx,
        table.concat(includes, " "),
        source,
        header
    )

    os.exec(cmd)
end
