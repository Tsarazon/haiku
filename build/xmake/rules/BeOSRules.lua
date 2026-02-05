--[[
    BeOSRules.lua - Haiku resource compilation and BeOS compatibility rules

    xmake equivalent of build/jam/BeOSRules (1:1 migration)

    Rules defined:
    - ResComp              - Compile .rdef to .rsrc (via C preprocessor + rc)
    - XRes                 - Merge resource files into executable
    - ResAttr              - Create attribute file from resources
    - AddFileDataResource  - Add file data as resource
    - AddStringDataResource - Add string as resource
    - AddFileDataAttribute - Add file attribute from data file
    - SetVersion           - Set version info on executable
    - SetType              - Set MIME type on executable
    - MimeSet              - Update MIME database
    - CreateAppMimeDBEntries - Create app MIME DB entries

    External tools used (from build/tools):
    - rc        - Resource compiler
    - xres      - Resource embedder
    - setversion - Version setter
    - settype   - MIME type setter
    - mimeset   - MIME database updater
    - resattr   - Resource to attribute converter
    - addattr   - Attribute adder

    Note: AddResources rule is in MainBuildRules.lua, not here.
]]

local function get_targetfile(target)
    if type(target) == "string" then
        return target
    else
        return target:targetfile()
    end
end

local function get_targetname(target)
    if type(target) == "string" then
        return path.basename(target)
    else
        return target:name()
    end
end

local function get_objectdir(target)
    if type(target) == "string" then
        return path.directory(target)
    else
        return target:objectdir()
    end
end

local function target_data_get(target, key)
    if type(target) == "string" then
        return nil
    end
    return target:data(key)
end

local function target_data_set(target, key, value)
    if type(target) ~= "string" then
        target:data_set(key, value)
    end
end

-- Safe wrappers for description-scope APIs that may not exist in import() sandbox
local function _get_config(key)
    if get_config then
        return get_config(key)
    end
    local ok, config = pcall(import, "core.project.config")
    if ok and config then
        return config.get(key)
    end
    return nil
end

local function _is_host(name)
    if is_host then
        return is_host(name)
    end
    return os.host() == name
end

-- Equivalent to HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR
local function get_build_tool_env()
    local env = {}
    local lib_dir = _get_config("haiku_build_compatibility_lib_dir")
    if lib_dir then
        if _is_host("macosx") then
            env.DYLD_LIBRARY_PATH = lib_dir
        else
            env.LD_LIBRARY_PATH = lib_dir
        end
    end
    return env
end

-- AddFileDataAttribute

--[[
    AddFileDataAttribute <target> : <attrName> : <attrType> : <dataFile>

    Adds a single attribute to a file, retrieving the attribute data from
    a separate file.

    Jam actions:
        CreateAttributeMetaFile: echo "-t $(ATTRIBUTE_TYPE)" "$(ATTRIBUTE_NAME)" > "$(1)"
        AddFileDataAttribute1:   addattr -f $(2[3]) `cat $(2[2])` $(1)
]]
function AddFileDataAttribute(target, attr_name, attr_type, data_file)
    local targetfile = get_targetfile(target)
    local env = get_build_tool_env()

    -- Jam creates a meta file with "-t type attrname", then uses `cat` to read it
    -- We can directly pass the arguments to addattr
    local args = {"-t", attr_type, "-f", data_file, attr_name, targetfile}
    
    print("AddFileDataAttribute: %s[%s] <- %s", targetfile, attr_name, data_file)
    os.vrunv("addattr", args, {envs = env})
end

-- AddStringDataResource

