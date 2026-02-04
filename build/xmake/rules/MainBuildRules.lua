----[[
    MainBuildRules.lua - Core build rules for Haiku OS

    xmake equivalent of build/jam/MainBuildRules

    Rules defined:
    - AddSharedObjectGlueCode (function)
    - Application
    - StdBinCommands
    - Addon
    - Translator
    - ScreenSaver
    - StaticLibrary
    - StaticLibraryFromObjects
    - SharedLibrary
    - SharedLibraryFromObjects
    - MergeObject
    - MergeObjectFromObjects
    - LinkAgainst (function)
    - AddResources (function)
    - SetVersionScript (function)
    - Ld
    - AssembleNasm
    - CreateAsmStructOffsetsHeader (function)
    - BuildPlatformMain
    - BuildPlatformSharedLibrary
    - BuildPlatformStaticLibrary
    - BuildPlatformStaticLibraryPIC
    - BuildPlatformMergeObject
    - BuildPlatformMergeObjectPIC
    - BootstrapStage0PlatformObjects
]]

-- ============================================================================
-- Platform and Architecture Helpers
-- ============================================================================

function get_platform(target)
    if target then
        local plat = target:get("plat")
        if plat then return plat end
    end
    local plat = get_config("plat")
    if plat == "haiku" then
        return "haiku"
    end
    return "host"
end

function get_target_arch()
    return get_config("arch") or "x86_64"
end

function is_platform_supported(target)
    local supported = target:values("supported_platforms")
    if not supported or #supported == 0 then
        return true
    end
    local platform = get_platform(target)
    for _, p in ipairs(supported) do
        if p == platform then
            return true
        end
    end
    return false
end

-- ============================================================================
-- AddSharedObjectGlueCode
-- ============================================================================

--[[
    AddSharedObjectGlueCode(target, is_executable)

    Links with -nostdlib and adds required libs manually when building for Haiku.
    Sets up glue code (crti.o/crtn.o or start_dyn.o/init_term_dyn.o).

    Jam equivalent:
        rule AddSharedObjectGlueCode { }
]]
function AddSharedObjectGlueCode(target, is_executable)
    local platform = get_platform(target)
    local arch = get_target_arch()

    if platform == "haiku" then
        local target_type = is_executable and "EXECUTABLE" or "LIBRARY"

        -- Get glue code paths from config (set by ArchitectureSetup)
        local begin_glue = get_config("HAIKU_" .. target_type .. "_BEGIN_GLUE_CODE_" .. arch)
        local end_glue = get_config("HAIKU_" .. target_type .. "_END_GLUE_CODE_" .. arch)

        -- Store for use in linking
        if begin_glue then
            target:values_set("link_begin_glue", {begin_glue})
        end
        if end_glue then
            target:values_set("link_end_glue", {end_glue})
        end

        target:add("ldflags", "-nostdlib", {force = true})
        target:add("ldflags", "-Xlinker", "--no-undefined", {force = true})

        -- Standard libs (unless DONT_LINK_AGAINST_LIBROOT is set)
        if not target:values("dont_link_against_libroot") then
            target:add("syslinks", "root")
            -- Add libgcc via TargetLibgcc equivalent
            target:add("syslinks", "gcc")
        end
    end

    -- Link against compatibility libraries if needed
    if platform ~= "host" then
        local compat_libs = get_config("TARGET_HAIKU_COMPATIBILITY_LIBS")
        if compat_libs then
            for _, lib in ipairs(compat_libs) do
                target:add("syslinks", lib)
            end
        end
    end
end

-- ============================================================================
-- Application
-- ============================================================================

--[[
    Application rule

    Creates a Haiku application executable.

    Jam equivalent:
        rule Application { }
        # Application <name> : <sources> : <libraries> : <res> ;
]]
rule("Application")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "binary")

        -- Set soname to _APP_
        target:add("ldflags", "-Xlinker", "-soname=_APP_", {force = true})
    end)

    on_config(function (target)
        AddSharedObjectGlueCode(target, true)
    end)

    -- Resources are processed via AddResources function, called from xmake.lua

-- ============================================================================
-- StdBinCommands
-- ============================================================================

