--[[
    MainBuildRules.lua - Core build rules for Haiku OS
    
    xmake equivalent of build/jam/MainBuildRules
    
    Migrated rules (1:1 from Jam):
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
    - BuildPlatformObjects
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

local function get_platform(target)
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

local function get_target_arch()
    return get_config("arch") or "x86_64"
end

local function is_platform_supported(target)
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
    
    Jam equivalent:
        rule AddSharedObjectGlueCode
        {
            # AddSharedObjectGlueCode <target> : <isExecutable> ;
            # we link with -nostdlib and add the required libs manually
        }
    
    Sets LINK_BEGIN_GLUE, LINK_END_GLUE, adds -nostdlib and --no-undefined,
    and links against libroot.so and libgcc.
]]
function AddSharedObjectGlueCode(target, is_executable)
    local platform = get_platform(target)
    local arch = get_target_arch()
    
    if platform == "haiku" then
        -- Determine type: EXECUTABLE or LIBRARY
        local target_type = "LIBRARY"
        if is_executable == true then
            target_type = "EXECUTABLE"
        end
        
        -- Get glue code paths from config
        -- Jam: local beginGlue = $(HAIKU_$(type)_BEGIN_GLUE_CODE_$(TARGET_PACKAGING_ARCH))
        -- Jam: local endGlue = $(HAIKU_$(type)_END_GLUE_CODE_$(TARGET_PACKAGING_ARCH))
        local begin_glue_key = "HAIKU_" .. target_type .. "_BEGIN_GLUE_CODE_" .. arch
        local end_glue_key = "HAIKU_" .. target_type .. "_END_GLUE_CODE_" .. arch
        
        local begin_glue = get_config(begin_glue_key)
        local end_glue = get_config(end_glue_key)
        
        -- Store glue code for linking phase
        -- Jam: LINK_BEGIN_GLUE on $(1) = $(beginGlue) ;
        -- Jam: LINK_END_GLUE on $(1) = $(endGlue) ;
        if begin_glue then
            target:values_set("link_begin_glue", {begin_glue})
        end
        if end_glue then
            target:values_set("link_end_glue", {end_glue})
        end
        
        -- Jam: LINKFLAGS on $(1) = [ on $(1) return $(LINKFLAGS) ] -nostdlib -Xlinker --no-undefined ;
        target:add("ldflags", "-nostdlib", {force = true})
        target:add("ldflags", "-Xlinker", "--no-undefined", {force = true})
        
        -- Standard libs: libroot.so and libgcc
        -- Jam: local stdLibs = [ MultiArchDefaultGristFiles libroot.so ] [ TargetLibgcc ] ;
        -- Special case: don't link libroot against itself
        local dont_link_libroot = target:values("dont_link_against_libroot")
        if target_type == "LIBRARY" and dont_link_libroot and dont_link_libroot[1] then
            -- Skip stdLibs for libroot itself
        else
            target:add("syslinks", "root")
            target:add("syslinks", "gcc")
        end
    end
    
    -- Jam: if $(platform) != host && $(TARGET_HAIKU_COMPATIBILITY_LIBS) {
    --          LinkAgainst $(1) : $(TARGET_HAIKU_COMPATIBILITY_LIBS) ;
    --      }
    if platform ~= "host" then
        local compat_libs = get_config("TARGET_HAIKU_COMPATIBILITY_LIBS")
        if compat_libs then
            if type(compat_libs) == "table" then
                for _, lib in ipairs(compat_libs) do
                    target:add("syslinks", lib)
                end
            else
                target:add("syslinks", compat_libs)
            end
        end
    end
end

-- ============================================================================
-- Application
-- ============================================================================

--[[
    Jam equivalent:
        rule Application
        {
            # Application <name> : <sources> : <libraries> : <res> ;
            if ! [ IsPlatformSupportedForTarget $(1) ] { return ; }
            AddResources $(1) : $(4) ;
            Main $(1) : $(2) ;
            LinkAgainst $(1) : $(3) ;
            LINKFLAGS on $(1) = [ on $(1) return $(LINKFLAGS) ] -Xlinker -soname=_APP_ ;
            AddSharedObjectGlueCode $(1) : true ;
        }
]]
rule("Application")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")
    
    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end
        
        target:set("kind", "binary")
        
        -- Jam: LINKFLAGS on $(1) = [ on $(1) return $(LINKFLAGS) ] -Xlinker -soname=_APP_ ;
        target:add("ldflags", "-Xlinker", "-soname=_APP_", {force = true})
    end)
    
    on_config(function (target)
        -- Jam: AddSharedObjectGlueCode $(1) : true ;
        AddSharedObjectGlueCode(target, true)
    end)

-- ============================================================================
-- StdBinCommands
-- ============================================================================

--[[
    Jam equivalent:
        rule StdBinCommands
        {
            # StdBinCommands <sources> : <libs> : <res> ;
            local source ;
            for source in $(1) {
                local target = $(source:S=) ;
                Application $(target) : $(source) : $(libs) : $(ress) ;
            }
        }
    
    Note: In xmake, this is typically handled by creating multiple targets.
    This rule is for single-source executables.
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
-- Addon / Translator shared helpers
-- ============================================================================

-- Shared on_load for Addon-like targets (Addon, Translator).
-- Jam: Translator simply calls Addon, so we extract common logic.
local function _addon_on_load(target)
    if not is_platform_supported(target) then
        target:set("enabled", false)
        return
    end

    local is_executable = target:values("is_executable")
    is_executable = is_executable and is_executable[1]
    local is_exec = (is_executable == true or is_executable == "true")

    if is_exec then
        target:set("kind", "binary")
    else
        target:set("kind", "shared")
        -- Jam: linkFlags = -shared $(linkFlags) ;
        target:add("ldflags", "-shared", {force = true})
    end

    -- Jam: local linkFlags = -Xlinker -soname=\"$(target:G=)\" ;
    local soname = target:basename()
    target:add("ldflags", "-Xlinker", string.format('-soname="%s"', soname), {force = true})
end

-- Shared on_config for Addon-like targets.
local function _addon_on_config(target)
    local is_executable = target:values("is_executable")
    is_executable = is_executable and is_executable[1]
    local is_exec = (is_executable == true or is_executable == "true")
    AddSharedObjectGlueCode(target, is_exec)
end

-- ============================================================================
-- Addon
-- ============================================================================

--[[
    Jam equivalent:
        rule Addon target : sources : libraries : isExecutable
        {
            # Addon <target> : <sources> : <is executable> : <libraries> ;
            if ! [ IsPlatformSupportedForTarget $(target) ] { return ; }
            Main $(target) : $(sources) ;
            local linkFlags = -Xlinker -soname=\"$(target:G=)\" ;
            if $(isExecutable) != true {
                linkFlags = -shared $(linkFlags) ;
            }
            LINKFLAGS on $(target) = [ on $(target) return $(LINKFLAGS) ] $(linkFlags) ;
            LinkAgainst $(target) : $(libraries) ;
            AddSharedObjectGlueCode $(target) : $(isExecutable) ;
        }
]]
rule("Addon")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        _addon_on_load(target)
    end)

    on_config(function (target)
        _addon_on_config(target)
    end)

-- ============================================================================
-- Translator
-- ============================================================================

--[[
    Jam equivalent:
        rule Translator target : sources : libraries : isExecutable
        {
            # Translator <target> : <sources> : <libraries> : <isExecutable> ;
            Addon $(target) : $(sources) : $(libraries) : $(isExecutable) ;
        }

    Translator is just a wrapper around Addon — delegates via shared helpers.
]]
rule("Translator")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        _addon_on_load(target)
    end)

    on_config(function (target)
        _addon_on_config(target)
    end)

