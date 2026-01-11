--[[
    FileRules.lua - Haiku file operation rules

    xmake equivalent of build/jam/FileRules

    Rules defined:
    - CleanDir                - Remove directory recursively
    - Copy                    - Copy file with attributes
    - SymLink                 - Create symbolic link
    - RelSymLink              - Create relative symbolic link
    - AbsSymLink              - Create absolute symbolic link
    - HaikuInstall            - Install files to directory
    - HaikuInstallAbsSymLink  - Install as absolute symlink
    - HaikuInstallRelSymLink  - Install as relative symlink
    - UnarchiveObjects        - Extract objects from static library
    - ExtractArchive          - Extract archive (zip, tgz, hpkg)
    - ObjectReference         - Create reference to object
    - ObjectReferences        - Create references to multiple objects
    - CopySetHaikuRevision    - Copy and set Haiku revision
    - DetermineHaikuRevision  - Determine Haiku git revision
    - DataFileToSourceFile    - Convert data file to C source
    - DownloadFile            - Download file from URL
    - DownloadLocatedFile     - Download to specific location
    - ChecksumFileSHA256      - Calculate SHA256 checksum
    - Sed                     - Perform sed substitutions
    - StripFile               - Strip debug symbols from file
    - StripFiles              - Strip multiple files

    External tools used:
    - copyattr      - Copy with Haiku attributes
    - xres          - Resource tool
    - package       - HPKG extraction
    - data_to_source - Convert binary to C source
    - set_haiku_revision - Set revision in ELF
]]

-- import("core.project.config")

-- ============================================================================
-- Directory Operations
-- ============================================================================

--[[
    CleanDir(dirs)

    Removes directories recursively.

    Equivalent to Jam:
        actions CleanDir { $(RM_DIR) "$(>)" }
]]
function CleanDir(dirs)
    if type(dirs) ~= "table" then
        dirs = {dirs}
    end

    for _, dir in ipairs(dirs) do
        if os.isdir(dir) then
            print("CleanDir: %s", dir)
            os.rmdir(dir)
        end
    end
end

-- ============================================================================
-- Copy Operations
-- ============================================================================

--[[
    Copy(target, source)

    Copies a file using copyattr to preserve Haiku attributes.

    Equivalent to Jam:
        rule Copy { }
        actions Copy1 { "copyattr" -d "$(source)" "$(target)" }

    Parameters:
        target - Destination file path
        source - Source file path
]]
function Copy(target, source)
    if not source then
        return
    end

    print("Copy: %s -> %s", source, target)

    -- Ensure target directory exists
    local target_dir = path.directory(target)
    if target_dir and target_dir ~= "" then
        os.mkdir(target_dir)
    end

    -- Use copyattr if available, otherwise fall back to cp
    local copyattr = "copyattr"
    if os.isexec(copyattr) then
        os.execv(copyattr, {"-d", source, target})
    else
        os.cp(source, target)
    end
end

-- ============================================================================
-- Symbolic Link Operations
-- ============================================================================

--[[
    SymLink(target, source, make_default_deps)

    Creates a symbolic link.

    Equivalent to Jam:
        rule SymLink { }
        actions SymLink1 { $(RM) "$(1)" && $(LN) -s "$(LINKCONTENTS)" "$(1)" }

    Parameters:
        target - Link path to create
        source - Link target (what the link points to)
        make_default_deps - If true, add to default build (default: true)
]]
function SymLink(target, source, make_default_deps)
    if make_default_deps == nil then
        make_default_deps = true
    end

    print("SymLink: %s -> %s", target, source)

    -- Ensure target directory exists
    local target_dir = path.directory(target)
    if target_dir and target_dir ~= "" then
        os.mkdir(target_dir)
    end

    -- Remove existing and create new symlink
    os.rm(target)
    os.ln(source, target)
end