--[[
    StdBinCommands rule

    For building multiple executables from individual source files.
    Each source becomes a separate executable.

    Jam equivalent:
        rule StdBinCommands { }
        # StdBinCommands <sources> : <libs> : <res> ;

    Usage in xmake.lua:
        for _, src in ipairs(sources) do
            target(path.basename(src))
                add_rules("StdBinCommands")
                add_files(src)
                add_links(...)
        end
]]
rule("StdBinCommands")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "binary")
        target:add("ldflags", "-Xlinker", "-soname=_APP_", {force = true})
    end)

    on_config(function (target)
        AddSharedObjectGlueCode(target, true)
    end)

-- ============================================================================
-- Addon
-- ============================================================================

--[[
    Addon rule

    Creates a Haiku add-on (shared library that can optionally be executable).

    Jam equivalent:
        rule Addon { }
        # Addon <target> : <sources> : <libraries> : <isExecutable> ;
]]
rule("Addon")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        local is_executable = target:values("is_executable")
        is_executable = is_executable and is_executable[1]

        if is_executable == true or is_executable == "true" then
            target:set("kind", "binary")
        else
            target:set("kind", "shared")
            target:add("ldflags", "-shared", {force = true})
        end

        -- Set soname to target name
        local soname = target:basename()
        target:add("ldflags", "-Xlinker", format('-soname="%s"', soname), {force = true})
    end)

    on_config(function (target)
        local is_executable = target:values("is_executable")
        is_executable = is_executable and is_executable[1]
        local is_exec = (is_executable == true or is_executable == "true")
        AddSharedObjectGlueCode(target, is_exec)
    end)

-- ============================================================================
-- Translator
-- ============================================================================

--[[
    Translator rule

    Creates a Haiku translator add-on.
    Wrapper around Addon rule.

    Jam equivalent:
        rule Translator { }
        # Translator <target> : <sources> : <libraries> : <isExecutable> ;
]]
rule("Translator")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        local is_executable = target:values("is_executable")
        is_executable = is_executable and is_executable[1]

        if is_executable == true or is_executable == "true" then
            target:set("kind", "binary")
        else
            target:set("kind", "shared")
            target:add("ldflags", "-shared", {force = true})
        end

        local soname = target:basename()
        target:add("ldflags", "-Xlinker", format('-soname="%s"', soname), {force = true})
    end)

    on_config(function (target)
        local is_executable = target:values("is_executable")
        is_executable = is_executable and is_executable[1]
        local is_exec = (is_executable == true or is_executable == "true")
        AddSharedObjectGlueCode(target, is_exec)
    end)

-- ============================================================================
-- ScreenSaver
-- ============================================================================

--[[
    ScreenSaver rule

    Creates a Haiku screen saver (non-executable add-on).

    Jam equivalent:
        rule ScreenSaver { }
        # ScreenSaver <target> : <sources> : <libraries> ;
]]
rule("ScreenSaver")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "shared")
        target:add("ldflags", "-shared", {force = true})

        local soname = target:basename()
        target:add("ldflags", "-Xlinker", format('-soname="%s"', soname), {force = true})
    end)

    on_config(function (target)
        AddSharedObjectGlueCode(target, false)
    end)

-- ============================================================================
-- StaticLibrary
-- ============================================================================

--[[
    StaticLibrary rule

    Creates a static library from sources.

    Jam equivalent:
        rule StaticLibrary { }
        # StaticLibrary <lib> : <sources> : <otherObjects> ;
]]
rule("StaticLibrary")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "static")

        -- Add hidden visibility unless NO_HIDDEN_VISIBILITY is set
        local no_hidden = target:values("no_hidden_visibility")
        if not no_hidden or not no_hidden[1] then
            target:add("cxflags", "-fvisibility=hidden")
        end
    end)

-- ============================================================================
-- StaticLibraryFromObjects
-- ============================================================================

--[[
    StaticLibraryFromObjects rule

    Creates a static library from pre-compiled object files.

    Jam equivalent:
        rule StaticLibraryFromObjects { }
]]
rule("StaticLibraryFromObjects")
    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "static")
    end)

-- ============================================================================
-- SharedLibrary
-- ============================================================================

--[[
    SharedLibrary rule

    Creates a shared library with optional ABI versioning.

    Jam equivalent:
        rule SharedLibrary { }
        # SharedLibrary <lib> : <sources> : <libraries> : <abiVersion> ;
]]
rule("SharedLibrary")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "shared")

        -- Handle ABI version for soname
        local abi_version = target:values("abi_version")
        abi_version = abi_version and abi_version[1]
        
        local basename = target:basename()
        local soname = basename

        if abi_version then
            soname = format("%s.%s", basename, abi_version)
            target:values_set("haiku_soname", {soname})
            target:values_set("haiku_lib_abi_version", {abi_version})
        end

        target:add("ldflags", "-shared", {force = true})
        target:add("ldflags", "-Xlinker", format('-soname="%s"', soname), {force = true})
    end)

    on_config(function (target)
        AddSharedObjectGlueCode(target, false)
    end)