-- ============================================================================
-- ScreenSaver
-- ============================================================================

--[[
    Jam equivalent:
        rule ScreenSaver target : sources : libraries
        {
            # ScreenSaver <target> : <sources> : <libraries> ;
            Addon $(target) : $(sources) : $(libraries) : false ;
        }
    
    ScreenSaver is an Addon with isExecutable = false.
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
        target:add("ldflags", "-Xlinker", string.format('-soname="%s"', soname), {force = true})
    end)
    
    on_config(function (target)
        -- isExecutable = false
        AddSharedObjectGlueCode(target, false)
    end)

-- ============================================================================
-- StaticLibrary
-- ============================================================================

--[[
    Jam equivalent:
        rule StaticLibrary
        {
            # StaticLibrary <lib> : <sources> : <otherObjects> ;
            local lib = $(1) ;
            local sources = [ FGristFiles $(2) ] ;
            local otherObjects = $(3) ;
            local objects = $(sources:S=$(SUFOBJ)) ;
            
            if ! [ IsPlatformSupportedForTarget $(1) ] { return ; }
            
            InheritPlatform $(objects) : $(lib) ;
            
            if [ on $(lib) return $(NO_HIDDEN_VISIBILITY) ] != 1 {
                CCFLAGS on $(objects) += -fvisibility=hidden ;
                C++FLAGS on $(objects) += -fvisibility=hidden ;
            }
            
            StaticLibraryFromObjects $(lib) : $(objects) $(otherObjects) ;
            Objects $(2) ;
        }
]]
rule("StaticLibrary")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")
    
    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end
        
        target:set("kind", "static")
        
        -- Jam: if [ on $(lib) return $(NO_HIDDEN_VISIBILITY) ] != 1 {
        --          CCFLAGS on $(objects) += -fvisibility=hidden ;
        --          C++FLAGS on $(objects) += -fvisibility=hidden ;
        --      }
        local no_hidden = target:values("no_hidden_visibility")
        if not no_hidden or not no_hidden[1] then
            target:add("cflags", "-fvisibility=hidden")
            target:add("cxxflags", "-fvisibility=hidden")
        end
    end)

-- ============================================================================
-- StaticLibraryFromObjects
-- ============================================================================

--[[
    Jam equivalent:
        rule StaticLibraryFromObjects
        {
            if ! [ IsPlatformSupportedForTarget $(1) ] { return ; }
            LibraryFromObjects $(1) : $(2) ;
        }
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
    Jam equivalent:
        rule SharedLibrary
        {
            # SharedLibrary <lib> : <sources> : <libraries> : <abiVersion> ;
            local lib = $(1) ;
            local sources = [ FGristFiles $(2) ] ;
            local objects = $(sources:S=$(SUFOBJ)) ;
            local libs = $(3) ;
            local abiVersion = $(4) ;
            
            if ! [ IsPlatformSupportedForTarget $(1) ] { return ; }
            
            if $(abiVersion) {
                HAIKU_SONAME on $(lib) = $(lib:BS).$(abiVersion) ;
                HAIKU_LIB_ABI_VERSION on $(lib) = $(abiVersion) ;
            }
            
            InheritPlatform $(objects) : $(lib) ;
            Objects $(sources) ;
            SharedLibraryFromObjects $(lib) : $(objects) : $(libs) ;
        }
]]
rule("SharedLibrary")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")
    
    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end
        
        target:set("kind", "shared")
        
        -- Handle ABI version
        local abi_version = target:values("abi_version")
        abi_version = abi_version and abi_version[1]
        
        local basename = target:basename()
        local soname = basename
        
        -- Jam: if $(abiVersion) {
        --          HAIKU_SONAME on $(lib) = $(lib:BS).$(abiVersion) ;
        --          HAIKU_LIB_ABI_VERSION on $(lib) = $(abiVersion) ;
        --      }
        if abi_version then
            soname = string.format("%s.%s", basename, abi_version)
            target:values_set("haiku_soname", {soname})
            target:values_set("haiku_lib_abi_version", {abi_version})
        end
        
        -- Jam (in SharedLibraryFromObjects):
        -- LINKFLAGS on $(_lib) = [ on $(_lib) return $(LINKFLAGS) ]
        --     -shared -Xlinker -soname=\"$(soname)\" ;
        target:add("ldflags", "-shared", {force = true})
        target:add("ldflags", "-Xlinker", string.format('-soname="%s"', soname), {force = true})
    end)
    
    on_config(function (target)
        -- Jam: AddSharedObjectGlueCode $(_lib) : false ;
        AddSharedObjectGlueCode(target, false)
    end)

-- ============================================================================
-- SharedLibraryFromObjects
-- ============================================================================