--[[
    RelSymLink(link, link_target, make_default_deps)

    Creates a relative symbolic link.

    Equivalent to Jam:
        rule RelSymLink { }

    Parameters:
        link - Link path to create
        link_target - Target file (will calculate relative path)
        make_default_deps - If true, add to default build
]]
function RelSymLink(link, link_target, make_default_deps)
    if make_default_deps == nil then
        make_default_deps = true
    end

    -- Calculate relative path
    local link_dir = path.directory(link)
    local rel_path = path.relative(link_target, link_dir)

    print("RelSymLink: %s -> %s (relative: %s)", link, link_target, rel_path)

    SymLink(link, rel_path, make_default_deps)
end

--[[
    AbsSymLink(link, link_target, link_dir, make_default_deps)

    Creates an absolute symbolic link.

    Equivalent to Jam:
        rule AbsSymLink { }

    Parameters:
        link - Link path to create
        link_target - Target file (will use absolute path)
        link_dir - Optional directory for the link
        make_default_deps - If true, add to default build
]]
function AbsSymLink(link, link_target, link_dir, make_default_deps)
    if make_default_deps == nil then
        make_default_deps = true
    end

    -- Get absolute path of target
    local abs_target = path.absolute(link_target)

    -- Determine link location
    local link_path = link
    if link_dir then
        link_path = path.join(link_dir, path.filename(link))
    end

    print("AbsSymLink: %s -> %s", link_path, abs_target)

    SymLink(link_path, abs_target, make_default_deps)
end

-- ============================================================================
-- Installation Operations
-- ============================================================================

--[[
    HaikuInstall(install_targets, dir, sources, install_grist, install_rule, targets)

    Installs files to a directory.

    Equivalent to Jam:
        rule HaikuInstall { }

    Parameters:
        install_targets - {install_name, uninstall_name} or just install_name
        dir - Destination directory
        sources - Source files to install
        install_grist - Grist for installed targets
        install_rule - Installation rule function (default: Copy)
        targets - Optional specific target names
]]
function HaikuInstall(install_targets, dir, sources, install_grist, install_rule, targets)
    if type(install_targets) ~= "table" then
        install_targets = {install_targets}
    end

    local install_name = install_targets[1] or "install"
    local uninstall_name = install_targets[2] or ("un" .. install_name)

    install_grist = install_grist or "installed"
    install_rule = install_rule or Copy

    if type(sources) ~= "table" then
        sources = {sources}
    end

    targets = targets or sources

    -- Ensure directory exists
    os.mkdir(dir)

    -- Install each source
    for i, source in ipairs(sources) do
        local target_name = targets[i] or path.filename(source)
        local target_path = path.join(dir, path.filename(target_name))

        print("HaikuInstall: %s -> %s", source, target_path)
        install_rule(target_path, source)
    end
end

--[[
    HaikuInstallAbsSymLink(install_targets, dir, sources, install_grist)

    Installs files as absolute symbolic links.
]]
function HaikuInstallAbsSymLink(install_targets, dir, sources, install_grist)
    local function install_abs_symlink(target, source)
        AbsSymLink(target, source, nil, false)
    end
    HaikuInstall(install_targets, dir, sources, install_grist, install_abs_symlink)
end

--[[
    HaikuInstallRelSymLink(install_targets, dir, sources, install_grist)

    Installs files as relative symbolic links.
]]
function HaikuInstallRelSymLink(install_targets, dir, sources, install_grist)
    local function install_rel_symlink(target, source)
        RelSymLink(target, source, false)
    end
    HaikuInstall(install_targets, dir, sources, install_grist, install_rel_symlink)
end

-- ============================================================================
-- Archive Operations
-- ============================================================================

--[[
    UnarchiveObjects(target_objects, static_lib)

    Extracts object files from a static library.

    Equivalent to Jam:
        rule UnarchiveObjects { }
        actions UnarchiveObjects { ar x "$(2)" $(1:BS) }

    Parameters:
        target_objects - List of object file names to extract
        static_lib - Static library to extract from
]]
function UnarchiveObjects(target_objects, static_lib)
    if type(target_objects) ~= "table" then
        target_objects = {target_objects}
    end

    local output_dir = path.directory(target_objects[1]) or "."
    os.mkdir(output_dir)

    -- Extract specific objects
    local ar = config.get("target_ar") or "ar"
    local args = {"x", static_lib}
    for _, obj in ipairs(target_objects) do
        table.insert(args, path.filename(obj))
    end

    print("UnarchiveObjects: %s <- %s", table.concat(target_objects, ", "), static_lib)

    local old_dir = os.curdir()
    os.cd(output_dir)
    os.execv(ar, args)
    os.cd(old_dir)
