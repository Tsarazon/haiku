--[[
    ImageRules.lua - Haiku image and container build rules

    xmake equivalent of build/jam/ImageRules (1:1 migration)

    This module manages "containers" — abstract image targets (haiku image,
    net boot archive, CD image, etc.) that accumulate directories, files,
    symlinks, and archives. At build time, shell scripts are generated to
    create the directories, copy files, create symlinks, and extract archives.

    Sections:
    1. Container infrastructure (generic)
    2. Script generation (mkdir, copy, extract)
    3. HaikuImage convenience wrappers
    4. NetBootArchive convenience wrappers
    5. CD/DVD boot image rules
    6. EFI system partition rules
    7. VMWare image rules

    Usage:
        import("rules.ImageRules")
        ImageRules.AddFilesToHaikuImage({"system", "lib"}, {"libroot.so"})
]]

-- ============================================================================
-- Module-level state
-- ============================================================================

-- Container registry: name -> container table
-- Container structure:
-- {
--   name = string,
--   grist = string,
--   system_dir_tokens = {string, ...},
--   update_only = bool,
--   strip_executables = bool,
--   always_create_directories = bool,
--   include_in_container_var = string,
--   inherit_update_variable = string,
--   install_targets_var = string,
--   directories = { [dir_key] = dir_entry, ... },
--   directory_order = {dir_key, ...},
--   system_packages = {target, ...},
--   other_packages = {target, ...},
-- }
-- Directory entry:
-- {
--   tokens = {string, ...},
--   dont_create = bool,
--   attribute_files = {string, ...},
--   targets_to_install = { {target=, name=, name_function=, install_dir=}, ... },
--   symlinks_to_install = { {link_path=, link_target=}, ... },
--   directories_to_install = { {source=, target_dir=, exclude_patterns={}}, ... },
--   archive_files = { {archive=, subdir=}, ... },
-- }
local _containers = {}

-- Named container identifiers
local _haiku_image_container = "haiku-image"
local _net_boot_archive_container = "haiku-net-boot-archive"

-- Optional packages tracking
local _optional_packages_added = {}     -- set: name -> true
local _optional_packages_suppressed = {} -- set: name -> true
local _optional_packages_exists = {}    -- set: name -> true
local _optional_package_dependencies = {} -- name -> {dep, ...}
local _added_optional_packages = {}     -- ordered list
local _existing_optional_packages = {}  -- ordered list

-- Added packages tracking
local _added_packages = {}              -- set: name -> true
local _added_packages_list = {}         -- ordered list

-- ============================================================================
-- Internal helpers
-- ============================================================================

-- Get or create a container
local function _get_container(name)
    if not _containers[name] then
        _containers[name] = {
            name = name,
            grist = name,
            system_dir_tokens = {"system"},
            update_only = false,
            strip_executables = false,
            always_create_directories = false,
            include_in_container_var = nil,
            inherit_update_variable = nil,
            install_targets_var = "install_targets",
            directories = {},
            directory_order = {},
            system_packages = {},
            other_packages = {},
        }
    end
    return _containers[name]
end

-- Create a directory key from tokens
local function _dir_key(tokens)
    return table.concat(tokens, "/")
end

-- Get or create a directory entry in a container
local function _get_directory(container, dir_tokens)
    local key = _dir_key(dir_tokens)
    if not container.directories[key] then
        container.directories[key] = {
            tokens = dir_tokens,
            dont_create = false,
            attribute_files = {},
            targets_to_install = {},
            symlinks_to_install = {},
            directories_to_install = {},
            archive_files = {},
        }
        table.insert(container.directory_order, key)

        -- Mark parent as don't-create
        if #dir_tokens > 1 then
            local parent_tokens = {}
            for i = 1, #dir_tokens - 1 do
                table.insert(parent_tokens, dir_tokens[i])
            end
            local parent_key = _dir_key(parent_tokens)
            if container.directories[parent_key] then
                container.directories[parent_key].dont_create = true
            end
        end
    end
    return container.directories[key]
end


-- ============================================================================
-- 1. Container infrastructure
-- ============================================================================

-- AddDirectoryToContainer: register a directory in a container
-- Returns the directory key for further operations
function AddDirectoryToContainer(container_name, dir_tokens, attribute_files)
    local container = _get_container(container_name)
    local dir = _get_directory(container, dir_tokens)

    if attribute_files then
        import("core.project.config")
        local haiku_top = config.get("haiku_top") or os.projectdir()
        local attrs_dir = path.join(haiku_top, "src", "data", "directory_attrs")

        for _, af in ipairs(attribute_files) do
            -- If not absolute, search in directory_attrs
            if not path.is_absolute(af) then
                local full = path.join(attrs_dir, af)
                if os.isfile(full) then
                    af = full
                end
            end
            table.insert(dir.attribute_files, af)
        end
    end

    return _dir_key(dir_tokens)
end


-- AddFilesToContainer: add files to a container directory
-- flags: table with optional keys: computeName, alwaysUpdate
function AddFilesToContainer(container_name, dir_tokens, targets, dest_name, flags)
    import("rules.BuildFeatureRules")

    local container = _get_container(container_name)
    targets = BuildFeatureRules.FFilterByBuildFeatures(targets or {})

    flags = flags or {}
    local flags_set = {}
    if type(flags) == "table" then
        for _, f in ipairs(flags) do flags_set[f] = true end
    end

    -- Update-only filtering
    if container.update_only and not flags_set["alwaysUpdate"] then
        -- In update mode, only include targets marked for update
        -- (In practice, this is managed externally)
    end

    if #targets == 0 then return end

    AddDirectoryToContainer(container_name, dir_tokens)
    local dir_key = _dir_key(dir_tokens)
    local dir = container.directories[dir_key]

    for _, target in ipairs(targets) do
        local name
        local name_function
        if dest_name then
            if flags_set["computeName"] then
                name_function = dest_name
                name = dest_name .. "/" .. path.filename(target)
            else
                name = dest_name
            end
        else
            name = path.filename(target)
        end

        table.insert(dir.targets_to_install, {
            target = target,
            name = name,
            name_function = name_function,
            install_dir = dir_key,
        })
    end
