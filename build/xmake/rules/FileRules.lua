--[[
    FileRules.lua - Haiku file operation rules

    xmake equivalent of build/jam/FileRules (1:1 migration)

    Rules/functions for file operations:
    - Copy(target, source)                        - Copy with attributes via copyattr
    - SymLink(target, source)                     - Create symbolic link
    - RelSymLink(target, source)                  - Create relative symbolic link
    - AbsSymLink(target, source, link_dir)        - Create absolute symbolic link
    - HaikuInstall(dir, sources, install_rule)    - Install files to directory
    - HaikuInstallAbsSymLink(dir, sources)        - Install as absolute symlinks
    - HaikuInstallRelSymLink(dir, sources)        - Install as relative symlinks
    - UnarchiveObjects(objects, archive)           - Extract objects from static archive
    - ExtractArchive(directory, entries, archive)  - Extract zip/tar/hpkg archive
    - CopySetHaikuRevision(target, source)        - Copy and set git revision in ELF
    - DetermineHaikuRevision()                    - Get git revision string
    - DataFileToSourceFile(source_file, data_file, data_var, size_var)
    - DownloadFile(file, url)                     - Download file via wget
    - ChecksumFileSHA256(target, source)          - Compute SHA256
    - Sed(target, source, substitutions)          - Text substitutions
    - StripFile(target, source)                   - Strip debug symbols
    - StripFiles(files)                           - Strip multiple files

    External tools: copyattr, xres, strip, wget, sha256sum, ar, package,
                    data_to_source, set_haiku_revision
]]

-- Copy: copy file preserving Haiku attributes via copyattr
function Copy(target_path, source_path)
    import("core.project.config")
    import("core.project.depend")

    local copyattr = config.get("build_copyattr") or "copyattr"

    depend.on_changed(function()
        os.mkdir(path.directory(target_path))
        os.vrunv(copyattr, {"-d", source_path, target_path})
    end, {
        files = {source_path},
        dependfile = target_path .. ".d"
    })
end

-- SymLink: create a symbolic link
function SymLink(target_path, link_contents)
    import("core.project.depend")

    depend.on_changed(function()
        os.mkdir(path.directory(target_path))
        if os.isfile(target_path) or os.islink(target_path) then
            os.rm(target_path)
        end
        os.ln(link_contents, target_path)
    end, {
        dependfile = target_path .. ".symlink.d"
    })
end

-- RelSymLink: create a relative symbolic link between two paths
function RelSymLink(link_path, target_path)
    import("core.project.depend")

    depend.on_changed(function()
        os.mkdir(path.directory(link_path))

        -- Compute relative path from link directory to target
        local link_dir = path.directory(link_path)
        local rel = path.relative(target_path, link_dir)

        if os.isfile(link_path) or os.islink(link_path) then
            os.rm(link_path)
        end
        os.ln(rel, link_path)
    end, {
        files = {target_path},
        dependfile = link_path .. ".relsymlink.d"
    })
end

-- AbsSymLink: create an absolute symbolic link
function AbsSymLink(link_path, target_path)
    import("core.project.depend")

    depend.on_changed(function()
        os.mkdir(path.directory(link_path))

        local abs_target = path.absolute(target_path)
        if os.isfile(link_path) or os.islink(link_path) then
            os.rm(link_path)
        end
        os.ln(abs_target, link_path)
    end, {
        files = {target_path},
        dependfile = link_path .. ".abssymlink.d"
    })
end

-- HaikuInstall: install files to a directory
-- install_func(target_path, source_path) is the installation action
function HaikuInstall(dir, sources, install_func, mode, owner, group)
    install_func = install_func or Copy

    for _, source in ipairs(sources) do
        local filename = path.filename(source)
        local target_path = path.join(dir, filename)

        install_func(target_path, source)

        -- Set permissions if specified
        if mode then
            os.exec("chmod %s %s", mode, target_path)
        end
        if owner then
            os.exec("chown %s %s", owner, target_path)
        end
        if group then
            os.exec("chgrp %s %s", group, target_path)
        end
    end
end

-- HaikuInstallAbsSymLink: install as absolute symlinks
function HaikuInstallAbsSymLink(dir, sources)
    HaikuInstall(dir, sources, AbsSymLink)