-- ============================================================================
-- SharedLibraryFromObjects
-- ============================================================================

--[[
    SharedLibraryFromObjects rule

    Creates a shared library from pre-compiled object files.

    Jam equivalent:
        rule SharedLibraryFromObjects { }
        # SharedLibraryFromObjects <lib> : <objects> : <libraries> ;
]]
rule("SharedLibraryFromObjects")
    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "shared")

        local soname = target:values("haiku_soname")
        soname = soname and soname[1] or target:basename()

        target:add("ldflags", "-shared", {force = true})
        target:add("ldflags", "-Xlinker", format('-soname="%s"', soname), {force = true})
    end)

    on_config(function (target)
        AddSharedObjectGlueCode(target, false)
    end)

-- ============================================================================
-- MergeObject
-- ============================================================================

--[[
    MergeObject rule

    Compiles sources and merges object files into a single relocatable object.

    Jam equivalent:
        rule MergeObject { }
        # MergeObject <name> : <sources> : <other objects> ;
]]
rule("MergeObject")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "object")
    end)

    after_build(function (target)
        local platform = get_platform(target)
        local ld

        if platform == "host" then
            ld = os.getenv("LD") or "ld"
        else
            local arch = get_target_arch()
            ld = get_config("TARGET_LD_" .. arch) or "ld"
        end

        local objectfiles = target:objectfiles()
        local other_objects = target:values("other_objects") or {}

        -- Combine all objects
        local all_objects = {}
        for _, obj in ipairs(objectfiles) do
            table.insert(all_objects, obj)
        end
        for _, obj in ipairs(other_objects) do
            table.insert(all_objects, obj)
        end

        if #all_objects > 0 then
            local targetfile = target:targetfile()
            os.mkdir(path.directory(targetfile))

            local args = {"-r", "-o", targetfile}
            for _, obj in ipairs(all_objects) do
                table.insert(args, obj)
            end

            os.execv(ld, args)
        end
    end)

-- ============================================================================
-- MergeObjectFromObjects
-- ============================================================================

--[[
    MergeObjectFromObjects rule

    Merges pre-compiled object files into a single relocatable object.

    Jam equivalent:
        rule MergeObjectFromObjects { }
        # MergeObjectFromObjects <name> : <objects> : <other objects> ;
]]
rule("MergeObjectFromObjects")
    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end

        target:set("kind", "object")
    end)

    on_build(function (target)
        local platform = get_platform(target)
        local ld

        if platform == "host" then
            ld = os.getenv("LD") or "ld"
        else
            local arch = get_target_arch()
            ld = get_config("TARGET_LD_" .. arch) or "ld"
        end

        local objects = target:values("objects") or {}
        local other_objects = target:values("other_objects") or {}

        local all_objects = {}
        for _, obj in ipairs(objects) do
            table.insert(all_objects, obj)
        end
        for _, obj in ipairs(other_objects) do
            table.insert(all_objects, obj)
        end

        if #all_objects > 0 then
            local targetfile = target:targetfile()
            os.mkdir(path.directory(targetfile))

            local args = {"-r", "-o", targetfile}
            for _, obj in ipairs(all_objects) do
                table.insert(args, obj)
            end

            os.execv(ld, args)
        end
    end)

-- ============================================================================
-- Ld
-- ============================================================================

--[[
    Ld rule

    Links objects with optional linker script.

    Jam equivalent:
        rule Ld { }
        # Ld <name> : <objs> : <linkerscript> : <flags> ;
]]
rule("Ld")
    on_load(function (target)
        target:set("kind", "binary")
    end)

    on_config(function (target)
        local platform = get_platform(target)
        local arch = get_target_arch()

        if platform == "host" then
            target:set("toolset", "ld", os.getenv("LD") or "ld")
        else
            local ld = get_config("TARGET_LD_" .. arch) or "ld"
            target:set("toolset", "ld", ld)
        end

        -- Handle linker script
        local linker_script = target:values("linker_script")
        if linker_script and linker_script[1] then
            target:add("ldflags", "--script=" .. linker_script[1], {force = true})
        end
    end)

    after_build(function (target)
        -- XRes, SetType, MimeSet, SetVersion are handled by BeOSRules
        -- Called only if DONT_USE_BEOS_RULES is not set
        local dont_use = target:values("dont_use_beos_rules")
        if not dont_use or not dont_use[1] then
            -- Import and call BeOS rules if available
            local ok, beos = pcall(import, "BeOSRules")
            if ok and beos then
                local resfiles = target:values("resfiles")
                if resfiles and #resfiles > 0 then
                    beos.XRes(target, resfiles)
                end
                beos.SetType(target)
                beos.MimeSet(target)
                beos.SetVersion(target)
            end
        end
    end)