--[[
    Jam equivalent:
        rule SharedLibraryFromObjects
        {
            # SharedLibraryFromObjects <lib> : <objects> : <libraries> ;
            local _lib = $(1) ;
            
            if ! [ IsPlatformSupportedForTarget $(1) ] { return ; }
            
            local soname = [ on $(_lib) return $(HAIKU_SONAME) ] ;
            soname ?= $(_lib:BS) ;
            
            MainFromObjects $(_lib) : $(2) ;
            LINKFLAGS on $(_lib) = [ on $(_lib) return $(LINKFLAGS) ]
                -shared -Xlinker -soname=\"$(soname)\" ;
            LinkAgainst $(_lib) : $(3) ;
            
            AddSharedObjectGlueCode $(_lib) : false ;
        }
]]
rule("SharedLibraryFromObjects")
    on_load(function (target)
        if not is_platform_supported(target) then
            target:set("enabled", false)
            return
        end
        
        target:set("kind", "shared")
        
        -- Jam: local soname = [ on $(_lib) return $(HAIKU_SONAME) ] ;
        --      soname ?= $(_lib:BS) ;
        local soname = target:values("haiku_soname")
        soname = soname and soname[1] or target:basename()
        
        target:add("ldflags", "-shared", {force = true})
        target:add("ldflags", "-Xlinker", string.format('-soname="%s"', soname), {force = true})
    end)
    
    on_config(function (target)
        AddSharedObjectGlueCode(target, false)
    end)

-- ============================================================================
-- MergeObject
-- ============================================================================

