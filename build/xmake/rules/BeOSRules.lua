--[[
    BeOSRules.lua - Haiku resource compilation and BeOS compatibility rules

    xmake equivalent of build/jam/BeOSRules

    Rules defined:
    - ResComp              - Compile .rdef to .rsrc (via C preprocessor + rc)
    - XRes                 - Merge resource files into executable
    - ResAttr              - Create attribute file from resources
    - AddResources         - Add resource files to target
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
]]

-- ============================================================================
-- Configuration
-- ============================================================================

-- Get platform (host or target)
local function get_platform()
    local plat = get_config("plat")
    if plat == "haiku" then
        return "haiku"
    end
    return "host"
end

-- Get C compiler for preprocessing
local function get_cc(target)
    local platform = get_platform()
    if platform == "host" then
        return "cc"
    else
        local arch = get_config("arch") or "x86_64"
        return format("%s-unknown-haiku-gcc", arch)
    end
end

-- Get build tools directory
local function get_build_tools_dir()
    return "$(buildir)/tools"
end

-- ============================================================================
-- ResComp - Compile .rdef to .rsrc
-- ============================================================================

--[[
    ResComp(rsrc, rdef)

    Compiles a .rdef file to .rsrc using C preprocessor and rc tool.

    Process:
    1. Pipe .rdef through C preprocessor (for #include, #define support)
    2. Filter out preprocessor # lines
    3. Pass to rc resource compiler

    Equivalent to Jam rule:
        rule ResComp { }
        actions ResComp1 { cat "$(2[2-])" | $(CC) -E ... - | grep -v '^#' | rc ... }
]]
rule("ResComp")
    set_extensions(".rdef")

    on_build_file(function (target, sourcefile, opt)
        local rsrcfile = sourcefile:gsub("%.rdef$", ".rsrc")
        local objectdir = target:objectdir()
        local outputfile = path.join(objectdir, path.filename(rsrcfile))

        -- Get compiler
        local cc = get_cc(target)

        -- Build include paths for preprocessor
        local hdrs = {}
        local rchdrs = {}

        -- Add source directory
        local srcdir = path.directory(sourcefile)
        table.insert(hdrs, format("-I%s", srcdir))
        table.insert(rchdrs, format("-I %s", srcdir))

        -- Add target include directories
        for _, dir in ipairs(target:get("includedirs") or {}) do
            table.insert(hdrs, format("-I%s", dir))
            table.insert(rchdrs, format("-I %s", dir))
        end

        -- Build defines
        local defs = {}
        for _, def in ipairs(target:get("defines") or {}) do
            table.insert(defs, format("-D%s", def))
        end

        -- Get CCFLAGS
        local ccflags = target:get("cflags") or {}

        os.mkdir(path.directory(outputfile))

        -- Build the command pipeline:
        -- cat file.rdef | cc -E ... - | grep -E -v '^#' | rc ... -o file.rsrc -
        local cmd = format(
            'cat "%s" | %s %s -E %s %s - | grep -E -v "^#" | rc %s --auto-names -o "%s" -',
            sourcefile,
            cc,
            table.concat(ccflags, " "),
            table.concat(defs, " "),
            table.concat(hdrs, " "),
            table.concat(rchdrs, " "),
            outputfile
        )

        print("ResComp: %s -> %s", sourcefile, outputfile)
        os.exec(cmd)

        -- Store compiled rsrc for later XRes
        local rsrc_files = target:get("_compiled_rsrc") or {}
        table.insert(rsrc_files, outputfile)
        target:set("_compiled_rsrc", rsrc_files)
    end)

-- ============================================================================
-- XRes - Merge resources into executable
-- ============================================================================

--[[
    XRes(target, resource_files)

    Merges resource files (.rsrc) into an executable.

    Equivalent to Jam:
        rule XRes { }
        actions XRes1 { xres -o "$(1)" "$(2[2-])" }
]]
function XRes(target, rsrc_files)
    if not rsrc_files or #rsrc_files == 0 then
        return
    end

    local targetfile
    if type(target) == "string" then
        targetfile = target
    else
        targetfile = target:targetfile()
    end

    local args = {"-o", targetfile}
    for _, rsrc in ipairs(rsrc_files) do
        table.insert(args, rsrc)
    end

    print("XRes: %s <- %s", targetfile, table.concat(rsrc_files, ", "))
    os.execv("xres", args)
end

-- ============================================================================
-- ResAttr - Convert resources to attributes
-- ============================================================================

--[[
    ResAttr(attribute_file, resource_files, delete_file)

    Creates an attribute file from resource files.

    Equivalent to Jam:
        rule ResAttr { }
        actions ResAttr1 { resattr -O -o "$(1)" "$(2[2-])" }
]]
function ResAttr(attribute_file, resource_files, delete_file)
    delete_file = delete_file or true

    if delete_file then
        os.rm(attribute_file)
    end

    local args = {"-O", "-o", attribute_file}
    for _, rsrc in ipairs(resource_files) do
        table.insert(args, rsrc)
    end

    print("ResAttr: %s", attribute_file)
    os.execv("resattr", args)
end

-- ============================================================================
-- AddResources - Add resource files to target
-- ============================================================================

--[[
    AddResources(target, resource_files)

    Adds resource files to a target. Handles .rdef compilation.

    Equivalent to Jam:
        rule AddResources { }
]]
function AddResources(target, resource_files)
    if type(resource_files) ~= "table" then
        resource_files = {resource_files}
    end

    local rsrc_files = target:get("_compiled_rsrc") or {}

    -- Add direct .rsrc files
    for _, file in ipairs(resource_files) do
        if file:match("%.rsrc$") then
            table.insert(rsrc_files, file)
        end
        -- .rdef files are handled by ResComp rule during build
    end

    -- Embed all resources
    if #rsrc_files > 0 then
        XRes(target, rsrc_files)
    end
end

-- ============================================================================
-- AddFileDataResource - Add file as resource
-- ============================================================================

--[[
    AddFileDataResource(target, resource_id, data_file)

    Adds file contents as a resource.

    resource_id format: "type:id[:name]" (as understood by xres)

    Equivalent to Jam:
        rule AddFileDataResource { }
        actions AddFileDataResource1 { xres -o "$(1)" -a "$(RESOURCE_ID)" "$(2[2])" }
]]
function AddFileDataResource(target, resource_id, data_file)
    local targetfile
    if type(target) == "string" then
        targetfile = target
    else
        targetfile = target:targetfile()
    end

    -- Create intermediate .rsrc file
    local rsrc_file = format("%s-file-data-%s.rsrc",
        path.basename(targetfile), resource_id:gsub(":", "-"))
    local objectdir = target:objectdir()
    rsrc_file = path.join(objectdir, rsrc_file)

    os.mkdir(path.directory(rsrc_file))

    local args = {"-o", rsrc_file, "-a", resource_id, data_file}
    print("AddFileDataResource: %s <- %s", rsrc_file, data_file)
    os.execv("xres", args)

    -- Add to target's resources
    local rsrc_files = target:get("_compiled_rsrc") or {}
    table.insert(rsrc_files, rsrc_file)
    target:set("_compiled_rsrc", rsrc_files)
end

-- ============================================================================
-- AddStringDataResource - Add string as resource
-- ============================================================================

--[[
    AddStringDataResource(target, resource_id, data_string)

    Adds a string as a resource.

    resource_id format: "type:id[:name]"

    Equivalent to Jam:
        rule AddStringDataResource { }
        actions AddStringDataResource1 { xres -o "$(1)" -a "id" -s "string" }
]]
function AddStringDataResource(target, resource_id, data_string)
    data_string = data_string or ""

    local targetfile
    if type(target) == "string" then
        targetfile = target
    else
        targetfile = target:targetfile()
    end

    -- Create intermediate .rsrc file
    local rsrc_file = format("%s-string-data-resources.rsrc", path.basename(targetfile))
    local objectdir = target:objectdir()
    rsrc_file = path.join(objectdir, rsrc_file)

    os.mkdir(path.directory(rsrc_file))

    local args = {"-o", rsrc_file, "-a", resource_id, "-s", data_string}
    print("AddStringDataResource: %s", resource_id)
    os.execv("xres", args)

    -- Add to target's resources
    local rsrc_files = target:get("_compiled_rsrc") or {}
    table.insert(rsrc_files, rsrc_file)
    target:set("_compiled_rsrc", rsrc_files)
end

-- ============================================================================
-- AddFileDataAttribute - Add file attribute
-- ============================================================================

--[[
    AddFileDataAttribute(target, attr_name, attr_type, data_file)

    Adds a file attribute from data in another file.

    Equivalent to Jam:
        rule AddFileDataAttribute { }
        actions AddFileDataAttribute1 { addattr -f datafile -t type attrname target }
]]
function AddFileDataAttribute(target, attr_name, attr_type, data_file)
    local targetfile
    if type(target) == "string" then
        targetfile = target
    else
        targetfile = target:targetfile()
    end

    local args = {"-f", data_file, "-t", attr_type, attr_name, targetfile}
    print("AddFileDataAttribute: %s[%s] <- %s", targetfile, attr_name, data_file)
    os.execv("addattr", args)
end

-- ============================================================================
-- SetVersion - Set executable version
-- ============================================================================

--[[
    SetVersion(target)

    Sets version information on an executable.
    Uses HAIKU_BUILD_VERSION and HAIKU_BUILD_DESCRIPTION.

    Equivalent to Jam:
        rule SetVersion { }
        actions SetVersion1 {
            setversion "$(1)" -system $(HAIKU_BUILD_VERSION) -short "$(HAIKU_BUILD_DESCRIPTION)"
        }
]]
function SetVersion(target)
    local targetfile
    if type(target) == "string" then
        targetfile = target
    else
        targetfile = target:targetfile()
    end

    -- Get version info from config or defaults
    local build_version = get_config("haiku_build_version") or "1 0 0 a 1"
    local build_description = get_config("haiku_build_description") or "Haiku"

    local args = {targetfile, "-system", build_version, "-short", build_description}
    print("SetVersion: %s", targetfile)
    os.execv("setversion", args)
end

-- ============================================================================
-- SetType - Set MIME type
-- ============================================================================

--[[
    SetType(target, mime_type)

    Sets the MIME type on a file.

    Equivalent to Jam:
        rule SetType { }
        actions SetType1 { settype -t $(TARGET_MIME_TYPE) "$(1)" }
]]
function SetType(target, mime_type)
    local targetfile
    if type(target) == "string" then
        targetfile = target
    else
        targetfile = target:targetfile()
    end

    -- Default to executable MIME type
    mime_type = mime_type or "application/x-vnd.Be-elfexecutable"

    local args = {"-t", mime_type, targetfile}
    print("SetType: %s -> %s", targetfile, mime_type)
    os.execv("settype", args)
end

-- ============================================================================
-- MimeSet - Update MIME database
-- ============================================================================

--[[
    MimeSet(target)

    Updates MIME database for the target file.

    Equivalent to Jam:
        rule MimeSet { }
        actions MimeSet1 { mimeset -f --mimedb "$(2[2])" "$(1)" }
]]
function MimeSet(target)
    local targetfile
    if type(target) == "string" then
        targetfile = target
    else
        targetfile = target:targetfile()
    end

    -- Get MIME database path
    local mimedb = get_config("haiku_mimedb") or "$(buildir)/mimedb"

    local args = {"-f", "--mimedb", mimedb, targetfile}
    print("MimeSet: %s", targetfile)
    os.execv("mimeset", args)
end

-- ============================================================================
-- CreateAppMimeDBEntries - Create app MIME DB entries
-- ============================================================================

--[[
    CreateAppMimeDBEntries(target)

    Creates MIME database entries for an application.

    Equivalent to Jam:
        rule CreateAppMimeDBEntries { }
        actions CreateAppMimeDBEntries1 {
            rm -rf "$appMimeDB"
            mkdir "$appMimeDB"
            mimeset -f --apps --mimedb "$appMimeDB" --mimedb "$(2[3])" "$(2[2])"
        }
]]
function CreateAppMimeDBEntries(target)
    local targetfile
    if type(target) == "string" then
        targetfile = target
    else
        targetfile = target:targetfile()
    end

    local app_mimedb = format("%s_mimedb", path.basename(targetfile))
    local objectdir = target:objectdir()
    app_mimedb = path.join(objectdir, app_mimedb)

    -- Get system MIME database path
    local mimedb = get_config("haiku_mimedb") or "$(buildir)/mimedb"

    os.rm(app_mimedb)
    os.mkdir(app_mimedb)

    local args = {"-f", "--apps", "--mimedb", app_mimedb, "--mimedb", mimedb, targetfile}
    print("CreateAppMimeDBEntries: %s -> %s", targetfile, app_mimedb)
    os.execv("mimeset", args)

    -- Store path for later use
    if type(target) ~= "string" then
        target:set("haiku_mime_db_entries", app_mimedb)
    end

    return app_mimedb
end

-- ============================================================================
-- HaikuResources rule for targets
-- ============================================================================

--[[
    HaikuResources rule for xmake targets

    Enables automatic resource compilation and embedding.

    Usage:
        target("myapp")
            add_rules("HaikuResources")
            add_files("app.rdef")
            set_values("resources", {"icon.rsrc"})
]]
rule("HaikuResources")
    add_deps("ResComp")

    after_build(function (target)
        local rsrc_files = target:get("_compiled_rsrc") or {}
        local extra_resources = target:get("resources") or {}

        -- Add explicit .rsrc files
        for _, file in ipairs(extra_resources) do
            if file:match("%.rsrc$") then
                table.insert(rsrc_files, file)
            end
        end

        -- Embed all resources
        if #rsrc_files > 0 then
            XRes(target, rsrc_files)
        end

        -- Apply BeOS rules unless disabled
        if not target:get("dont_use_beos_rules") then
            SetType(target)
            MimeSet(target)
            SetVersion(target)
        end
    end)