-- ============================================================================
-- AssembleNasm
-- ============================================================================

--[[
    AssembleNasm rule

    Assembles NASM source files.

    Jam equivalent:
        rule AssembleNasm { }
        actions AssembleNasm { $(HAIKU_NASM) $(NASMFLAGS) -I$(2:D)/ -o $(1) $(2) }
]]
rule("AssembleNasm")
    set_extensions(".asm", ".nasm")

    on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        local objectfile = target:objectfile(sourcefile)

        -- Default to elf32 as in Jam
        local nasmflags = target:values("nasmflags")
        if not nasmflags or #nasmflags == 0 then
            nasmflags = {"-f", "elf32"}
        end

        local asflags = target:values("asflags") or {}

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
        table.insert(args, "-I" .. path.directory(sourcefile) .. "/")

        -- Output and input
        table.insert(args, "-o")
        table.insert(args, objectfile)
        table.insert(args, sourcefile)

        batchcmds:mkdir(path.directory(objectfile))
        batchcmds:show_progress(opt.progress, "assembling.nasm %s", sourcefile)
        batchcmds:vrunv(get_config("HAIKU_NASM") or "nasm", args)
        batchcmds:add_depfiles(sourcefile)
        batchcmds:set_depmtime(os.mtime(objectfile))
        batchcmds:set_depcache(target:dependfile(objectfile))
    end)

-- ============================================================================
-- LinkAgainst (function)
-- ============================================================================

--[[
    LinkAgainst(target, libs, map_libs)

    Links target against specified libraries.
    Handles library name mapping for Haiku platform.

    Jam equivalent:
        rule LinkAgainst { }
        # LinkAgainst <name> : <libs> [ : <mapLibs> ] ;
]]
function LinkAgainst(target, libs, map_libs)
    if type(libs) ~= "table" then
        libs = {libs}
    end

    if map_libs == nil then
        map_libs = true
    end

    local platform = get_platform(target)
    local arch = get_target_arch()

    -- Get library name map
    local lib_map = nil
    if platform ~= "host" and map_libs then
        lib_map = get_config("TARGET_LIBRARY_NAME_MAP_" .. arch)
    end

    local needlibs = {}
    local linklibs = {}

    for _, lib in ipairs(libs) do
        local mapped = lib

        -- Map library name if map exists
        if lib_map and lib_map[lib] then
            mapped = lib_map[lib]
        end

        -- Determine if it's a file or a -l flag
        local is_file = false
        local dirname = path.directory(mapped)
        local grist = nil  -- xmake doesn't have grist concept

        if dirname and dirname ~= "" and dirname ~= "." then
            is_file = true
        else
            local basename = path.basename(mapped)
            if basename:match("^lib") then
                is_file = true
            elseif basename == "_APP_" or basename == "_KERNEL_" then
                is_file = true
            end

            local ext = path.extension(mapped)
            if ext == ".so" or ext == ".a" then
                is_file = true
            end
        end

        if is_file then
            table.insert(needlibs, mapped)
        else
            table.insert(linklibs, mapped)
        end
    end

    -- For static libraries, copy their dependencies
    for _, lib in ipairs(needlibs) do
        if path.extension(lib) == ".a" then
            -- In xmake, static library dependencies are handled automatically
            -- through add_deps, but we note this for compatibility
        end
    end

    -- Add to target
    for _, lib in ipairs(needlibs) do
        if path.extension(lib) == ".a" then
            target:add("links", lib, {public = true})
        else
            target:add("links", lib)
        end
    end

    for _, lib in ipairs(linklibs) do
        target:add("links", lib)
    end
end

-- ============================================================================
-- AddResources (function)
-- ============================================================================