--[[
    Jam equivalent:
        rule MergeObject
        {
            # MergeObject <name> : <sources> : <other objects> ;
            local target = $(1) ;
            local sources = [ FGristFiles $(2) ] ;
            local otherObjects = $(3) ;
            local objects = $(sources:S=$(SUFOBJ)) ;
            
            if ! [ IsPlatformSupportedForTarget $(1) ] { return ; }
            
            InheritPlatform $(objects) : $(target) ;
            Objects $(sources) ;
            MergeObjectFromObjects $(target) : $(objects) : $(otherObjects) ;
        }
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
    
    on_link(function (target)
        local platform = get_platform(target)
        local ld
        local ldflags = {}

        -- Jam: if $(PLATFORM) = host {
        --          LINK on $(1) = $(HOST_LD) ;
        --          LINKFLAGS on $(target) = $(HOST_LDFLAGS) ;
        --      } else {
        --          LINK on $(1) = $(TARGET_LD_$(TARGET_PACKAGING_ARCH)) ;
        --          LINKFLAGS on $(target) = $(TARGET_LDFLAGS_$(TARGET_PACKAGING_ARCH)) ;
        --      }
        if platform == "host" then
            ld = get_config("HOST_LD") or os.getenv("LD") or "ld"
            local host_ldflags = get_config("HOST_LDFLAGS")
            if host_ldflags then
                if type(host_ldflags) == "table" then
                    ldflags = host_ldflags
                else
                    table.insert(ldflags, host_ldflags)
                end
            end
        else
            local arch = get_target_arch()
            ld = get_config("TARGET_LD_" .. arch) or "ld"
            local target_ldflags = get_config("TARGET_LDFLAGS_" .. arch)
            if target_ldflags then
                if type(target_ldflags) == "table" then
                    ldflags = target_ldflags
                else
                    table.insert(ldflags, target_ldflags)
                end
            end
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

            -- Jam action: $(LINK) $(LINKFLAGS) -r $(2) -o $(1)
            local args = {}
            for _, flag in ipairs(ldflags) do
                table.insert(args, flag)
            end
            table.insert(args, "-r")
            table.insert(args, "-o")
            table.insert(args, targetfile)
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
    Jam equivalent:
        rule MergeObjectFromObjects
        {
            # MergeObjectFromObjects <name> : <objects> : <other objects> ;
            local objects = [ FGristFiles $(2) ] ;
            
            on $(1) {
                if ! $(PLATFORM) in $(SUPPORTED_PLATFORMS) { return ; }
                
                if $(PLATFORM) = host {
                    LINK on $(1) = $(HOST_LD) ;
                    LINKFLAGS on $(target) = $(HOST_LDFLAGS) ;
                } else {
                    LINK on $(1) = $(TARGET_LD_$(TARGET_PACKAGING_ARCH)) ;
                    LINKFLAGS on $(target) = $(TARGET_LDFLAGS_$(TARGET_PACKAGING_ARCH)) ;
                }
            }
            
            MakeLocateDebug $(1) ;
            Depends $(1) : $(objects) ;
            Depends $(1) : $(3) ;
            LocalDepends obj : $(1) ;
            MergeObjectFromObjects1 $(1) : $(objects) $(3) ;
        }
        
        actions MergeObjectFromObjects1
        {
            $(LINK) $(LINKFLAGS) -r $(2) -o $(1)
        }
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
        local ldflags = {}
        
        if platform == "host" then
            ld = get_config("HOST_LD") or os.getenv("LD") or "ld"
            local host_ldflags = get_config("HOST_LDFLAGS")
            if host_ldflags then
                if type(host_ldflags) == "table" then
                    ldflags = host_ldflags
                else
                    table.insert(ldflags, host_ldflags)
                end
            end
        else
            local arch = get_target_arch()
            ld = get_config("TARGET_LD_" .. arch) or "ld"
            local target_ldflags = get_config("TARGET_LDFLAGS_" .. arch)
            if target_ldflags then
                if type(target_ldflags) == "table" then
                    ldflags = target_ldflags
                else
                    table.insert(ldflags, target_ldflags)
                end
            end
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
            
            local args = {}
            for _, flag in ipairs(ldflags) do
                table.insert(args, flag)
            end
            table.insert(args, "-r")
            table.insert(args, "-o")
            table.insert(args, targetfile)
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
    Jam equivalent:
        rule Ld
        {
            # Ld <name> : <objs> : <linkerscript> : <flags> ;
            local target = $(1) ;
            local objects = $(2) ;
            local linkerScript = $(3) ;
            local linkerFlags = $(4) ;
            
            if $(linkerScript) {
                linkerFlags += --script=$(linkerScript) ;
            }
            
            on $(target) {
                if $(PLATFORM) = host {
                    LINK on $(target) = $(HOST_LD) ;
                    LINKFLAGS on $(target) = $(HOST_LDFLAGS) $(LINKFLAGS) $(linkerFlags) ;
                } else {
                    LINK on $(target) = $(TARGET_LD_$(TARGET_PACKAGING_ARCH)) ;
                    LINKFLAGS on $(target) = $(TARGET_LDFLAGS_$(TARGET_PACKAGING_ARCH))
                        $(LINKFLAGS) $(linkerFlags) ;
                }
                
                NEEDLIBS on $(target) = $(NEEDLIBS) ;
                LINKLIBS on $(target) = $(LINKLIBS) ;
            }
            
            LocalClean clean : $(target) ;
            LocalDepends all : $(target) ;
            Depends $(target) : $(objects) ;
            
            MakeLocateDebug $(target) ;
            
            on $(1) XRes $(1) : $(RESFILES) ;
            if ! [ on $(1) return $(DONT_USE_BEOS_RULES) ] {
                SetType $(1) ;
                MimeSet $(1) ;
                SetVersion $(1) ;
            }
        }
        
        actions Ld
        {
            $(LINK) $(LINKFLAGS) -o "$(1)" "$(2)" "$(NEEDLIBS)" $(LINKLIBS)
        }
]]
rule("Ld")
    on_load(function (target)
        target:set("kind", "binary")
    end)
    
    on_config(function (target)
        local platform = get_platform(target)
        local arch = get_target_arch()
        
        -- Jam: if $(PLATFORM) = host { LINK on $(target) = $(HOST_LD) ; }
        --      else { LINK on $(target) = $(TARGET_LD_$(TARGET_PACKAGING_ARCH)) ; }
        if platform == "host" then
            local ld = get_config("HOST_LD") or os.getenv("LD") or "ld"
            target:set("toolset", "ld", ld)
            
            local ldflags = get_config("HOST_LDFLAGS")
            if ldflags then
                if type(ldflags) == "table" then
                    for _, flag in ipairs(ldflags) do
                        target:add("ldflags", flag, {force = true})
                    end
                else
                    target:add("ldflags", ldflags, {force = true})
                end
            end
        else
            local ld = get_config("TARGET_LD_" .. arch) or "ld"
            target:set("toolset", "ld", ld)
            
            local ldflags = get_config("TARGET_LDFLAGS_" .. arch)
            if ldflags then
                if type(ldflags) == "table" then
                    for _, flag in ipairs(ldflags) do
                        target:add("ldflags", flag, {force = true})
                    end
                else
                    target:add("ldflags", ldflags, {force = true})
                end
            end
        end
        
        -- Handle linker script
        -- Jam: if $(linkerScript) { linkerFlags += --script=$(linkerScript) ; }
        local linker_script = target:values("linker_script")
        if linker_script and linker_script[1] then
            target:add("ldflags", "--script=" .. linker_script[1], {force = true})
        end
    end)
    
    after_link(function (target)
        -- Jam: on $(1) XRes $(1) : $(RESFILES) ;
        -- XRes is called UNCONDITIONALLY (outside DONT_USE_BEOS_RULES check)
        local ok, beos = pcall(import, "BeOSRules")
        if ok and beos then
            -- Also collect compiled .rsrc from ResComp rule
            local resfiles = target:values("resfiles") or {}
            local compiled_rsrc = target:data("_compiled_rsrc") or {}
            local all_rsrc = {}
            local seen = {}
            for _, f in ipairs(compiled_rsrc) do
                if not seen[f] then
                    table.insert(all_rsrc, f)
                    seen[f] = true
                end
            end
            for _, f in ipairs(resfiles) do
                if not seen[f] then
                    table.insert(all_rsrc, f)
                    seen[f] = true
                end
            end
            if #all_rsrc > 0 then
                beos.XRes(target, all_rsrc)
            end

            -- Jam: if ! [ on $(1) return $(DONT_USE_BEOS_RULES) ] {
            --          SetType $(1) ; MimeSet $(1) ; SetVersion $(1) ;
            --      }
            local dont_use = target:values("dont_use_beos_rules")
            if not dont_use or not dont_use[1] then
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
    Jam equivalent:
        rule AssembleNasm
        {
            if ! [ on $(1) return $(NASMFLAGS) ] {
                NASMFLAGS on $(1) = -f elf32 ;
            }
            
            Depends $(1) : $(2) [ on $(2) return $(PLATFORM) ] ;
        }
        
        actions AssembleNasm
        {
            if test $(ASFLAGS) ; then
                $(HAIKU_NASM) -d $(ASFLAGS) $(NASMFLAGS) -I$(2:D)/ -o $(1) $(2) ;
            else
                $(HAIKU_NASM) $(NASMFLAGS) -I$(2:D)/ -o $(1) $(2) ;
            fi
        }
]]
rule("AssembleNasm")
    set_extensions(".asm", ".nasm")
    
    on_buildcmd_file(function (target, batchcmds, sourcefile, opt)
        local objectfile = target:objectfile(sourcefile)
        
        -- Jam: if ! [ on $(1) return $(NASMFLAGS) ] { NASMFLAGS on $(1) = -f elf32 ; }
        -- DEFAULT IS ELF32, NOT ELF64!
        local nasmflags = target:values("nasmflags")
        if not nasmflags or #nasmflags == 0 then
            nasmflags = {"-f", "elf32"}
        end
        
        local asflags = target:values("asflags") or {}
        
        -- Build arguments
        local args = {}
        
        -- Jam: if test $(ASFLAGS) ; then $(HAIKU_NASM) -d $(ASFLAGS) ...
        for _, flag in ipairs(asflags) do
            table.insert(args, "-d")
            table.insert(args, flag)
        end
        
        -- Add nasmflags
        for _, flag in ipairs(nasmflags) do
            table.insert(args, flag)
        end
        
        -- Jam: -I$(2:D)/
        table.insert(args, "-I" .. path.directory(sourcefile) .. "/")
        
        -- Output and input
        table.insert(args, "-o")
        table.insert(args, objectfile)
        table.insert(args, sourcefile)
        
        batchcmds:mkdir(path.directory(objectfile))
        batchcmds:show_progress(opt.progress, "assembling.nasm %s", sourcefile)
        
        local nasm = get_config("HAIKU_NASM") or "nasm"
        batchcmds:vrunv(nasm, args)
        
        batchcmds:add_depfiles(sourcefile)
        batchcmds:set_depmtime(os.mtime(objectfile))
        batchcmds:set_depcache(target:dependfile(objectfile))
    end)

-- ============================================================================
-- LinkAgainst (function)
-- ============================================================================