end


-- FFilesInContainerDirectory: get list of files registered in a container directory
function FFilesInContainerDirectory(container_name, dir_tokens)
    local container = _containers[container_name]
    if not container then return {} end

    local dir_key = _dir_key(dir_tokens)
    local dir = container.directories[dir_key]
    if not dir then return {} end

    local result = {}
    for _, entry in ipairs(dir.targets_to_install) do
        table.insert(result, entry)
    end
    return result
end


-- AddSymlinkToContainer: add a symlink to a container directory
function AddSymlinkToContainer(container_name, dir_tokens, link_target, link_name)
    local container = _get_container(container_name)

    -- Update-only: skip symlinks
    if container.update_only then return end

    AddDirectoryToContainer(container_name, dir_tokens)
    local dir_key = _dir_key(dir_tokens)
    local dir = container.directories[dir_key]

    if not link_name then
        -- Extract filename from link target path
        link_name = path.filename(link_target)
    end

    table.insert(dir.symlinks_to_install, {
        link_path = _dir_key(dir_tokens) .. "/" .. link_name,
        link_target = link_target,
        link_name = link_name,
    })
end


-- FSymlinksInContainerDirectory: get symlinks registered in a container directory
function FSymlinksInContainerDirectory(container_name, dir_tokens)
    local container = _containers[container_name]
    if not container then return {} end

    local dir_key = _dir_key(dir_tokens)
    local dir = container.directories[dir_key]
    if not dir then return {} end

    return dir.symlinks_to_install
end


-- CopyDirectoryToContainer: copy a directory tree into a container
-- flags: alwaysUpdate, isTarget
function CopyDirectoryToContainer(container_name, dir_tokens, source_directory,
        target_dir_name, exclude_patterns, flags)
    local container = _get_container(container_name)

    flags = flags or {}
    local flags_set = {}
    if type(flags) == "table" then
        for _, f in ipairs(flags) do flags_set[f] = true end
    end

    if container.update_only and not flags_set["alwaysUpdate"] then
        return
    end

    target_dir_name = target_dir_name or path.filename(source_directory)

    -- Extend dir_tokens with target directory name
    local full_tokens = {}
    for _, t in ipairs(dir_tokens) do table.insert(full_tokens, t) end
    table.insert(full_tokens, target_dir_name)

    AddDirectoryToContainer(container_name, full_tokens)
    local dir_key = _dir_key(full_tokens)
    local dir = container.directories[dir_key]

    table.insert(dir.directories_to_install, {
        source = source_directory,
        target_dir = dir_key,
        exclude_patterns = exclude_patterns or {},
    })
end


-- AddHeaderDirectoryToContainer: add header directory to container
function AddHeaderDirectoryToContainer(container_name, dir_tokens, dir_name, flags)
    import("core.project.config")
    local haiku_top = config.get("haiku_top") or os.projectdir()
    local container = _get_container(container_name)

    local sys_tokens = container.system_dir_tokens
    local dest_tokens = {}
    for _, t in ipairs(sys_tokens) do table.insert(dest_tokens, t) end
    table.insert(dest_tokens, "develop")
    table.insert(dest_tokens, "headers")

    local source_dir = path.join(haiku_top, "headers", table.unpack(dir_tokens))

    CopyDirectoryToContainer(container_name, dest_tokens, source_dir,
        dir_name, {"-x", "*~"}, flags)
end


-- AddWifiFirmwareToContainer: add wifi firmware to container
function AddWifiFirmwareToContainer(container_name, driver, package, archive, extract)
    import("core.project.config")
    local haiku_top = config.get("haiku_top") or os.projectdir()
    local container = _get_container(container_name)

    local firmware_archive = path.join(haiku_top, "data", "system", "data",
        "firmware", driver, archive)

    local sys_tokens = container.system_dir_tokens
    local dir_tokens = {}
    for _, t in ipairs(sys_tokens) do table.insert(dir_tokens, t) end
    table.insert(dir_tokens, "data")
    table.insert(dir_tokens, "firmware")
    table.insert(dir_tokens, driver)

    if extract == "true" or extract == "1" or extract == true then
        ExtractArchiveToContainer(container_name, dir_tokens, firmware_archive,
            nil, package)
    else
        AddFilesToContainer(container_name, dir_tokens, {firmware_archive})
    end
end


-- ExtractArchiveToContainer: extract archive into a container directory
function ExtractArchiveToContainer(container_name, dir_tokens, archive_file,
        flags, extracted_subdir)
    local container = _get_container(container_name)

    flags = flags or {}
    local flags_set = {}
    if type(flags) == "table" then
        for _, f in ipairs(flags) do flags_set[f] = true end
    end

    if container.update_only and not flags_set["alwaysUpdate"] then
        return
    end

    AddDirectoryToContainer(container_name, dir_tokens)
    local dir_key = _dir_key(dir_tokens)
    local dir = container.directories[dir_key]

    table.insert(dir.archive_files, {
        archive = archive_file,
        subdir = extracted_subdir or ".",
    })
end