end

-- HaikuInstallRelSymLink: install as relative symlinks
function HaikuInstallRelSymLink(dir, sources)
    HaikuInstall(dir, sources, RelSymLink)
end

-- UnarchiveObjects: extract specific objects from a static archive
function UnarchiveObjects(target, objects, archive, object_dir)
    import("core.project.config")
    import("core.project.depend")

    local arch = config.get("target_packaging_arch") or config.get("arch") or "x86_64"
    local ar = config.get("target_ar_" .. arch) or "ar"
    local unarflags = config.get("target_unarflags_" .. arch) or "x"

    depend.on_changed(function()
        os.mkdir(object_dir)
        local cwd = os.cd(object_dir)
        os.vrunv(ar, {unarflags, archive, table.unpack(objects)})
        os.cd(cwd)
    end, {
        files = {archive},
        dependfile = target:dependfile(path.join(object_dir, "unarchive"))
    })
end

-- ExtractArchive: extract an archive (zip, tar, hpkg) to a directory
function ExtractArchive(target, directory, archive)
    import("core.project.config")
    import("core.project.depend")

    depend.on_changed(function()
        os.mkdir(directory)

        local ext = path.extension(archive)
        if ext == ".zip" then
            os.vrunv("unzip", {"-q", "-u", "-o", "-d", directory, archive})
        elseif ext == ".tgz" or ext == ".tar.gz" or ext == ".tar" then
            os.vrunv("tar", {"-C", directory, "-xf", archive})
        elseif ext == ".hpkg" then
            local package_tool = config.get("build_package") or "package"
            os.vrunv(package_tool, {"extract", "-C", directory, archive})
        else
            raise("ExtractArchive: Unhandled archive extension: " .. ext)
        end
    end, {
        files = {archive},
        dependfile = target:dependfile(directory .. ".extract")
    })
end

-- DetermineHaikuRevision: determine git revision of the working directory
-- Returns the revision string
function DetermineHaikuRevision()
    import("core.project.config")

    local haiku_top = config.get("haiku_top") or os.projectdir()
    local output_dir = config.get("build_output_dir") or
        path.join(haiku_top, "generated")
    local revision_file = path.join(output_dir, "haiku-revision")

    -- Check if pre-set revision
    local preset = config.get("haiku_revision")
    if preset then
        io.writefile(revision_file, preset)
        return revision_file
    end

    -- Determine from git
    local script = path.join(haiku_top, "build", "scripts", "determine_haiku_revision")
    if os.isfile(script) then
        os.vrunv(script, {haiku_top, revision_file})
    else
        -- Fallback: use git directly
        local git_dir = path.join(haiku_top, ".git")
        if os.isdir(git_dir) then
            local rev = os.iorunv("git", {"-C", haiku_top, "rev-parse", "--short", "HEAD"})
            if rev then
                io.writefile(revision_file, rev:trim())
            end
        else
            raise("ERROR: Haiku revision could not be determined.")
        end
    end

    return revision_file
end

-- CopySetHaikuRevision: copy file and set haiku revision in ELF section
function CopySetHaikuRevision(target, target_path, source_path)
    import("core.project.config")
    import("core.project.depend")

    local copyattr = config.get("build_copyattr") or "copyattr"
    local set_revision = config.get("build_set_haiku_revision") or "set_haiku_revision"

    depend.on_changed(function()
        os.mkdir(path.directory(target_path))

        -- Copy with data attributes
        os.vrunv(copyattr, {"--data", source_path, target_path})

        -- Determine and set revision
        local revision_file = DetermineHaikuRevision()
        local revision = "0"
        if os.isfile(revision_file) then
            revision = io.readfile(revision_file):trim()
        end
        os.vrunv(set_revision, {target_path, revision})
    end, {
        files = {source_path},
        dependfile = target:dependfile(target_path .. ".rev")
    })
end