--[[
    Jam equivalent:
        rule LinkAgainst
        {
            # LinkAgainst <name> : <libs> [ : <mapLibs> ] ;
            local target = $(1) ;
            local libs = $(2) ;
            local mapLibs = $(3:E=true) ;
            
            local linkLibs ;
            local needLibs ;
            
            on $(target) {
                local i ;
                
                # map libraries, if desired and target platform is Haiku
                local map = $(TARGET_LIBRARY_NAME_MAP_$(TARGET_PACKAGING_ARCH)) ;
                if $(PLATFORM) != host && $(mapLibs) = true && $(map) {
                    local mappedLibs ;
                    for i in $(libs) {
                        local mapped = $($(map)_$(i)) ;
                        mapped ?= $(i) ;
                        mappedLibs += $(mapped) ;
                    }
                    libs = $(mappedLibs) ;
                }
                
                for i in $(libs) {
                    local isfile = ;
                    if $(i:D) || $(i:G) {
                        isfile = true ;
                        if $(i:G) = <nogrist> {
                            i = $(i:G=) ;
                        }
                    } else {
                        switch $(i:B)
                        {
                            case _APP_ : isfile = true ;
                            case _KERNEL_ : isfile = true ;
                            case lib*   : isfile = true ;
                            case *  : isfile = ;
                        }
                        if ! $(isfile) && ( $(i:S) = .so || $(i:S) = .a ) {
                            isfile = true ;
                        }
                    }
                    
                    if $(isfile) {
                        needLibs += $(i) ;
                    } else {
                        linkLibs += $(i) ;
                    }
                }
            }
            
            # Copy in the library dependencies from any static libraries first.
            for i in $(needLibs) {
                if $(i:S) = .a  {
                    needLibs += [ on $(i) return $(NEEDLIBS) ] ;
                    linkLibs += [ on $(i) return $(LINKLIBS) ] ;
                }
            }
            
            on $(target) {
                NEEDLIBS on $(1) = $(NEEDLIBS) $(needLibs) ;
                LINKLIBS on $(1) = $(LINKLIBS) -l$(linkLibs) ;
                
                if $(needLibs) && ! $(NO_LIBRARY_DEPENDENCIES) {
                    Depends $(1) : $(needLibs) ;
                }
            }
        }
]]
function LinkAgainst(target, libs, map_libs)
    if type(libs) ~= "table" then
        libs = {libs}
    end
    
    -- Jam: local mapLibs = $(3:E=true) ;
    if map_libs == nil then
        map_libs = true
    end
    
    local platform = get_platform(target)
    local arch = get_target_arch()
    
    -- Get library name map
    -- Jam: local map = $(TARGET_LIBRARY_NAME_MAP_$(TARGET_PACKAGING_ARCH)) ;
    local lib_map = nil
    if platform ~= "host" and map_libs then
        lib_map = get_config("TARGET_LIBRARY_NAME_MAP_" .. arch)
    end
    
    local needlibs = {}
    local linklibs = {}
    
    for _, lib in ipairs(libs) do
        local mapped = lib
        
        -- Map library name if map exists
        -- Jam: local mapped = $($(map)_$(i)) ; mapped ?= $(i) ;
        if lib_map and type(lib_map) == "table" and lib_map[lib] then
            mapped = lib_map[lib]
        end
        
        -- Determine if it's a file or a -l flag
        -- Jam logic for determining isfile
        local is_file = false
        local dirname = path.directory(mapped)
        
        -- Jam: if $(i:D) || $(i:G) { isfile = true ; }
        if dirname and dirname ~= "" and dirname ~= "." then
            is_file = true
        else
            local basename = path.basename(mapped)
            -- Jam: switch $(i:B) { case _APP_ : ... case _KERNEL_ : ... case lib* : ... }
            if basename == "_APP_" or basename == "_KERNEL_" then
                is_file = true
            elseif basename:match("^lib") then
                is_file = true
            end
            
            -- Jam: if ! $(isfile) && ( $(i:S) = .so || $(i:S) = .a ) { isfile = true ; }
            if not is_file then
                local ext = path.extension(mapped)
                if ext == ".so" or ext == ".a" then
                    is_file = true
                end
            end
        end
        
        if is_file then
            table.insert(needlibs, mapped)
        else
            table.insert(linklibs, mapped)
        end
    end
    
    -- Jam: Copy in the library dependencies from any static libraries first.
    -- for i in $(needLibs) {
    --     if $(i:S) = .a  {
    --         needLibs += [ on $(i) return $(NEEDLIBS) ] ;
    --         linkLibs += [ on $(i) return $(LINKLIBS) ] ;
    --     }
    -- }
    --
    -- In xmake, if libraries are declared as targets with add_deps({public = true}),
    -- transitive inheritance happens automatically. For file-based .a references
    -- we replicate Jam's recursive NEEDLIBS/LINKLIBS collection by querying
    -- xmake project targets whose output matches the .a path.
    local project_mod = nil
    local function _try_import_project()
        if project_mod == nil then
            local ok, mod = pcall(import, "core.project.project")
            project_mod = ok and mod or false
        end
        return project_mod
    end

    local i = 1
    while i <= #needlibs do
        local lib = needlibs[i]
        if path.extension(lib) == ".a" then
            local proj = _try_import_project()
            if proj then
                -- Try to find a target whose output matches this .a file
                for _, t in pairs(proj.targets()) do
                    if t:kind() == "static" and t:targetfile() == lib then
                        -- Collect its needlibs (links) and linklibs (syslinks)
                        local sub_links = t:get("links")
                        if sub_links then
                            if type(sub_links) == "string" then sub_links = {sub_links} end
                            for _, sl in ipairs(sub_links) do
                                table.insert(needlibs, sl)
                            end
                        end
                        local sub_syslinks = t:get("syslinks")
                        if sub_syslinks then
                            if type(sub_syslinks) == "string" then sub_syslinks = {sub_syslinks} end
                            for _, sl in ipairs(sub_syslinks) do
                                table.insert(linklibs, sl)
                            end
                        end
                        break
                    end
                end
            end
        end
        i = i + 1
    end

    -- Add to target
    -- Jam: NEEDLIBS on $(1) = $(NEEDLIBS) $(needLibs) ;
    for _, lib in ipairs(needlibs) do
        target:add("links", lib)
    end

    -- Jam: LINKLIBS on $(1) = $(LINKLIBS) -l$(linkLibs) ;
    for _, lib in ipairs(linklibs) do
        target:add("syslinks", lib)
    end