-- AddDriversToContainer: add drivers with dev symlinks + bin entries
function AddDriversToContainer(container_name, relative_dir_tokens, targets)
    import("rules.BuildFeatureRules")

    local container = _get_container(container_name)
    targets = BuildFeatureRules.FFilterByBuildFeatures(targets or {})

    local sys_tokens = container.system_dir_tokens

    -- Add to bin directory
    local bin_tokens = {}
    for _, t in ipairs(sys_tokens) do table.insert(bin_tokens, t) end
    table.insert(bin_tokens, "add-ons")
    table.insert(bin_tokens, "kernel")
    table.insert(bin_tokens, "drivers")
    table.insert(bin_tokens, "bin")

    AddFilesToContainer(container_name, bin_tokens, targets)

    -- Add dev directory symlinks
    if container.update_only then return end

    local dev_tokens = {}
    for _, t in ipairs(sys_tokens) do table.insert(dev_tokens, t) end
    table.insert(dev_tokens, "add-ons")
    table.insert(dev_tokens, "kernel")
    table.insert(dev_tokens, "drivers")
    table.insert(dev_tokens, "dev")
    for _, rt in ipairs(relative_dir_tokens) do
        table.insert(dev_tokens, rt)
    end

    -- Compute relative symlink prefix (.. for each relative dir token, then .. bin)
    local link_parts = {}
    for _ in ipairs(relative_dir_tokens) do
        table.insert(link_parts, "..")
    end
    table.insert(link_parts, "..")
    table.insert(link_parts, "bin")

    for _, target in ipairs(targets) do
        local name = path.filename(target)
        local link_target = table.concat(link_parts, "/") .. "/" .. name
        AddSymlinkToContainer(container_name, dev_tokens, link_target, name)
    end
end


-- AddNewDriversToContainer: add new-style drivers (no dev symlinks)
function AddNewDriversToContainer(container_name, relative_dir_tokens, targets, flags)
    import("rules.BuildFeatureRules")

    local container = _get_container(container_name)
    targets = BuildFeatureRules.FFilterByBuildFeatures(targets or {})

    local sys_tokens = container.system_dir_tokens
    local dir_tokens = {}
    for _, t in ipairs(sys_tokens) do table.insert(dir_tokens, t) end
    table.insert(dir_tokens, "add-ons")
    table.insert(dir_tokens, "kernel")
    table.insert(dir_tokens, "drivers")
    for _, rt in ipairs(relative_dir_tokens) do
        table.insert(dir_tokens, rt)
    end

    AddFilesToContainer(container_name, dir_tokens, targets, nil, flags)
end


-- AddBootModuleSymlinksToContainer: add symlinks in kernel/boot for boot modules
function AddBootModuleSymlinksToContainer(container_name, targets)
    import("rules.BuildFeatureRules")

    local container = _get_container(container_name)
    if container.update_only then return end

    targets = BuildFeatureRules.FFilterByBuildFeatures(targets or {})

    local sys_tokens = container.system_dir_tokens
    local boot_tokens = {}
    for _, t in ipairs(sys_tokens) do table.insert(boot_tokens, t) end
    table.insert(boot_tokens, "add-ons")
    table.insert(boot_tokens, "kernel")
    table.insert(boot_tokens, "boot")

    for _, target in ipairs(targets) do
        local name = path.filename(target)
        -- Create symlink pointing ../../.. back to the actual install location
        -- The actual install dir needs to be known; default to a relative path
        local link_target = "../../../" .. name
        AddSymlinkToContainer(container_name, boot_tokens, link_target, name)
    end
end


-- AddLibrariesToContainer: add libraries with ABI versioned symlinks
function AddLibrariesToContainer(container_name, directory_tokens, libs)
    for _, lib in ipairs(libs or {}) do
        local lib_name = path.filename(lib)
        -- Check for ABI version (stored externally or via naming convention)
        -- Default: just add the file
        AddFilesToContainer(container_name, directory_tokens, {lib})
    end
end


-- ============================================================================
-- 2. Script generation
-- ============================================================================