--[[
    AddResources(target, resource_files)

    Adds resource files (.rdef or .rsrc) to a target.

    Jam equivalent:
        rule AddResources { }
        # AddResources <name> : <resourcefiles> ;
]]
function AddResources(target, resource_files)
    if type(resource_files) ~= "table" then
        resource_files = {resource_files}
    end

    local resfiles = target:values("resfiles") or {}

    for _, file in ipairs(resource_files) do
        if path.extension(file) == ".rdef" then
            -- .rdef needs to be compiled to .rsrc via ResComp
            -- Add as file with ResComp rule
            target:add("files", file, {rule = "ResComp"})
            -- The compiled .rsrc will be added later
            local rsrc = file:gsub("%.rdef$", ".rsrc")
            table.insert(resfiles, rsrc)
        else
            table.insert(resfiles, file)
        end
    end

    target:values_set("resfiles", resfiles)
end

-- ============================================================================
-- SetVersionScript (function)
-- ============================================================================

--[[
    SetVersionScript(target, version_script)

    Sets the version script for symbol versioning.

    Jam equivalent:
        rule SetVersionScript { }
        # SetVersionScript <target> : <versionScript>
]]
function SetVersionScript(target, version_script)
    target:values_set("version_script", {version_script})
    target:add("ldflags", "-Wl,--version-script," .. version_script, {force = true})
    target:add("deps", version_script)
end

-- ============================================================================
-- CreateAsmStructOffsetsHeader (function)
-- ============================================================================

--[[
    CreateAsmStructOffsetsHeader(header, source, architecture)

    Creates assembly header with structure offsets.
    Compiles source with -S and extracts #define lines.

    Jam equivalent:
        rule CreateAsmStructOffsetsHeader { }
        actions CreateAsmStructOffsetsHeader1 {
            $(C++) -S "$(2)" $(C++FLAGS) $(CCDEFS) $(CCHDRS) -o - \
                | $(SED) 's/@define/#define/g' | grep "#define" \
                | $(SED) -e 's/[\$\#]\([0-9]\)/\1/' > "$(1)"
        }
]]
function CreateAsmStructOffsetsHeader(header, source, architecture)
    architecture = architecture or get_target_arch()

    local cxx = get_config("TARGET_C++_" .. architecture) or "g++"

    -- Build include paths
    local includes = {"-I" .. path.directory(source)}

    -- Add config headers, etc. would need to be passed in

    local flags = {"-Wno-invalid-offsetof"}

    os.mkdir(path.directory(header))

    -- Build command
    local cmd = format(
        '%s -S %s %s %s -o - | sed "s/@define/#define/g" | grep "#define" | sed -e "s/[\\$\\#]\\([0-9]\\)/\\1/" > "%s"',
        cxx,
        table.concat(flags, " "),
        table.concat(includes, " "),
        source,
        header
    )

    os.exec(cmd)

    -- Verify output contains defines
    local content = io.readfile(header)
    if not content or not content:find("#define") then
        os.raise("CreateAsmStructOffsetsHeader: no #define found in output")
    end
end

-- ============================================================================
-- BuildPlatformMain
-- ============================================================================

--[[
    BuildPlatformMain rule

    Builds an executable for the host platform (build tools).

    Jam equivalent:
        rule BuildPlatformMain { }
        # BuildPlatformMain <target> : <sources> : <libraries> ;
]]
rule("BuildPlatformMain")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        target:set("kind", "binary")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})
        target:values_set("dont_use_beos_rules", {true})
    end)

    on_config(function (target)
        local uses_be_api = target:values("uses_be_api")
        if uses_be_api and uses_be_api[1] then
            -- Add build libroot
            local host_libroot = get_config("HOST_LIBROOT")
            if host_libroot then
                target:add("links", host_libroot)
            end
        end
    end)

-- ============================================================================
-- BuildPlatformSharedLibrary
-- ============================================================================

--[[
    BuildPlatformSharedLibrary rule

    Builds a shared library for the host platform.

    Jam equivalent:
        rule BuildPlatformSharedLibrary { }
        # BuildPlatformSharedLibrary <target> : <sources> : <libraries> ;
]]
rule("BuildPlatformSharedLibrary")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        target:set("kind", "shared")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})
        target:values_set("dont_use_beos_rules", {true})

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
        target:add("cxflags", "-fPIC")
    end)

    on_config(function (target)
        local uses_be_api = target:values("uses_be_api")
        if uses_be_api and uses_be_api[1] then
            local host_libroot = get_config("HOST_LIBROOT")
            if host_libroot then
                target:add("links", host_libroot)
            end
        end
    end)