end

--[[
    ExtractArchive(directory, entries, archive_file, grist)

    Extracts an archive file (zip, tgz, hpkg).

    Equivalent to Jam:
        rule ExtractArchive { }

    Parameters:
        directory - Extraction directory
        entries - Entries to extract (for tracking)
        archive_file - Archive file path
        grist - Grist for extracted targets (default: "extracted")

    Returns:
        List of extracted entry paths
]]
function ExtractArchive(directory, entries, archive_file, grist)
    grist = grist or "extracted"

    if type(entries) ~= "table" then
        entries = {entries}
    end

    -- Create directory
    os.mkdir(directory)

    -- Determine archive type and extract
    local ext = path.extension(archive_file)

    print("ExtractArchive: %s -> %s", archive_file, directory)

    if ext == ".zip" then
        os.execv("unzip", {"-q", "-u", "-o", "-d", directory, archive_file})

    elseif ext == ".tgz" or ext == ".tar.gz" then
        os.execv("tar", {"-C", directory, "-xf", archive_file})

    elseif ext == ".hpkg" then
        -- Use Haiku package tool
        local package_tool = "package"
        os.execv(package_tool, {"extract", "-C", directory, archive_file})

    else
        error("ExtractArchive: Unhandled archive extension: " .. ext)
    end

    -- Return paths to extracted entries
    local targets = {}
    for _, entry in ipairs(entries) do
        table.insert(targets, path.join(directory, entry))
    end

    return targets
end

-- ============================================================================
-- Object Reference Operations
-- ============================================================================

--[[
    ObjectReference(ref, source)

    Makes ref refer to the same file as source.

    Equivalent to Jam:
        rule ObjectReference { }
]]
function ObjectReference(ref, source)
    -- In xmake, this is handled differently through dependencies
    -- This is mainly for compatibility
    return source
end

--[[
    ObjectReferences(sources)

    Creates local references to source objects.

    Equivalent to Jam:
        rule ObjectReferences { }
]]
function ObjectReferences(sources)
    if type(sources) ~= "table" then
        sources = {sources}
    end
    return sources
end

-- ============================================================================
-- Revision Operations
-- ============================================================================

-- Cache for revision
local _haiku_revision = nil
local _revision_file = nil

--[[
    DetermineHaikuRevision()

    Determines the Haiku git revision.

    Equivalent to Jam:
        rule DetermineHaikuRevision { }

    Returns:
        Path to revision file
]]
function DetermineHaikuRevision()
    if _revision_file then
        return _revision_file
    end

    local haiku_top = os.projectdir()
    local build_dir = config.get("buildir") or "build"
    _revision_file = path.join(build_dir, "haiku-revision")

    -- Check for pre-set revision
    local preset_revision = config.get("haiku_revision")
    if preset_revision then
        io.writefile(_revision_file, preset_revision)
        return _revision_file
    end

    -- Try to get from git
    local git_dir = path.join(haiku_top, ".git")
    if os.isdir(git_dir) then
        local script = path.join(haiku_top, "build", "scripts", "determine_haiku_revision")
        if os.isfile(script) then
            os.mkdir(path.directory(_revision_file))
            os.exec(script .. " " .. haiku_top .. " " .. _revision_file)
        else
            -- Fallback: use git directly
            local revision = os.iorun("git -C " .. haiku_top .. " rev-parse --short HEAD")
            if revision then
                io.writefile(_revision_file, revision:trim())
            end
        end
    else
        error("ERROR: Haiku revision could not be determined.")
    end

    return _revision_file
end