-- Helper: write a shell script that initializes variables
function AddVariableToScript(script_path, variable, value)
    import("core.project.depend")

    if not value or (type(value) == "table" and #value == 0) then
        value = ""
    end

    -- Append variable assignment to script
    local line
    if type(value) == "table" then
        line = variable .. '="' .. table.concat(value, " ") .. '"'
    else
        line = variable .. '="' .. tostring(value) .. '"'
    end

    local f = io.open(script_path, "a")
    if f then
        f:write(line .. "\n")
        f:close()
    end
end


-- AddTargetVariableToScript: add a target file path as a variable to script
function AddTargetVariableToScript(script_path, targets, variable)
    if type(targets) == "string" then
        targets = {targets}
    end
    variable = variable or path.filename(targets[1])

    local paths = {}
    for _, t in ipairs(targets) do
        table.insert(paths, t)
    end

    AddVariableToScript(script_path, variable, paths)
end


-- CreateContainerMakeDirectoriesScript: generate script that creates directories
function CreateContainerMakeDirectoriesScript(container_name, script_path)
    import("core.project.config")
    import("core.project.depend")

    local container = _containers[container_name]
    if not container then return end

    depend.on_changed(function()
        os.mkdir(path.directory(script_path))

        local lines = {"#!/bin/sh", "# Auto-generated directory creation script", ""}

        for _, dir_key in ipairs(container.directory_order) do
            local dir = container.directories[dir_key]
            if not dir.dont_create then
                lines[#lines + 1] = string.format(
                    '$mkdir -p "${tPrefix}%s"', dir_key)
            end
        end

        -- Add attribute copying for directories with attribute files
        for _, dir_key in ipairs(container.directory_order) do
            local dir = container.directories[dir_key]
            if #dir.attribute_files > 0 then
                for _, af in ipairs(dir.attribute_files) do
                    lines[#lines + 1] = string.format(
                        '$copyAttrs "${sPrefix}%s" "${tPrefix}%s"', af, dir_key)
                end
            end
        end

        io.writefile(script_path, table.concat(lines, "\n") .. "\n")
    end, {
        files = {},
        dependfile = script_path .. ".d"
    })
end


-- CreateContainerCopyFilesScript: generate script that copies files and creates symlinks
function CreateContainerCopyFilesScript(container_name, script_path)
    import("core.project.depend")

    local container = _containers[container_name]
    if not container then return end

    depend.on_changed(function()
        os.mkdir(path.directory(script_path))

        local lines = {"#!/bin/sh", "# Auto-generated file copy script", ""}

        for _, dir_key in ipairs(container.directory_order) do
            local dir = container.directories[dir_key]

            -- Copy files
            for _, entry in ipairs(dir.targets_to_install) do
                local target_file = entry.target
                local dest_name = entry.name or path.filename(target_file)

                if entry.name_function then
                    -- Dynamic name computation
                    lines[#lines + 1] = string.format(
                        'name=`%s "%s" 2>/dev/null` || exit 1',
                        entry.name_function, target_file)
                    lines[#lines + 1] = string.format(
                        '$cp "${sPrefix}%s" "${tPrefix}%s/${name}"',
                        target_file, dir_key)
                elseif dest_name ~= path.filename(target_file) then
                    -- Renamed copy
                    lines[#lines + 1] = string.format(
                        '$cp "${sPrefix}%s" "${tPrefix}%s/%s"',
                        target_file, dir_key, dest_name)
                else
                    -- Regular batch copy
                    lines[#lines + 1] = string.format(
                        '$cp "${sPrefix}%s" "${tPrefix}%s"',
                        target_file, dir_key)
                end
            end

            -- Create symlinks
            for _, sym in ipairs(dir.symlinks_to_install) do
                lines[#lines + 1] = string.format(
                    '$ln -sfn "%s" "${tPrefix}%s"',
                    sym.link_target, sym.link_path)
            end

            -- Copy directories
            for _, d in ipairs(dir.directories_to_install) do
                local exclude_args = ""
                for _, pat in ipairs(d.exclude_patterns) do
                    exclude_args = exclude_args .. " " .. pat
                end
                lines[#lines + 1] = string.format(
                    '$cp -r%s "${sPrefix}%s/." "${tPrefix}%s"',
                    exclude_args, d.source, d.target_dir)
            end
        end

        io.writefile(script_path, table.concat(lines, "\n") .. "\n")
    end, {
        files = {},
        dependfile = script_path .. ".d"
    })
end


-- CreateContainerExtractFilesScript: generate script that extracts archives
function CreateContainerExtractFilesScript(container_name, script_path)
    import("core.project.depend")

    local container = _containers[container_name]
    if not container then return end

    depend.on_changed(function()
        os.mkdir(path.directory(script_path))

        local lines = {"#!/bin/sh", "# Auto-generated archive extraction script", ""}

        for _, dir_key in ipairs(container.directory_order) do
            local dir = container.directories[dir_key]
            for _, af in ipairs(dir.archive_files) do
                lines[#lines + 1] = string.format(
                    'extractFile "%s" "%s" "%s"',
                    af.archive, dir_key, af.subdir)
            end
        end

        io.writefile(script_path, table.concat(lines, "\n") .. "\n")
    end, {
        files = {},
        dependfile = script_path .. ".d"
    })
end


-- AddPackagesAndRepositoryVariablesToContainerScript:
-- add package lists and repository variables to container script
-- NOTE: downloadDir, package tool paths, and flags are already in init script
-- This function only adds the dynamic package/repository lists
function AddPackagesAndRepositoryVariablesToContainerScript(script_path, container_name)
    import("core.project.config")

    local container = _containers[container_name]
    if not container then return end

    -- Helper function to deduplicate a list while preserving order
    local function deduplicate(list)
        local seen = {}
        local result = {}
        for _, item in ipairs(list) do
            if not seen[item] then
                seen[item] = true
                table.insert(result, item)
            end
        end
        return result
    end

    -- System packages (files to copy to system/packages) - deduplicated
    local system_packages = deduplicate(container.system_packages)
    if #system_packages > 0 then
        AddTargetVariableToScript(script_path, system_packages,
            "systemPackages")
    else
        AddVariableToScript(script_path, "systemPackages", "")
    end

    -- Other packages (optional/disabled packages in other locations) - deduplicated
    local other_packages = deduplicate(container.other_packages)
    if #other_packages > 0 then
        AddTargetVariableToScript(script_path, other_packages,
            "otherPackages")
    else
        AddVariableToScript(script_path, "otherPackages", "")
    end

    -- Repository files for dependency resolution
    import("rules.RepositoryRules")
    local repos = RepositoryRules.GetAllRepositories()
    local repo_files = {}
    for _, repo in pairs(repos) do
        if repo.cache_file then
            table.insert(repo_files, repo.cache_file)
        end
    end
    if #repo_files > 0 then
        AddTargetVariableToScript(script_path, repo_files, "repositories")
    else
        AddVariableToScript(script_path, "repositories", "")
    end
end


-- ============================================================================
-- 3. Haiku Image convenience wrappers
-- ============================================================================

function SetUpdateHaikuImageOnly(flag)
    local container = _get_container(_haiku_image_container)
    container.update_only = flag
end

function IsUpdateHaikuImageOnly()
    local container = _containers[_haiku_image_container]
    return container and container.update_only or false
end

function AddDirectoryToHaikuImage(dir_tokens, attribute_files)
    return AddDirectoryToContainer(_haiku_image_container,
        dir_tokens, attribute_files)
end

function AddFilesToHaikuImage(directory, targets, dest_name, flags)
    AddFilesToContainer(_haiku_image_container, directory,
        targets, dest_name, flags)
end

function FFilesInHaikuImageDirectory(dir_tokens)
    return FFilesInContainerDirectory(_haiku_image_container, dir_tokens)
end

function AddSymlinkToHaikuImage(dir_tokens, link_target, link_name)
    -- Join link target if it's a table
    if type(link_target) == "table" then
        link_target = table.concat(link_target, "/")
    end
    AddSymlinkToContainer(_haiku_image_container, dir_tokens,
        link_target, link_name)
end

function FSymlinksInHaikuImageDirectory(dir_tokens)
    return FSymlinksInContainerDirectory(_haiku_image_container, dir_tokens)
end

function CopyDirectoryToHaikuImage(dir_tokens, source_directory,
        target_dir_name, exclude_patterns, flags)
    CopyDirectoryToContainer(_haiku_image_container, dir_tokens,
        source_directory, target_dir_name, exclude_patterns, flags)
end

function AddSourceDirectoryToHaikuImage(dir_tokens, flags)
    import("core.project.config")
    local haiku_top = config.get("haiku_top") or os.projectdir()
    local source_dir = path.join(haiku_top, table.unpack(dir_tokens))
    CopyDirectoryToHaikuImage({"home", "HaikuSources"}, source_dir,
        nil, nil, flags)
end

function AddHeaderDirectoryToHaikuImage(dir_tokens, dir_name, flags)
    AddHeaderDirectoryToContainer(_haiku_image_container,
        dir_tokens, dir_name, flags)
end

function AddWifiFirmwareToHaikuImage(driver, package, archive, extract)
    AddWifiFirmwareToContainer(_haiku_image_container,
        driver, package, archive, extract)
end

function ExtractArchiveToHaikuImage(dir_tokens, archive_file, flags, extracted_subdir)
    ExtractArchiveToContainer(_haiku_image_container, dir_tokens,
        archive_file, flags, extracted_subdir)
end

function AddDriversToHaikuImage(relative_dir_tokens, targets)
    AddDriversToContainer(_haiku_image_container,
        relative_dir_tokens, targets)
end

function AddNewDriversToHaikuImage(relative_dir_tokens, targets, flags)
    AddNewDriversToContainer(_haiku_image_container,
        relative_dir_tokens, targets, flags)
end

function AddBootModuleSymlinksToHaikuImage(targets)
    AddBootModuleSymlinksToContainer(_haiku_image_container, targets)
end

-- AddPackageFilesToHaikuImage: add package files to the image
-- location: directory tokens (e.g. {"system", "packages"})
-- packages: list of package files
-- flags: optional, e.g. {"nameFromMetaInfo"}
function AddPackageFilesToHaikuImage(location, packages, flags)
    import("rules.BuildFeatureRules")

    packages = BuildFeatureRules.FFilterByBuildFeatures(packages or {})

    local container = _get_container(_haiku_image_container)

    -- Helper to check if package already exists in list
    local function contains(list, item)
        for _, v in ipairs(list) do
            if v == item then return true end
        end
        return false
    end

    -- Track as system or other packages (avoid duplicates)
    if #location >= 2 and location[1] == "system" and location[2] == "packages"
            and not location[3] then
        for _, pkg in ipairs(packages) do
            if not contains(container.system_packages, pkg) then
                table.insert(container.system_packages, pkg)
            end
        end
    else
        for _, pkg in ipairs(packages) do
            if not contains(container.other_packages, pkg) then
                table.insert(container.other_packages, pkg)
            end
        end
    end

    flags = flags or {}
    local flags_set = {}
    for _, f in ipairs(flags) do flags_set[f] = true end

    if flags_set["nameFromMetaInfo"] then
        AddFilesToHaikuImage(location, packages, "packageFileName",
            {"computeName"})
    else
        AddFilesToHaikuImage(location, packages)
    end
end


-- AddOptionalHaikuImagePackages: add optional packages and their dependencies
function AddOptionalHaikuImagePackages(packages)
    for _, package in ipairs(packages or {}) do
        if not _optional_packages_added[package] then
            _optional_packages_added[package] = true
            table.insert(_added_optional_packages, package)

            -- Recursively add dependencies
            local deps = _optional_package_dependencies[package]
            if deps then
                AddOptionalHaikuImagePackages(deps)
            end
        end
    end
end

function SuppressOptionalHaikuImagePackages(packages)
    for _, package in ipairs(packages or {}) do
        _optional_packages_suppressed[package] = true
    end
end

function IsOptionalHaikuImagePackageAdded(package)
    if not _optional_packages_exists[package] then
        _optional_packages_exists[package] = true
        table.insert(_existing_optional_packages, package)
    end

    return _optional_packages_added[package]
        and not _optional_packages_suppressed[package]
end

function OptionalPackageDependencies(package, dependencies)
    _optional_package_dependencies[package] = dependencies
    if _optional_packages_added[package] then
        AddOptionalHaikuImagePackages(dependencies)
    end
end


-- AddHaikuImagePackages: add packages to the image from the repository
function AddHaikuImagePackages(packages, directory)
    import("rules.BuildFeatureRules")
    import("rules.RepositoryRules")

    packages = BuildFeatureRules.FFilterByBuildFeatures(packages or {})
    directory = directory or {"system", "packages"}

    for _, package in ipairs(packages) do
        local resolved = RepositoryRules.IsPackageAvailable(package)
        if not resolved then
            print("AddHaikuImagePackages: package " .. package .. " not available!")
        else
            if not _added_packages[resolved] then
                _added_packages[resolved] = true
                table.insert(_added_packages_list, resolved)

                -- Fetch the package file
                local file = RepositoryRules.FetchPackage(package)

                AddPackageFilesToHaikuImage(directory, {file})
            end
        end
    end
end

function AddHaikuImageSourcePackages(packages)
    import("core.project.config")
    if config.get("haiku_include_sources") then
        local source_packages = {}
        for _, pkg in ipairs(packages or {}) do
            table.insert(source_packages, pkg .. "_source")
        end
        AddHaikuImagePackages(source_packages, {"_sources_"})
    end
end

function AddHaikuImageSystemPackages(packages)
    AddHaikuImagePackages(packages, {"system", "packages"})
end

function AddHaikuImageDisabledPackages(packages)
    AddHaikuImagePackages(packages, {"_packages_"})
end

function IsHaikuImagePackageAdded(package)
    import("rules.RepositoryRules")
    local resolved = RepositoryRules.IsPackageAvailable(package)
    return resolved and _added_packages[resolved] or false
end


-- BuildHaikuImagePackageList: generate sorted list of added package names
function BuildHaikuImagePackageList(target_path)
    import("core.project.depend")
    import("rules.RepositoryRules")

    if not target_path then return end

    depend.on_changed(function()
        os.mkdir(path.directory(target_path))

        local package_names = {}
        for _, pkg_name in ipairs(_added_packages_list) do
            local file = RepositoryRules.FetchPackage(pkg_name, {"nameResolved"})
            if file then
                -- Extract versioned name (without revision)
                local basename = path.basename(file)
                local name = basename:match("^([^-]*)") or basename
                table.insert(package_names, name)
            end
        end

        -- Sort and deduplicate
        table.sort(package_names)
        local unique = {}
        local prev
        for _, n in ipairs(package_names) do
            if n ~= prev then
                table.insert(unique, n)
                prev = n
            end
        end

        io.writefile(target_path, table.concat(unique, "\n") .. "\n")
    end, {
        files = {},  -- No source files to track, but required by depend.on_changed
        dependfile = target_path .. ".d"
    })
end


-- AddEntryToHaikuImageUserGroupFile: add a passwd/group entry
-- file_id: unique identifier for the file (e.g. "passwd", "group")
local _user_group_entries = {}

function AddEntryToHaikuImageUserGroupFile(file_id, entry)
    if not _user_group_entries[file_id] then
        _user_group_entries[file_id] = {}
    end
    table.insert(_user_group_entries[file_id], entry)
end

-- BuildHaikuImageUserGroupFile: write collected entries to file
function BuildHaikuImageUserGroupFile(file_id, target_path)
    import("core.project.depend")

    local entries = _user_group_entries[file_id] or {}

    depend.on_changed(function()
        os.mkdir(path.directory(target_path))
        io.writefile(target_path, table.concat(entries, "\n") .. "\n")
    end, {
        dependfile = target_path .. ".d"
    })

    -- Also add to image
    AddFilesToHaikuImage({"system", "settings", "etc"}, {target_path})
end


-- AddUserToHaikuImage: add a user entry to the image passwd file
function AddUserToHaikuImage(user, uid, gid, home, shell, real_name)
    if not user or not uid or not gid or not home then
        raise("Invalid haiku user specification passed to AddUserToHaikuImage.")
    end

    real_name = real_name or user
    shell = shell or ""
    local entry = string.format("%s:x:%s:%s:%s:%s:%s",
        user, uid, gid, real_name, home, shell)

    AddEntryToHaikuImageUserGroupFile("passwd", entry)
end


-- AddGroupToHaikuImage: add a group entry to the image group file
function AddGroupToHaikuImage(group, gid, members)
    if not group or not gid then
        raise("Invalid haiku group specification passed to AddGroupToHaikuImage.")
    end

    local members_str = ""
    if members and #members > 0 then
        members_str = table.concat(members, ",")
    end
    local entry = string.format("%s:x:%s:%s", group, gid, members_str)

    AddEntryToHaikuImageUserGroupFile("group", entry)
end


function AddLibrariesToHaikuImage(directory, libs)
    AddLibrariesToContainer(_haiku_image_container, directory, libs)
end


function CreateHaikuImageMakeDirectoriesScript(script_path)
    CreateContainerMakeDirectoriesScript(_haiku_image_container, script_path)
end

function CreateHaikuImageCopyFilesScript(script_path)
    CreateContainerCopyFilesScript(_haiku_image_container, script_path)
end

function CreateHaikuImageExtractFilesScript(script_path)
    CreateContainerExtractFilesScript(_haiku_image_container, script_path)
end


-- BuildHaikuImage: build the final Haiku image using build_haiku_image script
function BuildHaikuImage(image_path, scripts, is_image, is_vmware_image)
    import("core.project.config")
    import("core.project.depend")

    local haiku_top = config.get("haiku_top") or os.projectdir()
    local output_dir = config.get("haiku_output_dir")
        or config.get("haiku_output_dir") or path.join(haiku_top, "spawned")
    local main_script = path.join(haiku_top, "build", "scripts", "build_haiku_image")

    local depfiles = {main_script}
    for _, s in ipairs(scripts or {}) do
        -- Scripts may already be absolute paths, don't join again
        local abs = path.is_absolute(s) and s or path.join(output_dir, s)
        table.insert(depfiles, abs)
    end

    depend.on_changed(function()
        os.mkdir(path.directory(image_path))

        local args = {}
        for _, s in ipairs(scripts or {}) do
            -- Scripts may already be absolute paths, don't join again
            local abs = path.is_absolute(s) and s or path.join(output_dir, s)
            table.insert(args, abs)
        end

        local envs = {
            imagePath = image_path,
            isImage = (is_image and "1" or ""),
            isVMwareImage = (is_vmware_image and "1" or ""),
        }

        os.execv(main_script, args, {envs = envs})
    end, {
        files = depfiles,
        dependfile = image_path .. ".d"
    })
end


-- BuildVMWareImage: build VMWare VMDK from raw image
function BuildVMWareImage(vmware_image, plain_image, image_size_mb)
    import("core.project.config")
    import("core.project.depend")

    local vmdkheader = config.get("build_vmdkheader") or "vmdkheader"

    depend.on_changed(function()
        os.mkdir(path.directory(vmware_image))
        if os.isfile(vmware_image) then os.rm(vmware_image) end

        os.vrunv(vmdkheader, {"-h", "64k", "-i" .. image_size_mb .. "M",
            vmware_image})

        -- Append the plain image data
        os.exec(string.format("cat %s >> %s",
            plain_image, vmware_image))
    end, {
        files = {plain_image},
        dependfile = vmware_image .. ".d"
    })
end


-- ============================================================================
-- 4. Net Boot Archive convenience wrappers
-- ============================================================================

function AddDirectoryToNetBootArchive(dir_tokens)
    return AddDirectoryToContainer(_net_boot_archive_container, dir_tokens)
end

function AddFilesToNetBootArchive(directory, targets, dest_name)
    AddFilesToContainer(_net_boot_archive_container, directory,
        targets, dest_name)
end

function AddSymlinkToNetBootArchive(dir_tokens, link_target, link_name)
    AddSymlinkToContainer(_net_boot_archive_container,
        dir_tokens, link_target, link_name)
end

function AddDriversToNetBootArchive(relative_dir_tokens, targets)
    AddDriversToContainer(_net_boot_archive_container,
        relative_dir_tokens, targets)
end

function AddNewDriversToNetBootArchive(relative_dir_tokens, targets)
    AddNewDriversToContainer(_net_boot_archive_container,
        relative_dir_tokens, targets)
end

function AddBootModuleSymlinksToNetBootArchive(targets)
    AddBootModuleSymlinksToContainer(_net_boot_archive_container, targets)
end

function CreateNetBootArchiveMakeDirectoriesScript(script_path)
    CreateContainerMakeDirectoriesScript(_net_boot_archive_container, script_path)
end

function CreateNetBootArchiveCopyFilesScript(script_path)
    CreateContainerCopyFilesScript(_net_boot_archive_container, script_path)
end

-- BuildNetBootArchive: build the net boot archive
function BuildNetBootArchive(archive_path, scripts)
    import("core.project.config")
    import("core.project.depend")

    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")
    local main_script = path.join(haiku_top, "build", "scripts", "build_archive")

    local depfiles = {main_script}
    for _, s in ipairs(scripts or {}) do
        -- Scripts may already be absolute paths, don't join again
        local abs = path.is_absolute(s) and s or path.join(output_dir, s)
        table.insert(depfiles, abs)
    end

    depend.on_changed(function()
        os.mkdir(path.directory(archive_path))

        local args = {archive_path}
        for _, s in ipairs(scripts or {}) do
            -- Scripts may already be absolute paths, don't join again
            local abs = path.is_absolute(s) and s or path.join(output_dir, s)
            table.insert(args, abs)
        end

        os.vrunv(main_script, args)
    end, {
        files = depfiles,
        dependfile = archive_path .. ".d"
    })
end


-- BuildHaikuCD: build the Haiku CD image using build_haiku_image script
-- Similar to BuildHaikuImage but with CD-specific environment variables
function BuildHaikuCD(cd_image_path, boot_file, scripts)
    import("core.project.config")
    import("core.project.depend")

    local haiku_top = config.get("haiku_top") or path.directory(path.directory(os.projectdir()))
    local output_dir = config.get("haiku_output_dir") or path.join(haiku_top, "spawned")
    local main_script = path.join(haiku_top, "build", "scripts", "build_haiku_image")

    local depfiles = {main_script}
    for _, s in ipairs(scripts or {}) do
        local abs = path.is_absolute(s) and s or path.join(output_dir, s)
        table.insert(depfiles, abs)
    end

    depend.on_changed(function()
        os.mkdir(path.directory(cd_image_path))

        local args = {}
        for _, s in ipairs(scripts or {}) do
            local abs = path.is_absolute(s) and s or path.join(output_dir, s)
            table.insert(args, abs)
        end

        local envs = {
            cdImagePath = cd_image_path,
            cdBootFloppy = boot_file or "",
            isCD = "1",
        }

        os.execv(main_script, args, {envs = envs})
    end, {
        files = depfiles,
        dependfile = cd_image_path .. ".d"
    })
end


-- ============================================================================
-- 5. CD/DVD Boot Image rules
-- ============================================================================

-- BuildCDBootImageEFI: build CD image with both legacy and EFI boot
function BuildCDBootImageEFI(image_path, boot_floppy, boot_efi, extra_files)
    import("core.project.config")
    import("core.project.depend")

    local target_arch = config.get("target_arch") or config.get("arch") or "x86_64"
    local nightly = config.get("haiku_nightly_build")
    local version = config.get("haiku_version") or ""

    local volid
    if nightly then
        volid = "haiku-nightly-" .. target_arch
    else
        volid = "haiku-" .. version .. "-" .. target_arch
    end

    local depfiles = {boot_floppy, boot_efi}
    for _, f in ipairs(extra_files or {}) do
        table.insert(depfiles, f)
    end

    depend.on_changed(function()
        os.mkdir(path.directory(image_path))
        if os.isfile(image_path) then os.rm(image_path) end

        local args = {
            "-as", "mkisofs",
            "-b", boot_floppy,
            "-no-emul-boot",
            "-eltorito-alt-boot",
            "-no-emul-boot",
            "-e", boot_efi,
            "-r", "-J",
            "-V", volid,
            "-o", image_path,
            boot_floppy,
            boot_efi,
        }
        for _, f in ipairs(extra_files or {}) do
            table.insert(args, f)
        end

        os.vrunv("xorriso", args)
    end, {
        files = depfiles,
        dependfile = image_path .. ".d"
    })
end


-- BuildCDBootImageEFIOnly: build EFI-only CD image (no legacy floppy boot)
function BuildCDBootImageEFIOnly(image_path, boot_efi, extra_files)
    import("core.project.config")
    import("core.project.depend")

    local target_arch = config.get("target_arch") or config.get("arch") or "x86_64"
    local nightly = config.get("haiku_nightly_build")
    local version = config.get("haiku_version") or ""

    local volid
    if nightly then
        volid = "haiku-nightly-" .. target_arch
    else
        volid = "haiku-" .. version .. "-" .. target_arch
    end

    local depfiles = {boot_efi}
    for _, f in ipairs(extra_files or {}) do
        table.insert(depfiles, f)
    end

    depend.on_changed(function()
        os.mkdir(path.directory(image_path))
        if os.isfile(image_path) then os.rm(image_path) end

        local args = {
            "-as", "mkisofs",
            "-no-emul-boot",
            "-e", boot_efi,
            "-r", "-J",
            "-V", volid,
            "-o", image_path,
            boot_efi,
        }
        for _, f in ipairs(extra_files or {}) do
            table.insert(args, f)
        end

        os.vrunv("xorriso", args)
    end, {
        files = depfiles,
        dependfile = image_path .. ".d"
    })
end


-- BuildCDBootImageBIOS: build BIOS-bootable CD image (El Torito)
function BuildCDBootImageBIOS(image_path, boot_mbr, bios_loader, extra_files)
    import("core.project.config")
    import("core.project.depend")

    local target_arch = config.get("target_arch") or config.get("arch") or "x86_64"
    local nightly = config.get("haiku_nightly_build")
    local version = config.get("haiku_version") or ""
    local output_dir = config.get("haiku_output_dir")
        or path.join(config.get("haiku_top") or os.projectdir(), "generated")

    local volid
    if nightly then
        volid = "haiku-nightly-" .. target_arch
    else
        volid = "haiku-" .. version .. "-" .. target_arch
    end

    local depfiles = {boot_mbr, bios_loader}
    for _, f in ipairs(extra_files or {}) do
        table.insert(depfiles, f)
    end

    depend.on_changed(function()
        os.mkdir(path.directory(image_path))
        if os.isfile(image_path) then os.rm(image_path) end

        local cd_dir = path.join(output_dir, "cd")
        local boot_dir = path.join(cd_dir, "boot")
        os.mkdir(boot_dir)

        -- Copy BIOS loader
        os.cp(bios_loader, path.join(boot_dir, "haiku_loader"))

        -- Create ISO with El Torito boot support
        local args = {
            "-R", "-V", volid,
            "-b", "boot/haiku_loader",
            "-no-emul-boot",
            "-boot-load-size", "4",
            "-boot-info-table",
            "-o", image_path,
            cd_dir,
        }
        for _, f in ipairs(extra_files or {}) do
            table.insert(args, f)
        end

        os.vrunv("mkisofs", args)

        -- Cleanup
        os.rm(cd_dir)
    end, {
        files = depfiles,
        dependfile = image_path .. ".d"
    })
end


-- ============================================================================
-- 6. EFI System Partition rules
-- ============================================================================

-- BuildEfiSystemPartition: create an EFI system partition FAT image
function BuildEfiSystemPartition(image_path, efi_loader)
    import("core.project.config")
    import("core.project.depend")

    local haiku_top = config.get("haiku_top") or os.projectdir()
    local target_arch = config.get("target_arch") or config.get("arch") or "x86_64"

    -- Determine EFI binary name per architecture
    local efi_names = {
        x86 = "BOOTIA32.EFI",
        x86_64 = "BOOTX64.EFI",
        arm = "BOOTARM.EFI",
        arm64 = "BOOTAA64.EFI",
        riscv32 = "BOOTRISCV32.EFI",
        riscv64 = "BOOTRISCV64.EFI",
    }
    local efi_name = efi_names[target_arch]
    if not efi_name then
        raise("Error: Unknown EFI architecture: " .. target_arch)
    end

    local mac_volume_icon = path.join(haiku_top, "data", "artwork", "VolumeIcon.icns")
    local efi_keys_dir = path.join(haiku_top, "data", "boot", "efi", "keys")
    local fat_shell = config.get("build_fat_shell") or "fat_shell"
    local compat_lib_dir = config.get("host_add_build_compatibility_lib_dir") or ""

    local depfiles = {efi_loader, mac_volume_icon}

    depend.on_changed(function()
        os.mkdir(path.directory(image_path))
        if os.isfile(image_path) then os.rm(image_path) end

        local envs = nil
        if compat_lib_dir ~= "" then
            envs = {LD_LIBRARY_PATH = compat_lib_dir}
        end

        -- Create empty FAT image (2880 KB floppy size)
        os.exec(string.format("dd if=/dev/zero of=%s bs=1024 count=2880",
            image_path))

        -- Initialize FAT filesystem
        os.execv(fat_shell, {"--initialize", image_path, "Haiku ESP"},
            {envs = envs})

        -- Create directory structure
        local cmds = {
            "mkdir myfs/EFI",
            "mkdir myfs/KEYS",
            "mkdir myfs/EFI/BOOT",
            string.format("cp :%s myfs/EFI/BOOT/%s", efi_loader, efi_name),
            string.format("cp :%s myfs/.VolumeIcon.icns", mac_volume_icon),
        }

        -- Add EFI keys
        local readme = path.join(efi_keys_dir, "README.md")
        if os.isfile(readme) then
            table.insert(cmds, string.format("cp :%s myfs/KEYS/README.md", readme))
        end
        for _, ext in ipairs({"auth", "cer", "crt"}) do
            local key_file = path.join(efi_keys_dir, "DB." .. ext)
            if os.isfile(key_file) then
                table.insert(cmds, string.format("cp :%s myfs/KEYS/DB.%s",
                    key_file, ext))
            end
        end

        for _, cmd in ipairs(cmds) do
            os.execv("sh", {"-c",
                string.format('echo "%s" | %s %s', cmd, fat_shell, image_path)},
                {envs = envs})
        end
    end, {
        files = depfiles,
        dependfile = image_path .. ".d"
    })
end


-- ============================================================================
-- 7. Accessors
-- ============================================================================

function GetContainer(name)
    return _containers[name]
end

function GetHaikuImageContainer()
    return _get_container(_haiku_image_container)
end

function GetNetBootArchiveContainer()
    return _get_container(_net_boot_archive_container)
end

function SetContainerAttribute(container_name, key, value)
    local container = _get_container(container_name)
    container[key] = value
end

function GetAddedPackages()
    return _added_packages_list
end

function GetAddedOptionalPackages()
    return _added_optional_packages
end

function GetExistingOptionalPackages()
    return _existing_optional_packages
end