end

-- ============================================================================
-- AddResources (function)
-- ============================================================================

--[[
    Jam equivalent:
        rule AddResources
        {
            # AddResources <name> : <resourcefiles> ;
            
            # add grist to the resource files which don't have any yet
            local resfiles ;
            local file ;
            for file in $(2) {
                if ! $(file:G) {
                    file = [ FGristFiles $(file) ] ;
                }
                resfiles += $(file) ;
            }
            
            SEARCH on $(resfiles) += $(SEARCH_SOURCE) ;
            
            for file in $(resfiles) {
                if $(file:S) = .rdef {
                    local rdef = $(file) ;
                    file = $(rdef:S=.rsrc) ;
                    ResComp $(file) : $(rdef) ;
                }
                InheritPlatform $(file) : $(1) ;
                RESFILES on $(1) += $(file) ;
            }
        }
]]
function AddResources(target, resource_files)
    if type(resource_files) ~= "table" then
        resource_files = {resource_files}
    end

    local resfiles = target:values("resfiles") or {}

    for _, file in ipairs(resource_files) do
        -- Jam: if $(file:S) = .rdef { ... ResComp $(file) : $(rdef) ; }
        if path.extension(file) == ".rdef" then
            -- .rdef needs to be compiled to .rsrc via ResComp rule
            -- ResComp outputs to objectdir/filename.rsrc and stores
            -- the path in target:data("_compiled_rsrc")
            target:add("files", file, {rule = "ResComp"})
            -- Don't add wrong source-relative path to resfiles;
            -- the compiled .rsrc will be collected from target:data("_compiled_rsrc")
            -- in the after_link phase (Ld rule / post-link BeOS rules)
        else
            table.insert(resfiles, file)
        end
    end

    -- Jam: RESFILES on $(1) += $(file) ;
    target:values_set("resfiles", resfiles)
end

-- ============================================================================
-- SetVersionScript (function)
-- ============================================================================

--[[
    Jam equivalent:
        rule SetVersionScript target : versionScript
        {
            # SetVersionScript <target> : <versionScript>
            versionScript = [ FGristFiles $(versionScript) ] ;
            SEARCH on $(versionScript) += $(SEARCH_SOURCE) ;
            VERSION_SCRIPT on $(target) = $(versionScript) ;
            Depends $(target) : $(versionScript) ;
        }
    
    Note: VERSION_SCRIPT is used in Link actions as:
        -Wl,--version-script,$(VERSION_SCRIPT)
]]
function SetVersionScript(target, version_script)
    target:values_set("version_script", {version_script})
    -- Jam Link action: -Wl,--version-script,$(VERSION_SCRIPT)
    target:add("ldflags", "-Wl,--version-script," .. version_script, {force = true})
    -- Jam: Depends $(target) : $(versionScript) ;
    -- File dependency is implicit through ldflags referencing the file.
    -- xmake's add_deps() is for target dependencies, not file dependencies.
end

-- ============================================================================
-- CreateAsmStructOffsetsHeader (function)
-- ============================================================================

--[[
    Jam equivalent:
        rule CreateAsmStructOffsetsHeader header : source : architecture
        {
            # CreateAsmStructOffsetsHeader header : source : architecture
            header = [ FGristFiles $(header) ] ;
            source = [ FGristFiles $(source) ] ;
            
            TARGET_PACKAGING_ARCH on $(header) = $(architecture) ;
            
            # ... complex header/define collection ...
            
            C++FLAGS on $(header) = $(flags) ;
            CCHDRS on $(header) = [ FIncludes $(headers) : $(localIncludesOption) ]
                $(includesSeparator)
                [ FSysIncludes $(sysHeaders) : $(systemIncludesOption) ] ;
            CCDEFS on $(header) = [ FDefines $(defines) ] ;
            
            CreateAsmStructOffsetsHeader1 $(header) : $(source) ;
        }
        
        actions CreateAsmStructOffsetsHeader1
        {
            $(C++) -S "$(2)" $(C++FLAGS) $(CCDEFS) $(CCHDRS) -o - \
                | $(SED) 's/@define/#define/g' | grep "#define" \
                | $(SED) -e 's/[\$\#]\([0-9]\)/\1/' > "$(1)"
            grep -q "#define" "$(1)"
        }
]]
function CreateAsmStructOffsetsHeader(header, source, architecture, options)
    architecture = architecture or get_target_arch()
    options = options or {}
    
    local platform = options.platform or "haiku"
    local cxx
    local flags = {}
    local includes = {}
    local defines = {}
    
    -- Get compiler
    if platform == "host" then
        cxx = get_config("HOST_C++") or os.getenv("CXX") or "g++"
    else
        cxx = get_config("TARGET_C++_" .. architecture) or "g++"
    end
    
    -- Jam: flags += -Wno-invalid-offsetof ;
    table.insert(flags, "-Wno-invalid-offsetof")
    
    -- Add includes from options
    if options.hdrs then
        for _, hdr in ipairs(options.hdrs) do
            table.insert(includes, "-I" .. hdr)
        end
    end
    
    -- Add source directory
    table.insert(includes, "-I" .. path.directory(source))
    
    -- Add system includes
    if options.syshdrs then
        for _, hdr in ipairs(options.syshdrs) do
            table.insert(includes, "-isystem")
            table.insert(includes, hdr)
        end
    end
    
    -- Add defines
    if options.defines then
        for _, def in ipairs(options.defines) do
            table.insert(defines, "-D" .. def)
        end
    end
    
    -- Add C++ flags from options
    if options.cxxflags then
        for _, flag in ipairs(options.cxxflags) do
            table.insert(flags, flag)
        end
    end
    
    os.mkdir(path.directory(header))
    
    -- Jam action:
    -- $(C++) -S "$(2)" $(C++FLAGS) $(CCDEFS) $(CCHDRS) -o - \
    --     | $(SED) 's/@define/#define/g' | grep "#define" \
    --     | $(SED) -e 's/[\$\#]\([0-9]\)/\1/' > "$(1)"
    local cmd = string.format(
        '%s -S "%s" %s %s %s -o - | sed "s/@define/#define/g" | grep "#define" | sed -e "s/[\\$\\#]\\([0-9]\\)/\\1/" > "%s"',
        cxx,
        source,
        table.concat(flags, " "),
        table.concat(defines, " "),
        table.concat(includes, " "),
        header
    )
    
    os.exec(cmd)
    
    -- Jam: grep -q "#define" "$(1)"
    local content = io.readfile(header)
    if not content or not content:find("#define") then
        os.raise("CreateAsmStructOffsetsHeader: no #define found in output for " .. header)
    end