--[[
    AddStringDataResource <target> : <resourceID> : <dataString>

    Adds a single resource to the resources of an executable/library.
    Multiple calls accumulate into a single .rsrc file.

    Jam:
        RESOURCE_STRINGS on $(resources) += "-a "$(resourceID)" -s \""$(dataString)"\"" ;
        actions AddStringDataResource1 { xres -o "$(1)" $(RESOURCE_STRINGS) }
]]
function AddStringDataResource(target, resource_id, data_string)
    data_string = data_string or ""

    -- Accumulate (like Jam's +=)
    local resource_strings = target_data_get(target, "_resource_strings") or {}
    table.insert(resource_strings, {id = resource_id, str = data_string})
    target_data_set(target, "_resource_strings", resource_strings)
    target_data_set(target, "_has_string_resources", true)
end

-- Called later to build the accumulated string resources
function _BuildStringResources(target)
    local strings = target_data_get(target, "_resource_strings")
    if not strings or #strings == 0 then
        return nil
    end

    local objectdir = get_objectdir(target)
    local targetname = get_targetname(target)
    local env = get_build_tool_env()
    
    local rsrc_file = path.join(objectdir, 
        string.format("%s-added-string-data-resources.rsrc", targetname))

    os.mkdir(path.directory(rsrc_file))

    -- Build xres command: xres -o file -a id -s "string" -a id2 -s "string2" ...
    local args = {"-o", rsrc_file}
    for _, s in ipairs(strings) do
        table.insert(args, "-a")
        table.insert(args, s.id)
        table.insert(args, "-s")
        table.insert(args, s.str)
    end

    print("AddStringDataResource: %s", rsrc_file)
    os.vrunv("xres", args, {envs = env})

    return rsrc_file
end

-- AddFileDataResource

--[[
    AddFileDataResource <target> : <resourceID> : <dataFile>

    Adds a single resource to the resources of an executable/library.

    Jam actions:
        AddFileDataResource1 { xres -o "$(1)" -a "$(RESOURCE_ID)" "$(2[2])" }
]]
function AddFileDataResource(target, resource_id, data_file)
    local objectdir = get_objectdir(target)
    local env = get_build_tool_env()

    -- Jam: <added-resources>file-data-$(resourceID)-$(dataFile).rsrc
    local rsrc_file = string.format("file-data-%s-%s.rsrc",
        resource_id:gsub(":", "-"), path.basename(data_file))
    rsrc_file = path.join(objectdir, rsrc_file)

    os.mkdir(path.directory(rsrc_file))

    local args = {"-o", rsrc_file, "-a", resource_id, data_file}
    print("AddFileDataResource: %s <- %s", rsrc_file, data_file)
    os.vrunv("xres", args, {envs = env})

    return rsrc_file
end

-- XRes

--[[
    XRes <target> : <resource files>

    Jam:
        if $(2) { Depends $(1) : <build>xres $(2) ; XRes1 ... }
        actions XRes1 { xres -o "$(1)" "$(2[2-])" }
]]
function XRes(target, rsrc_files)
    if not rsrc_files or #rsrc_files == 0 then
        return
    end

    local targetfile = get_targetfile(target)
    local env = get_build_tool_env()

    local args = {"-o", targetfile}
    for _, rsrc in ipairs(rsrc_files) do
        table.insert(args, rsrc)
    end

    print("XRes: %s <- %s", targetfile, table.concat(rsrc_files, ", "))
    os.vrunv("xres", args, {envs = env})
end

-- SetVersion

--[[
    SetVersion <target>

    Jam actions:
        setversion "$(1)" -system $(HAIKU_BUILD_VERSION) -short "$(HAIKU_BUILD_DESCRIPTION)"
]]
function SetVersion(target)
    local targetfile = get_targetfile(target)
    local env = get_build_tool_env()

    local build_version = _get_config("haiku_build_version") or "1 0 0 a 1"
    local build_description = _get_config("haiku_build_description") or "Developer Build"

    local args = {targetfile, "-system", build_version, "-short", build_description}
    print("SetVersion: %s", targetfile)
    os.vrunv("setversion", args, {envs = env})
end

-- SetType

--[[
    SetType <target> [ : <type> ]

    Sets the MIME type on the target. If none is given, the executable MIME type is used.

    Jam actions:
        settype -t $(TARGET_MIME_TYPE) "$(1)"
]]
function SetType(target, mime_type)
    local targetfile = get_targetfile(target)
    local env = get_build_tool_env()

    -- TARGET_EXECUTABLE_MIME_TYPE from BuildSetup
    mime_type = mime_type or "application/x-vnd.Be-elfexecutable"

    local args = {"-t", mime_type, targetfile}
    print("SetType: %s -> %s", targetfile, mime_type)
    os.vrunv("settype", args, {envs = env})
end

-- MimeSet

--[[
    MimeSet <target>

    Jam actions:
        mimeset -f --mimedb "$(2[2])" "$(1)"
]]
function MimeSet(target)
    local targetfile = get_targetfile(target)
    local env = get_build_tool_env()

    local mimedb = _get_config("haiku_mimedb") or path.join("$(buildir)", "mimedb")

    local args = {"-f", "--mimedb", mimedb, targetfile}
    print("MimeSet: %s", targetfile)
    os.vrunv("mimeset", args, {envs = env})
end

-- CreateAppMimeDBEntries

--[[
    CreateAppMimeDBEntries <target>

    Create the app meta MIME DB entries for the given target.

    Jam actions:
        rm -rf "$appMimeDB"
        mkdir "$appMimeDB"
        mimeset -f --apps --mimedb "$appMimeDB" --mimedb "$(2[3])" "$(2[2])"
]]
function CreateAppMimeDBEntries(target)
    local targetfile = get_targetfile(target)
    local objectdir = get_objectdir(target)
    local targetname = get_targetname(target)
    local env = get_build_tool_env()

    -- Jam: $(target:BS)_mimedb
    local app_mimedb = path.join(objectdir, string.format("%s_mimedb", targetname))
    local mimedb = _get_config("haiku_mimedb") or path.join("$(buildir)", "mimedb")

    os.tryrm(app_mimedb)
    os.mkdir(app_mimedb)

    local args = {"-f", "--apps", "--mimedb", app_mimedb, "--mimedb", mimedb, targetfile}
    print("CreateAppMimeDBEntries: %s -> %s", targetfile, app_mimedb)
    os.vrunv("mimeset", args, {envs = env})

    -- HAIKU_MIME_DB_ENTRIES on $(target) = $(appMimeDB)
    target_data_set(target, "haiku_mime_db_entries", app_mimedb)

    return app_mimedb
end

-- ResComp

--[[
    ResComp <resource file> : <rdef file>

    Compiles .rdef to .rsrc using C preprocessor + rc.

    Jam logic:
        if $(PLATFORM) = host {
            cc = $(HOST_CC)
            defines += $(HOST_DEFINES)
            flags += $(HOST_CCFLAGS)
        } else {
            cc = $(TARGET_CC_$(TARGET_PACKAGING_ARCH))
            defines += $(TARGET_DEFINES_$(TARGET_PACKAGING_ARCH)) $(TARGET_DEFINES)
            flags += $(TARGET_CCFLAGS_$(TARGET_PACKAGING_ARCH))
        }

    Jam actions:
        cat "$(2[2-])" | $(CC) $(CCFLAGS) -E $(CCDEFS) $(HDRS) - \
            | grep -E -v '^#' | $(2[1]) $(RCHDRS) --auto-names -o "$(1)" -
]]
-- rule() is a description-scope API, only available when loaded via includes().
-- When loaded via import() from script scope (e.g. MainBuildRules after_link),
-- rule() is not defined. Guard so import() can succeed and return the function table.
if rule then
    rule("ResComp")
        set_extensions(".rdef")

        on_build_file(function (target, sourcefile, opt)
            import("core.project.depend")
            import("core.tool.compiler")
            import("utils.progress")

            local objectdir = target:objectdir()
            local outputfile = path.join(objectdir,
                path.filename(sourcefile):gsub("%.rdef$", ".rsrc"))

            os.mkdir(path.directory(outputfile))

            -- Get compiler program via core.tool.compiler module
            -- This handles host vs target platform automatically based on target configuration
            local cc
            local compinst = compiler.load("cc", {target = target})
            if compinst then
                cc = compinst:program()
            end
            cc = cc or os.getenv("CC") or "cc"

            -- HDRS: -I for preprocessor
            -- RCHDRS: -I for rc (with space: "-I dir")
            local hdrs = {}
            local rchdrs = {}

            -- SEARCH_SOURCE
            local srcdir = path.directory(sourcefile)
            table.insert(hdrs, "-I" .. srcdir)
            table.insert(rchdrs, "-I " .. srcdir)

            -- SUBDIRHDRS, HDRS - use values() to get flattened array
            for _, dir in ipairs(target:get("includedirs") or {}) do
                table.insert(hdrs, "-I" .. dir)
                table.insert(rchdrs, "-I " .. dir)
            end

            -- SYSHDRS
            for _, dir in ipairs(target:get("sysincludedirs") or {}) do
                table.insert(hdrs, "-I" .. dir)
                table.insert(rchdrs, "-I " .. dir)
            end

            -- CCDEFS = FDefines $(defines)
            local defs = {}
            for _, def in ipairs(target:get("defines") or {}) do
                table.insert(defs, "-D" .. def)
            end

            -- CCFLAGS - get from target
            local ccflags_list = target:get("cflags") or {}
            local ccflags = table.concat(ccflags_list, " ")

            depend.on_changed(function ()
                local cmd = string.format(
                    'cat "%s" | %s %s -E %s %s - | grep -E -v "^#" | rc %s --auto-names -o "%s" -',
                    sourcefile,
                    cc,
                    ccflags,
                    table.concat(defs, " "),
                    table.concat(hdrs, " "),
                    table.concat(rchdrs, " "),
                    outputfile
                )

                os.vrun(cmd)
                progress.show(opt.progress, "${color.build.object}rescomp %s", sourcefile)
            end, {files = sourcefile, dependfile = target:dependfile(outputfile)})

            -- Store for later use by XRes (called from MainBuildRules)
            local rsrc_files = target:data("_compiled_rsrc") or {}
            table.insert(rsrc_files, outputfile)
            target:data_set("_compiled_rsrc", rsrc_files)
        end)
end

-- ResAttr

--[[
    ResAttr <attribute file> : <resource files> [ : <delete file> ]

    Creates attribute file from resources. Compiles .rdef if needed.

    Jam logic for .rdef compilation:
        ResComp $(resourceFile) : $(rdefFile) ;

    Jam actions:
        if [ "$(deleteAttributeFile1)" = "true" ]; then rm $(1); fi
        resattr -O -o "$(1)" "$(2[2-])"
]]
function ResAttr(attribute_file, resource_files, delete_file)
    if delete_file == nil then
        delete_file = true
    end

    local env = get_build_tool_env()

    -- Get compiler for .rdef preprocessing
    local cc = os.getenv("CC")
    if not cc then
        -- Try to find system compiler
        local find_tool = nil
        pcall(function()
            find_tool = import("lib.detect.find_tool")
        end)
        if find_tool then
            local tool = find_tool("gcc") or find_tool("clang") or find_tool("cc")
            if tool then
                cc = tool.program
            end
        end
    end
    cc = cc or "cc"

    -- Compile .rdef to .rsrc if needed (like Jam does via ResComp)
    local compiled = {}
    for _, file in ipairs(resource_files) do
        if file:match("%.rdef$") then
            local rsrcfile = file:gsub("%.rdef$", ".rsrc")
            local srcdir = path.directory(file)
            
            -- Build command matching Jam's ResComp1 action
            local cmd = string.format(
                'cat "%s" | %s -E -I%s - | grep -E -v "^#" | rc -I %s --auto-names -o "%s" -',
                file, cc, srcdir, srcdir, rsrcfile
            )
            os.mkdir(path.directory(rsrcfile))
            os.vrun(cmd)
            table.insert(compiled, rsrcfile)
        else
            table.insert(compiled, file)
        end
    end

    if delete_file then
        os.tryrm(attribute_file)
    end

    os.mkdir(path.directory(attribute_file))

    local args = {"-O", "-o", attribute_file}
    for _, rsrc in ipairs(compiled) do
        table.insert(args, rsrc)
    end

    print("ResAttr: %s", attribute_file)
    os.vrunv("resattr", args, {envs = env})
end

-- Export

return {
    AddFileDataAttribute = AddFileDataAttribute,
    AddStringDataResource = AddStringDataResource,
    AddFileDataResource = AddFileDataResource,
    XRes = XRes,
    SetVersion = SetVersion,
    SetType = SetType,
    MimeSet = MimeSet,
    CreateAppMimeDBEntries = CreateAppMimeDBEntries,
    ResAttr = ResAttr,
    
    -- Internal
    _BuildStringResources = _BuildStringResources,
}