-- DataFileToSourceFile: convert binary data file to C source file
function DataFileToSourceFile(target, source_file, data_file, data_variable, size_variable)
    import("core.project.config")
    import("core.project.depend")

    size_variable = size_variable or (data_variable .. "Size")
    local data_to_source = config.get("build_data_to_source") or "data_to_source"

    depend.on_changed(function()
        os.mkdir(path.directory(source_file))
        os.vrunv(data_to_source, {data_variable, size_variable, data_file, source_file})
    end, {
        files = {data_file},
        dependfile = target:dependfile(source_file)
    })
end

-- DownloadFile: download a file from a URL using wget
function DownloadFile(target_path, url, no_downloads)
    import("core.project.config")
    import("core.project.depend")

    no_downloads = no_downloads or config.get("haiku_no_downloads")

    depend.on_changed(function()
        if no_downloads then
            raise(string.format(
                "ERROR: Would need to download %s, but HAIKU_NO_DOWNLOADS is set!", url))
        end

        os.mkdir(path.directory(target_path))

        local retry_flags = {"--retry-connrefused", "--timeout", "30"}
        local retry_on_host = config.get("host_wget_retry_on_host_error")
        if retry_on_host then
            table.insert(retry_flags, "--retry-on-host-error")
        end

        local args = {}
        for _, f in ipairs(retry_flags) do table.insert(args, f) end
        table.insert(args, "-O")
        table.insert(args, target_path)
        table.insert(args, url)

        os.vrunv("wget", args)
    end, {
        dependfile = target_path .. ".download.d"
    })

    return target_path
end

-- ChecksumFileSHA256: compute SHA256 checksum and write to target
function ChecksumFileSHA256(target_path, source_path)
    import("core.project.config")
    import("core.project.depend")

    local sha256 = config.get("host_sha256") or "sha256sum"

    depend.on_changed(function()
        os.mkdir(path.directory(target_path))
        local output = os.iorunv(sha256, {source_path})
        if output then
            -- Extract just the hash (first field)
            local hash = output:match("^(%S+)")
            io.writefile(target_path, hash or output)
        end
    end, {
        files = {source_path},
        dependfile = target_path .. ".sha256.d"
    })
end

-- Sed: perform text substitutions in a file
-- substitutions is a list of {pattern, replacement} pairs
function Sed(target_path, source_path, substitutions, variables)
    import("core.project.depend")

    local input = source_path or target_path

    depend.on_changed(function()
        os.mkdir(path.directory(target_path))
        local content = io.readfile(input)

        -- Apply variable substitutions if provided
        if variables then
            for var_name, var_value in pairs(variables) do
                content = content:gsub("%$" .. var_name, var_value)
            end
        end

        -- Apply sed-style substitutions
        for _, sub in ipairs(substitutions) do
            local pattern = sub[1] or sub.pattern
            local replacement = sub[2] or sub.replacement
            if pattern and replacement then
                content = content:gsub(pattern, replacement)
            end
        end

        io.writefile(target_path, content)
    end, {
        files = {input},
        dependfile = target_path .. ".sed.d"
    })
end

-- StripFile: strip debug symbols from an ELF binary
function StripFile(target, target_path, source_path)
    import("core.project.config")
    import("core.project.depend")

    local arch = config.get("target_packaging_arch") or config.get("arch") or "x86_64"
    local strip = config.get("haiku_strip_" .. arch) or "strip"
    local xres = config.get("build_xres") or "xres"
    local copyattr = config.get("build_copyattr") or "copyattr"

    depend.on_changed(function()
        os.mkdir(path.directory(target_path))
        -- Strip symbols
        os.vrunv(strip, {"-o", target_path, source_path})
        -- Copy resources
        os.vrunv(xres, {"-o", target_path, source_path})
        -- Copy attributes
        os.vrunv(copyattr, {source_path, target_path})
    end, {
        files = {source_path},
        dependfile = target:dependfile(target_path .. ".strip")
    })
end

-- StripFiles: strip multiple files, placing results in a "stripped" subdirectory
function StripFiles(target, files)
    import("core.project.config")

    local stripped_files = {}
    for _, file in ipairs(files) do
        local dir = path.directory(file)
        local stripped_dir = path.join(dir, "stripped")
        local stripped_file = path.join(stripped_dir, path.filename(file))

        StripFile(target, stripped_file, file)
        table.insert(stripped_files, stripped_file)
    end

    return stripped_files
end