end

-- ============================================================================
-- BuildPlatformObjects
-- ============================================================================

--[[
    Jam equivalent:
        rule BuildPlatformObjects
        {
            # Usage BuildPlatformObjects <sources> ;
            local sources = [ FGristFiles $(1) ] ;
            local objects = [ FGristFiles $(sources:S=$(SUFOBJ)) ] ;

            PLATFORM on $(objects) = host ;
            SUPPORTED_PLATFORMS on $(objects) = host ;

            Objects $(sources) ;
        }

    Compiles sources as host-platform objects.
    In xmake, this is a rule that sets platform to host and compiles sources.
]]
rule("BuildPlatformObjects")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")

    on_load(function (target)
        target:set("kind", "object")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})
    end)

-- ============================================================================
-- BuildPlatformMain
-- ============================================================================

--[[
    Jam equivalent:
        rule BuildPlatformMain
        {
            # BuildPlatformMain <target> : <sources> : <libraries> ;
            local target = $(1) ;
            local sources = $(2) ;
            local libs = $(3) ;
            local objects = [ FGristFiles $(sources:S=$(SUFOBJ)) ] ;
            
            PLATFORM on $(target) = host ;
            SUPPORTED_PLATFORMS on $(target) = host ;
            DONT_USE_BEOS_RULES on $(target) = true ;
            
            local usesBeAPI = [ on $(target) return $(USES_BE_API) ] ;
            if $(usesBeAPI) {
                USES_BE_API on $(objects) = $(usesBeAPI) ;
                local libroot = [ on $(target) return $(HOST_LIBROOT) ] ;
                Depends $(target) : $(libroot) ;
                NEEDLIBS on $(target) += $(libroot) ;
            }
            
            Main $(target) : $(sources) ;
            LinkAgainst $(target) : $(libs) ;
        }
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
            local host_libroot = get_config("HOST_LIBROOT")
            if host_libroot then
                if type(host_libroot) == "table" then
                    for _, lib in ipairs(host_libroot) do
                        target:add("links", lib)
                    end
                else
                    target:add("links", host_libroot)
                end
            end
        end
    end)

-- ============================================================================
-- BuildPlatformSharedLibrary
-- ============================================================================

--[[
    Jam equivalent:
        rule BuildPlatformSharedLibrary
        {
            # BuildPlatformSharedLibrary <target> : <sources> : <libraries> ;
            local target = $(1) ;
            local sources = $(2) ;
            local libs = $(3) ;
            
            BuildPlatformMain $(target) : $(sources) : $(libs) ;
            
            if $(HOST_PLATFORM) = darwin {
                LINKFLAGS on $(target) = [ on $(target) return $(LINKFLAGS) ]
                    -dynamic -dynamiclib -Xlinker -flat_namespace ;
            } else {
                LINKFLAGS on $(target) = [ on $(target) return $(LINKFLAGS) ]
                    -shared -Xlinker -soname=\"$(target:G=)\" ;
            }
            
            local objects = [ FGristFiles $(sources:S=$(SUFOBJ)) ] ;
            CCFLAGS on $(objects) += $(HOST_PIC_CCFLAGS) ;
            C++FLAGS on $(objects) += $(HOST_PIC_C++FLAGS) ;
        }
]]
rule("BuildPlatformSharedLibrary")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")
    
    on_load(function (target)
        target:set("kind", "shared")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})
        target:values_set("dont_use_beos_rules", {true})
        
        -- Platform-specific flags
        -- Jam: if $(HOST_PLATFORM) = darwin { ... } else { ... }
        if os.host() == "macosx" then
            target:add("ldflags", "-dynamic", "-dynamiclib", {force = true})
            target:add("ldflags", "-Xlinker", "-flat_namespace", {force = true})
        else
            target:add("ldflags", "-shared", {force = true})
            local soname = target:basename()
            target:add("ldflags", "-Xlinker", string.format('-soname="%s"', soname), {force = true})
        end
        
        -- Jam: CCFLAGS on $(objects) += $(HOST_PIC_CCFLAGS) ;
        --      C++FLAGS on $(objects) += $(HOST_PIC_C++FLAGS) ;
        local pic_ccflags = get_config("HOST_PIC_CCFLAGS") or "-fPIC"
        local pic_cxxflags = get_config("HOST_PIC_C++FLAGS") or "-fPIC"
        target:add("cflags", pic_ccflags)
        target:add("cxxflags", pic_cxxflags)
    end)
    
    on_config(function (target)
        local uses_be_api = target:values("uses_be_api")
        if uses_be_api and uses_be_api[1] then
            local host_libroot = get_config("HOST_LIBROOT")
            if host_libroot then
                if type(host_libroot) == "table" then
                    for _, lib in ipairs(host_libroot) do
                        target:add("links", lib)
                    end
                else
                    target:add("links", host_libroot)
                end
            end
        end
    end)

-- ============================================================================
-- BuildPlatformStaticLibrary
-- ============================================================================

--[[
    Jam equivalent:
        rule BuildPlatformStaticLibrary lib : sources : otherObjects
        {
            # BuildPlatformStaticLibrary <lib> : <sources> : <otherObjects> ;
            local objects = [ FGristFiles $(sources:S=$(SUFOBJ)) ] ;
            
            PLATFORM on $(lib) = host ;
            SUPPORTED_PLATFORMS on $(lib) = host ;
            
            local usesBeAPI = [ on $(lib) return $(USES_BE_API) ] ;
            if $(usesBeAPI) {
                USES_BE_API on $(objects) = $(usesBeAPI) ;
            }
            
            StaticLibrary $(lib) : $(sources) : $(otherObjects) ;
        }
]]
rule("BuildPlatformStaticLibrary")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")
    
    on_load(function (target)
        target:set("kind", "static")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})
    end)

-- ============================================================================
-- BuildPlatformStaticLibraryPIC
-- ============================================================================