--[[
    CopySetHaikuRevision(target, source)

    Copies file and sets Haiku revision in the ELF.

    Equivalent to Jam:
        rule CopySetHaikuRevision { }
]]
function CopySetHaikuRevision(target, source)
    local revision_file = DetermineHaikuRevision()

    -- Read revision
    local revision = "0"
    if os.isfile(revision_file) then
        revision = io.readfile(revision_file):trim()
    end

    print("CopySetHaikuRevision: %s (rev: %s)", target, revision)

    -- Copy with attributes
    Copy(target, source)

    -- Set revision using set_haiku_revision tool
    local set_revision_tool = "set_haiku_revision"
    if os.isexec(set_revision_tool) then
        os.execv(set_revision_tool, {target, revision})
    end
end

-- ============================================================================
-- Data Conversion Operations
-- ============================================================================

--[[
    DataFileToSourceFile(source_file, data_file, data_variable, size_variable)

    Converts a binary data file to C source code.

    Equivalent to Jam:
        rule DataFileToSourceFile { }

    Parameters:
        source_file - Output C source file
        data_file - Input binary data file
        data_variable - C variable name for data array
        size_variable - C variable name for size (default: dataVariable + "Size")
]]
function DataFileToSourceFile(source_file, data_file, data_variable, size_variable)
    size_variable = size_variable or (data_variable .. "Size")

    print("DataFileToSourceFile: %s -> %s", data_file, source_file)

    -- Ensure output directory exists
    os.mkdir(path.directory(source_file))

    -- Use data_to_source tool if available
    local data_to_source = "data_to_source"
    if os.isexec(data_to_source) then
        os.execv(data_to_source, {data_variable, size_variable, data_file, source_file})
    else
        -- Fallback: generate manually
        local data = io.readfile(data_file, "rb")
        local output = {}

        table.insert(output, "// Auto-generated from " .. path.filename(data_file))
        table.insert(output, "")
        table.insert(output, string.format("const unsigned char %s[] = {", data_variable))

        local bytes = {}
        for i = 1, #data do
            table.insert(bytes, string.format("0x%02x", data:byte(i)))
            if i % 12 == 0 then
                table.insert(output, "    " .. table.concat(bytes, ", ") .. ",")
                bytes = {}
            end
        end
        if #bytes > 0 then
            table.insert(output, "    " .. table.concat(bytes, ", "))
        end

        table.insert(output, "};")
        table.insert(output, "")
        table.insert(output, string.format("const unsigned int %s = %d;", size_variable, #data))

        io.writefile(source_file, table.concat(output, "\n"))
    end
end

-- ============================================================================
-- Download Operations
-- ============================================================================

-- Download cache to prevent duplicate downloads
local _download_cache = {}

--[[
    DownloadLocatedFile(target, url, source)

    Downloads a file from URL to target location.

    Equivalent to Jam:
        rule DownloadLocatedFile { }

    Parameters:
        target - Destination file path
        url - URL to download from
        source - Optional dependency target
]]
function DownloadLocatedFile(target, url, source)
    -- Check if downloads are disabled
    if config.get("haiku_no_downloads") then
        error("ERROR: Would need to download " .. url .. ", but HAIKU_NO_DOWNLOADS is set!")
    end

    print("DownloadLocatedFile: %s <- %s", target, url)

    -- Ensure directory exists
    os.mkdir(path.directory(target))

    -- Use wget
    local wget_flags = {"--retry-connrefused", "--timeout", "30", "-O", target, url}
    os.execv("wget", wget_flags)

    -- Touch to update timestamp
    os.touch(target)
end

--[[
    DownloadFile(file, url, source)

    Downloads a file to the download directory.

    Equivalent to Jam:
        rule DownloadFile { }

    Parameters:
        file - Filename
        url - URL to download from
        source - Optional dependency

    Returns:
        Path to downloaded file
]]
function DownloadFile(file, url, source)
    -- Check cache
    if _download_cache[file] then
        return _download_cache[file]
    end

    local download_dir = config.get("haiku_download_dir")
        or path.join(config.get("buildir") or "build", "download")

    os.mkdir(download_dir)

    local target = path.join(download_dir, file)
    _download_cache[file] = target

    -- Only download if not exists
    if not os.isfile(target) then
        DownloadLocatedFile(target, url, source)
    end

    return target
end

-- ============================================================================
-- Checksum Operations
-- ============================================================================

--[[
    ChecksumFileSHA256(output, input)

    Calculates SHA256 checksum of a file.

    Equivalent to Jam:
        actions ChecksumFileSHA256 { }

    Parameters:
        output - Output file for checksum
        input - Input file to checksum
]]
function ChecksumFileSHA256(output, input)
    print("ChecksumFileSHA256: %s", input)

    local sha256_cmd = config.get("host_sha256") or "sha256sum"
    local result = os.iorun(sha256_cmd .. " " .. input)

    if result then
        -- Extract just the hash (first field)
        local hash = result:match("^(%S+)")
        io.writefile(output, hash)
    end
end

-- ============================================================================
-- Sed Operations
-- ============================================================================

--[[
    Sed(target, source, substitutions, target_map)

    Performs sed substitutions on a file.

    Equivalent to Jam:
        rule Sed { }

    Parameters:
        target - Output file
        source - Input file (if nil, edit target in place)
        substitutions - List of "pattern,replacement" strings
        target_map - Optional variable mappings {"VAR=target", ...}
]]
function Sed(target, source, substitutions, target_map)
    if type(substitutions) ~= "table" then
        substitutions = {substitutions}
    end

    print("Sed: %s", target)

    -- Build sed arguments
    local sed_args = {}
    for _, sub in ipairs(substitutions) do
        table.insert(sed_args, "-e")
        table.insert(sed_args, "s," .. sub .. ",g")
    end

    if source then
        -- Read from source, write to target
        table.insert(sed_args, source)
        local result = os.iorun("sed " .. table.concat(sed_args, " "))
        if result then
            io.writefile(target, result)
        end
    else
        -- In-place edit
        table.insert(sed_args, "-i")
        table.insert(sed_args, target)
        os.execv("sed", sed_args)
    end
end

-- ============================================================================
-- Strip Operations
-- ============================================================================

--[[
    StripFile(target, source)

    Strips debug symbols from a file.

    Equivalent to Jam:
        rule StripFile { }

    Parameters:
        target - Output stripped file
        source - Input file to strip
]]
function StripFile(target, source)
    local arch = config.get("arch") or "x86_64"
    local strip = config.get("haiku_strip_" .. arch) or "strip"

    print("StripFile: %s -> %s", source, target)

    -- Ensure directory exists
    os.mkdir(path.directory(target))

    -- Strip to target
    os.execv(strip, {"-o", target, source})

    -- Copy resources if xres available
    local xres = "xres"
    if os.isexec(xres) then
        os.execv(xres, {"-o", target, source})
    end

    -- Copy attributes if copyattr available
    local copyattr = "copyattr"
    if os.isexec(copyattr) then
        os.execv(copyattr, {source, target})
    end
end

--[[
    StripFiles(files)

    Strips multiple files.

    Equivalent to Jam:
        rule StripFiles { }

    Parameters:
        files - List of files to strip

    Returns:
        List of stripped file paths
]]
function StripFiles(files)
    if type(files) ~= "table" then
        files = {files}
    end

    local stripped_files = {}

    for _, file in ipairs(files) do
        local dir = path.directory(file) or "."
        local stripped_dir = path.join(dir, "stripped")
        local stripped_file = path.join(stripped_dir, path.filename(file))

        os.mkdir(stripped_dir)
        StripFile(stripped_file, file)

        table.insert(stripped_files, stripped_file)
    end

    return stripped_files
end

-- ============================================================================
-- xmake Rules
-- ============================================================================

--[[
    FileOperations rule

    Provides file operation utilities for targets.

    Usage:
        target("myapp")
            add_rules("FileOperations")
            on_install(function (target)
                HaikuInstall("install", "/boot/apps", target:targetfile())
            end)
]]
rule("FileOperations")
    -- This rule mainly provides access to the file operation functions
    -- Functions are available globally after this file is included