-- ============================================================================
-- BuildPlatformStaticLibrary
-- ============================================================================

--[[
    BuildPlatformStaticLibrary rule

    Builds a static library for the host platform.

    Jam equivalent:
        rule BuildPlatformStaticLibrary { }
        # BuildPlatformStaticLibrary <lib> : <sources> : <otherObjects> ;
]]
rule("BuildPlatformStaticLibrary")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        target:set("kind", "static")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})
    end)

    on_config(function (target)
        local uses_be_api = target:values("uses_be_api")
        if uses_be_api and uses_be_api[1] then
            -- Propagate to objects - handled by xmake automatically
        end
    end)

-- ============================================================================
-- BuildPlatformStaticLibraryPIC
-- ============================================================================

--[[
    BuildPlatformStaticLibraryPIC rule

    Like BuildPlatformStaticLibrary, but producing position independent code.

    Jam equivalent:
        rule BuildPlatformStaticLibraryPIC { }
]]
rule("BuildPlatformStaticLibraryPIC")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        target:set("kind", "static")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})

        -- Add PIC flags
        target:add("cxflags", "-fPIC")
    end)

-- ============================================================================
-- BuildPlatformMergeObject
-- ============================================================================

--[[
    BuildPlatformMergeObject rule

    Builds a merged object file for the host platform.

    Jam equivalent:
        rule BuildPlatformMergeObject { }
        # BuildPlatformMergeObject <name> : <sources> : <other objects> ;
]]
rule("BuildPlatformMergeObject")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        target:set("kind", "object")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})
    end)

    after_build(function (target)
        local ld = os.getenv("LD") or "ld"

        local objectfiles = target:objectfiles()
        local other_objects = target:values("other_objects") or {}

        local all_objects = {}
        for _, obj in ipairs(objectfiles) do
            table.insert(all_objects, obj)
        end
        for _, obj in ipairs(other_objects) do
            table.insert(all_objects, obj)
        end

        if #all_objects > 0 then
            local targetfile = target:targetfile()
            os.mkdir(path.directory(targetfile))

            local args = {"-r", "-o", targetfile}
            for _, obj in ipairs(all_objects) do
                table.insert(args, obj)
            end

            os.execv(ld, args)
        end
    end)

-- ============================================================================
-- BuildPlatformMergeObjectPIC
-- ============================================================================

--[[
    BuildPlatformMergeObjectPIC rule

    Builds a position-independent merged object for the host platform.

    Jam equivalent:
        rule BuildPlatformMergeObjectPIC { }
]]
rule("BuildPlatformMergeObjectPIC")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        target:set("kind", "object")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})

        -- Add PIC flags
        target:add("cxflags", "-fPIC")
    end)

    after_build(function (target)
        local ld = os.getenv("LD") or "ld"

        local objectfiles = target:objectfiles()
        local other_objects = target:values("other_objects") or {}

        local all_objects = {}
        for _, obj in ipairs(objectfiles) do
            table.insert(all_objects, obj)
        end
        for _, obj in ipairs(other_objects) do
            table.insert(all_objects, obj)
        end

        if #all_objects > 0 then
            local targetfile = target:targetfile()
            os.mkdir(path.directory(targetfile))

            local args = {"-r", "-o", targetfile}
            for _, obj in ipairs(all_objects) do
                table.insert(args, obj)
            end

            os.execv(ld, args)
        end
    end)

-- ============================================================================
-- BootstrapStage0PlatformObjects (function)
-- ============================================================================

--[[
    BootstrapStage0PlatformObjects(target, sources, separate_from_standard_siblings)

    Builds objects for stage0 of the bootstrap process.

    Jam equivalent:
        rule BootstrapStage0PlatformObjects { }
        # BootstrapStage0PlatformObjects <sources> : <separateFromStandardSiblings>
]]
function BootstrapStage0PlatformObjects(target, sources, separate_from_standard_siblings)
    target:set("plat", "bootstrap_stage0")
    target:values_set("supported_platforms", {"bootstrap_stage0"})

    if separate_from_standard_siblings then
        -- Set separate object directory
        local objdir = path.join(target:objectdir(), "bootstrap")
        target:set("objectdir", objdir)
    end

    for _, source in ipairs(sources) do
        target:add("files", source)
    end
end