--[[
    Jam equivalent:
        rule BuildPlatformStaticLibraryPIC target : sources : otherObjects
        {
            # Like BuildPlatformStaticLibrary, but producing position independent code.
            ObjectCcFlags $(sources) : $(HOST_PIC_CCFLAGS) ;
            ObjectC++Flags $(sources) : $(HOST_PIC_C++FLAGS) ;
            
            BuildPlatformStaticLibrary $(target) : $(sources) : $(otherObjects) ;
        }
]]
rule("BuildPlatformStaticLibraryPIC")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")
    
    on_load(function (target)
        target:set("kind", "static")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})
        
        -- Jam: ObjectCcFlags $(sources) : $(HOST_PIC_CCFLAGS) ;
        --      ObjectC++Flags $(sources) : $(HOST_PIC_C++FLAGS) ;
        local pic_ccflags = get_config("HOST_PIC_CCFLAGS") or "-fPIC"
        local pic_cxxflags = get_config("HOST_PIC_C++FLAGS") or "-fPIC"
        target:add("cflags", pic_ccflags)
        target:add("cxxflags", pic_cxxflags)
    end)

-- ============================================================================
-- BuildPlatformMergeObject
-- ============================================================================

--[[
    Jam equivalent:
        rule BuildPlatformMergeObject
        {
            # BuildPlatformMergeObject <name> : <sources> : <other objects> ;
            local target = $(1) ;
            local sources = $(2) ;
            local otherObjects = $(3) ;
            local objects = [ FGristFiles $(sources:S=$(SUFOBJ)) ] ;
            
            PLATFORM on $(target) = host ;
            SUPPORTED_PLATFORMS on $(target) = host ;
            
            local usesBeAPI = [ on $(target[1]) return $(USES_BE_API) ] ;
            if $(usesBeAPI) {
                USES_BE_API on $(objects) = $(usesBeAPI) ;
            }
            
            MergeObject $(target) : $(sources) : $(otherObjects) ;
        }
]]
rule("BuildPlatformMergeObject")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")
    
    on_load(function (target)
        target:set("kind", "object")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})
    end)
    
    on_link(function (target)
        local ld = get_config("HOST_LD") or os.getenv("LD") or "ld"
        local ldflags = {}

        -- Jam: LINKFLAGS on $(target) = $(HOST_LDFLAGS) ;
        -- (set via MergeObjectFromObjects when PLATFORM = host)
        local host_ldflags = get_config("HOST_LDFLAGS")
        if host_ldflags then
            if type(host_ldflags) == "table" then
                ldflags = host_ldflags
            else
                table.insert(ldflags, host_ldflags)
            end
        end

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

            -- Jam action: $(LINK) $(LINKFLAGS) -r $(2) -o $(1)
            local args = {}
            for _, flag in ipairs(ldflags) do
                table.insert(args, flag)
            end
            table.insert(args, "-r")
            table.insert(args, "-o")
            table.insert(args, targetfile)
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
    Jam equivalent:
        rule BuildPlatformMergeObjectPIC target : sources : otherObjects
        {
            # BuildPlatformMergeObjectPIC <name> : <sources> : <other objects> ;
            ObjectCcFlags $(sources) : $(HOST_PIC_CCFLAGS) ;
            ObjectC++Flags $(sources) : $(HOST_PIC_C++FLAGS) ;
            
            BuildPlatformMergeObject $(target) : $(sources) : $(otherObjects) ;
        }
]]
rule("BuildPlatformMergeObjectPIC")
    set_extensions(".cpp", ".c", ".cc", ".cxx", ".S", ".s")
    
    on_load(function (target)
        target:set("kind", "object")
        target:set("plat", "host")
        target:values_set("supported_platforms", {"host"})
        
        local pic_ccflags = get_config("HOST_PIC_CCFLAGS") or "-fPIC"
        local pic_cxxflags = get_config("HOST_PIC_C++FLAGS") or "-fPIC"
        target:add("cflags", pic_ccflags)
        target:add("cxxflags", pic_cxxflags)
    end)
    
    on_link(function (target)
        local ld = get_config("HOST_LD") or os.getenv("LD") or "ld"
        local ldflags = {}

        -- Jam: LINKFLAGS on $(target) = $(HOST_LDFLAGS) ;
        -- (set via MergeObjectFromObjects when PLATFORM = host)
        local host_ldflags = get_config("HOST_LDFLAGS")
        if host_ldflags then
            if type(host_ldflags) == "table" then
                ldflags = host_ldflags
            else
                table.insert(ldflags, host_ldflags)
            end
        end

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

            -- Jam action: $(LINK) $(LINKFLAGS) -r $(2) -o $(1)
            local args = {}
            for _, flag in ipairs(ldflags) do
                table.insert(args, flag)
            end
            table.insert(args, "-r")
            table.insert(args, "-o")
            table.insert(args, targetfile)
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
    Jam equivalent:
        rule BootstrapStage0PlatformObjects sources : separateFromStandardSiblings
        {
            # BootstrapStage0PlatformObjects <sources> : <separateFromStandardSiblings>
            local source ;
            for source in $(sources) {
                local objectGrist ;
                if $(separateFromStandardSiblings) = true {
                    objectGrist = "bootstrap!$(SOURCE_GRIST)" ;
                } else {
                    objectGrist = $(SOURCE_GRIST) ;
                }
                local object = $(source:S=$(SUFOBJ):G=$(objectGrist)) ;
                PLATFORM on $(object) = bootstrap_stage0 ;
                SUPPORTED_PLATFORMS on $(object) = bootstrap_stage0 ;
                if $(separateFromStandardSiblings) = true {
                    MakeLocate $(object) : [
                        FDirName $(TARGET_DEBUG_$(DEBUG)_LOCATE_TARGET) bootstrap
                    ] ;
                }
                Object $(object) : $(source) ;
            }
        }
]]
function BootstrapStage0PlatformObjects(target, sources, separate_from_standard_siblings)
    target:set("plat", "bootstrap_stage0")
    target:values_set("supported_platforms", {"bootstrap_stage0"})
    
    if separate_from_standard_siblings then
        -- Set separate object directory
        local objdir = path.join(target:objectdir(), "bootstrap")
        target:set("objectdir", objdir)
    end
    
    if type(sources) ~= "table" then
        sources = {sources}
    end
    
    for _, source in ipairs(sources) do
        target:add("files", source)
    end
end
