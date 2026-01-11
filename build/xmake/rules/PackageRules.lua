--[[
    Haiku OS Build System - Package Build Rules

    xmake equivalent of build/jam/PackageRules + Container parts from ImageRules

    This module handles:
    1. Container abstraction - base for both packages and images
    2. HPKG package building
    3. Package info preprocessing
    4. Script generation for package creation

    Haiku uses HPKG (Haiku Package) format - a custom package format with:
    - Compressed file system hierarchy
    - Package metadata (.PackageInfo)
    - Dependency resolution
    - Activation on boot

    Rules provided:
    - HaikuPackage: Define a new package
    - BuildHaikuPackage: Build the package
    - PreprocessPackageInfo: Preprocess .PackageInfo template
    - Add*ToPackage: Add files/dirs/symlinks to package

    Container Rules (shared with ImageRules):
    - AddDirectoryToContainer: Add directory to container
    - AddFilesToContainer: Add files to container
    - AddSymlinkToContainer: Add symlink to container
    - CopyDirectoryToContainer: Copy directory tree
    - AddDriversToContainer: Add drivers with symlinks
    - AddLibrariesToContainer: Add libraries with ABI version links
]]

-- ============================================================================
-- Configuration
-- ============================================================================

-- Default package compression level (1-9, 9 = max compression)
local HAIKU_PACKAGE_COMPRESSION_LEVEL = 9

-- Package info search directories
local HAIKU_PACKAGE_INFOS_DIR = "build/jam/packages"

-- ============================================================================
-- State Management
-- ============================================================================

-- Currently being built package (for Add*ToPackage context)
local _current_package = nil

-- Package registry
local _packages = {}

-- Container registry (packages and images share this)
local _containers = {}

-- ============================================================================
-- Helper Functions
-- ============================================================================

-- Get Haiku source tree root
local function get_haiku_top()
    return os.projectdir()
end

-- Get packaging architecture
local function get_packaging_arch()
    return get_config("arch") or "x86_64"
end

-- Get output directory
local function get_output_dir()
    return path.join(os.projectdir(), "generated")
end

-- Get packages directory
local function get_packages_dir()
    local arch = get_packaging_arch()
    return path.join(get_output_dir(), "packages", arch)
end

-- Get packages build directory (temp files during build)
local function get_packages_build_dir()
    local arch = get_packaging_arch()
    return path.join(get_output_dir(), "build_packages", arch)
end

-- Generate container grist (unique identifier)
local function get_container_grist(container_name)
    if _containers[container_name] then
        return _containers[container_name].grist
    end
    return "container_" .. container_name:gsub("[^%w]", "_")
end

-- Generate package grist
local function get_package_grist(package_name)
    local base_grist = package_name:match("<(.-)>") or ""
    local clean_name = package_name:gsub("<.->", "")
    if base_grist ~= "" then
        return "hpkg_" .. base_grist .. "-" .. clean_name
    end
    return "hpkg_-" .. clean_name
end

-- ============================================================================
-- Container Data Structures
-- ============================================================================

-- Create or get container
local function get_or_create_container(name)
    if not _containers[name] then
        _containers[name] = {
            name = name,
            grist = get_container_grist(name),
            directories = {},           -- Directories to create
            files = {},                 -- Files to install
            symlinks = {},              -- Symlinks to create
            directory_copies = {},      -- Directories to copy
            archive_extracts = {},      -- Archives to extract
            install_targets = {},       -- Tracking installed targets
            update_only = false,        -- Only update, don't create
            strip_executables = false,  -- Strip binaries
            system_dir_tokens = {},     -- System directory path tokens
            always_create_dirs = false, -- Always create directories
        }
    end
    return _containers[name]
end

-- ============================================================================
-- Container Rules
-- ============================================================================

--[[
    AddDirectoryToContainer <container> : <directoryTokens> : <attributeFiles>

    Adds a directory to be created in the container.
]]
function AddDirectoryToContainer(container_name, directory_tokens, attribute_files)
    local container = get_or_create_container(container_name)

    -- Build directory path from tokens
    local dir_path
    if type(directory_tokens) == "table" then
        dir_path = path.join(table.unpack(directory_tokens))
    else
        dir_path = directory_tokens
    end

    -- Check if directory already registered
    if not container.directories[dir_path] then
        container.directories[dir_path] = {
            tokens = directory_tokens,
            attributes = attribute_files or {},
            dont_create = false,
        }

        -- Mark parent directory as not to be explicitly created
        local parent = path.directory(dir_path)
        if parent and parent ~= "" and parent ~= "." then
            if container.directories[parent] then
                container.directories[parent].dont_create = true
            end
        end
    end

    -- Add attribute files if specified
    if attribute_files then
        local dir_entry = container.directories[dir_path]
        if type(attribute_files) == "table" then
            for _, attr_file in ipairs(attribute_files) do
                table.insert(dir_entry.attributes, attr_file)
            end
        else
            table.insert(dir_entry.attributes, attribute_files)
        end
    end

    return dir_path
end

--[[
    AddFilesToContainer <container> : <directoryTokens> : <targets> : <destName> : <flags>

    Adds files to the container.
    Flags: computeName, alwaysUpdate
]]
function AddFilesToContainer(container_name, directory_tokens, targets, dest_name, flags)
    local container = get_or_create_container(container_name)

    if not targets then return end
    if type(targets) ~= "table" then targets = {targets} end

    flags = flags or {}
    if type(flags) == "string" then flags = {flags} end

    -- Ensure directory exists
    local dir_path = AddDirectoryToContainer(container_name, directory_tokens)

    -- Process each target
    for _, target in ipairs(targets) do
        local install_name
        local name_function = nil

        if dest_name then
            if flags.computeName then
                name_function = dest_name
                install_name = dest_name .. "/" .. path.filename(target)
            else
                install_name = dest_name
            end
        else
            install_name = path.filename(target)
        end

        local file_entry = {
            target = target,
            directory = dir_path,
            install_name = install_name,
            name_function = name_function,
            always_update = flags.alwaysUpdate or false,
        }

        table.insert(container.files, file_entry)

        -- Track where target is installed
        if not container.install_targets[target] then
            container.install_targets[target] = {}
        end
        table.insert(container.install_targets[target], file_entry)
    end
end

--[[
    AddSymlinkToContainer <container> : <directoryTokens> : <linkTarget> : <linkName>

    Adds a symbolic link to the container.
]]
function AddSymlinkToContainer(container_name, directory_tokens, link_target, link_name)
    local container = get_or_create_container(container_name)

    -- Skip if update only
    if container.update_only then return end

    -- Ensure directory exists
    local dir_path = AddDirectoryToContainer(container_name, directory_tokens)

    -- Determine link name from target if not specified
    if not link_name then
        link_name = path.filename(link_target)
    end

    local symlink_entry = {
        directory = dir_path,
        target = link_target,
        name = link_name,
    }

    table.insert(container.symlinks, symlink_entry)
end

--[[
    CopyDirectoryToContainer <container> : <directoryTokens> : <sourceDirectory>
        : <targetDirectoryName> : <excludePatterns> : <flags>

    Copies a directory tree into the container.
    Flags: alwaysUpdate, isTarget
]]
function CopyDirectoryToContainer(container_name, directory_tokens, source_directory,
                                   target_directory_name, exclude_patterns, flags)
    local container = get_or_create_container(container_name)

    flags = flags or {}
    if type(flags) == "string" then flags = {flags} end

    -- Skip if update only and not always update
    if container.update_only and not flags.alwaysUpdate then return end

    -- Determine target directory name
    if not target_directory_name then
        target_directory_name = path.filename(source_directory)
    end

    -- Add the target directory
    local dir_tokens = directory_tokens
    if type(dir_tokens) ~= "table" then dir_tokens = {dir_tokens} end
    table.insert(dir_tokens, target_directory_name)

    local dir_path = AddDirectoryToContainer(container_name, dir_tokens)

    local copy_entry = {
        source = source_directory,
        target_directory = dir_path,
        exclude_patterns = exclude_patterns or {},
        is_target = flags.isTarget or false,
    }

    table.insert(container.directory_copies, copy_entry)
end

--[[
    AddHeaderDirectoryToContainer <container> : <dirTokens> : <dirName> : <flags>

    Adds a header directory from headers/ to develop/headers in the container.
]]
function AddHeaderDirectoryToContainer(container_name, dir_tokens, dir_name, flags)
    local container = get_or_create_container(container_name)
    local system_tokens = container.system_dir_tokens or {}

    local haiku_top = get_haiku_top()
    local source_dir = path.join(haiku_top, "headers", table.unpack(dir_tokens))

    local target_tokens = {}
    for _, t in ipairs(system_tokens) do table.insert(target_tokens, t) end
    table.insert(target_tokens, "develop")
    table.insert(target_tokens, "headers")

    CopyDirectoryToContainer(container_name, target_tokens, source_dir,
                             dir_name, {"-x", "*~"}, flags)
end

--[[
    AddDriversToContainer <container> : <relativeDirectoryTokens> : <targets>

    Adds drivers to the container with proper symlinks structure.
    Drivers are installed to add-ons/kernel/drivers/bin/ with symlinks
    in add-ons/kernel/drivers/dev/<relative>/.
]]
function AddDriversToContainer(container_name, relative_dir_tokens, targets)
    local container = get_or_create_container(container_name)
    local system_tokens = container.system_dir_tokens or {}

    if not targets then return end
    if type(targets) ~= "table" then targets = {targets} end

    -- Build directory tokens for dev directory
    local dev_tokens = {}
    for _, t in ipairs(system_tokens) do table.insert(dev_tokens, t) end
    table.insert(dev_tokens, "add-ons")
    table.insert(dev_tokens, "kernel")
    table.insert(dev_tokens, "drivers")
    table.insert(dev_tokens, "dev")
    if type(relative_dir_tokens) == "table" then
        for _, t in ipairs(relative_dir_tokens) do table.insert(dev_tokens, t) end
    else
        table.insert(dev_tokens, relative_dir_tokens)
    end

    -- Build directory tokens for bin directory
    local bin_tokens = {}
    for _, t in ipairs(system_tokens) do table.insert(bin_tokens, t) end
    table.insert(bin_tokens, "add-ons")
    table.insert(bin_tokens, "kernel")
    table.insert(bin_tokens, "drivers")
    table.insert(bin_tokens, "bin")

    -- Add drivers to bin directory
    AddFilesToContainer(container_name, bin_tokens, targets)

    -- Skip symlinks if update only
    if container.update_only then return end

    -- Calculate relative symlink prefix
    local link_prefix_parts = {}
    if type(relative_dir_tokens) == "table" then
        for _ in ipairs(relative_dir_tokens) do
            table.insert(link_prefix_parts, "..")
        end
    else
        table.insert(link_prefix_parts, "..")
    end
    table.insert(link_prefix_parts, "..")
    table.insert(link_prefix_parts, "bin")

    -- Add symlinks in dev directory
    for _, target in ipairs(targets) do
        local name = path.filename(target)
        local link_target = path.join(table.unpack(link_prefix_parts), name)
        AddSymlinkToContainer(container_name, dev_tokens, link_target, name)
    end
end

--[[
    AddNewDriversToContainer <container> : <relativeDirectoryTokens> : <targets> : <flags>

    Adds new-style drivers (without bin/dev separation) to the container.
]]
function AddNewDriversToContainer(container_name, relative_dir_tokens, targets, flags)
    local container = get_or_create_container(container_name)
    local system_tokens = container.system_dir_tokens or {}

    -- Build directory tokens
    local dir_tokens = {}
    for _, t in ipairs(system_tokens) do table.insert(dir_tokens, t) end
    table.insert(dir_tokens, "add-ons")
    table.insert(dir_tokens, "kernel")
    table.insert(dir_tokens, "drivers")
    if type(relative_dir_tokens) == "table" then
        for _, t in ipairs(relative_dir_tokens) do table.insert(dir_tokens, t) end
    else
        table.insert(dir_tokens, relative_dir_tokens)
    end

    AddFilesToContainer(container_name, dir_tokens, targets, nil, flags)
end

--[[
    AddBootModuleSymlinksToContainer <container> : <targets>

    Adds symlinks in add-ons/kernel/boot/ pointing to previously installed targets.
]]
function AddBootModuleSymlinksToContainer(container_name, targets)
    local container = get_or_create_container(container_name)

    if container.update_only then return end

    local system_tokens = container.system_dir_tokens or {}

    if not targets then return end
    if type(targets) ~= "table" then targets = {targets} end

    for _, target in ipairs(targets) do
        -- Find where target was installed
        local install_entries = container.install_targets[target]
        if install_entries and #install_entries > 0 then
            local first_install = install_entries[1]
            local install_dir = first_install.directory

            -- Calculate relative path from boot directory
            local name = path.filename(target)
            local link_target = path.join("../../..", install_dir, name)

            -- Add symlink in boot directory
            local boot_tokens = {}
            for _, t in ipairs(system_tokens) do table.insert(boot_tokens, t) end
            table.insert(boot_tokens, "add-ons")
            table.insert(boot_tokens, "kernel")
            table.insert(boot_tokens, "boot")

            AddSymlinkToContainer(container_name, boot_tokens, link_target, name)
        end
    end
end

--[[
    AddLibrariesToContainer <container> : <directory> : <libs>

    Adds libraries with appropriate ABI version symlinks.
]]
function AddLibrariesToContainer(container_name, directory, libs)
    if not libs then return end
    if type(libs) ~= "table" then libs = {libs} end

    for _, lib in ipairs(libs) do
        -- Check if library has ABI version
        local abi_version = nil  -- Would be set from target metadata

        if abi_version then
            local versioned_name = path.filename(lib) .. "." .. abi_version
            AddFilesToContainer(container_name, directory, {lib}, versioned_name)
            AddSymlinkToContainer(container_name, directory, versioned_name, path.filename(lib))
        else
            AddFilesToContainer(container_name, directory, {lib})
        end
    end
end

--[[
    ExtractArchiveToContainer <container> : <directoryTokens> : <archiveFile>
        : <flags> : <extractedSubDir>

    Extracts an archive into the container.
]]
function ExtractArchiveToContainer(container_name, directory_tokens, archive_file,
                                    flags, extracted_sub_dir)
    local container = get_or_create_container(container_name)

    flags = flags or {}
    if type(flags) == "string" then flags = {flags} end

    if container.update_only and not flags.alwaysUpdate then return end

    local dir_path = AddDirectoryToContainer(container_name, directory_tokens)

    local extract_entry = {
        archive = archive_file,
        target_directory = dir_path,
        sub_directory = extracted_sub_dir or ".",
    }

    table.insert(container.archive_extracts, extract_entry)
end

-- ============================================================================
-- Package-Specific Rules
-- ============================================================================

--[[
    HaikuPackage <package>

    Defines a new Haiku package to be built.
]]
function HaikuPackage(package_name)
    local grist = get_package_grist(package_name)

    local package = get_or_create_container(package_name)
    package.is_package = true
    package.grist = grist
    package.compression_level = HAIKU_PACKAGE_COMPRESSION_LEVEL
    package.always_create_dirs = true

    _packages[package_name] = package
    _current_package = package_name

    return package
end

--[[
    PreprocessPackageInfo <source> : <directory> : <architecture> : <secondaryArchitecture>

    Preprocesses a .PackageInfo template file, substituting:
    - %HAIKU_PACKAGING_ARCH% -> architecture
    - %HAIKU_VERSION% -> version with revision
    - %HAIKU_SECONDARY_PACKAGING_ARCH% -> secondary arch
    And optionally filtering through C preprocessor.
]]
function PreprocessPackageInfo(source, directory, architecture, secondary_architecture)
    architecture = architecture or get_packaging_arch()

    local haiku_top = get_haiku_top()

    -- Search paths for package info
    local search_paths = {
        path.join(haiku_top, HAIKU_PACKAGE_INFOS_DIR, architecture),
        path.join(haiku_top, HAIKU_PACKAGE_INFOS_DIR, "any"),
        path.join(haiku_top, HAIKU_PACKAGE_INFOS_DIR, "generic"),
    }

    -- Find source file
    local source_path = nil
    for _, search_dir in ipairs(search_paths) do
        local candidate = path.join(search_dir, source)
        if os.isfile(candidate) then
            source_path = candidate
            break
        end
    end

    if not source_path then
        print("Warning: Package info not found: " .. source)
        return nil
    end

    -- Generate target name
    local grist = "package-info"
    if secondary_architecture then
        grist = grist .. secondary_architecture
    end
    local target_name = path.basename(source) .. "-package-info"
    local target_path = path.join(directory, target_name)

    return {
        source = source_path,
        target = target_path,
        architecture = architecture,
        secondary_architecture = secondary_architecture,
        grist = grist,
    }
end

--[[
    BuildHaikuPackage <package> : <packageInfo>

    Main rule to build a Haiku package.
    Creates scripts and calls build_haiku_package script.
]]
function BuildHaikuPackage(package_name, package_info)
    local package = _packages[package_name]
    if not package then
        print("Error: Package not defined: " .. package_name)
        return nil
    end

    local architecture = get_packaging_arch()
    local haiku_top = get_haiku_top()

    -- Locate package output
    local packages_dir = get_packages_dir()
    local package_path = path.join(packages_dir, package_name)

    -- Temp directory for build
    local temp_dir = path.join(get_packages_build_dir(), package.grist)
    local script_dir = path.join(temp_dir, "scripts")

    -- Preprocess package info
    local processed_info = PreprocessPackageInfo(package_info, temp_dir, architecture)

    -- Script paths
    local init_vars_script = path.join(script_dir, "haiku.package-init-vars")
    local make_dirs_script = path.join(script_dir, "haiku.package-make-dirs")
    local copy_files_script = path.join(script_dir, "haiku.package-copy-files")
    local extract_files_script = path.join(script_dir, "haiku.package-extract-files")

    return {
        package = package_name,
        package_path = package_path,
        temp_dir = temp_dir,
        package_info = processed_info,
        scripts = {
            init_vars = init_vars_script,
            make_dirs = make_dirs_script,
            copy_files = copy_files_script,
            extract_files = extract_files_script,
        },
        main_script = path.join(haiku_top, "build", "scripts", "build_haiku_package"),
    }
end

-- ============================================================================
-- Add*ToPackage Wrapper Rules
-- ============================================================================

-- Check if current package should be rebuilt
local function dont_rebuild_current_package()
    if not _current_package then return false end
    local package = _packages[_current_package]
    return package and package.dont_rebuild
end

--[[
    AddDirectoryToPackage <directoryTokens> : <attributeFiles>
]]
function AddDirectoryToPackage(directory_tokens, attribute_files)
    if dont_rebuild_current_package() then return nil end
    return AddDirectoryToContainer(_current_package, directory_tokens, attribute_files)
end

--[[
    AddFilesToPackage <directory> : <targets> : <destName>
]]
function AddFilesToPackage(directory, targets, dest_name)
    if dont_rebuild_current_package() then return end
    AddFilesToContainer(_current_package, directory, targets, dest_name)
end

--[[
    AddSymlinkToPackage <directoryTokens> : <linkTarget> : <linkName>
]]
function AddSymlinkToPackage(directory_tokens, link_target, link_name)
    if dont_rebuild_current_package() then return end
    -- Join link target if it's a table
    if type(link_target) == "table" then
        link_target = path.join(table.unpack(link_target))
    end
    AddSymlinkToContainer(_current_package, directory_tokens, link_target, link_name)
end

--[[
    CopyDirectoryToPackage <directoryTokens> : <sourceDirectory>
        : <targetDirectoryName> : <excludePatterns> : <flags>
]]
function CopyDirectoryToPackage(directory_tokens, source_directory, target_directory_name,
                                 exclude_patterns, flags)
    if dont_rebuild_current_package() then return end
    CopyDirectoryToContainer(_current_package, directory_tokens, source_directory,
                             target_directory_name, exclude_patterns, flags)
end

--[[
    AddHeaderDirectoryToPackage <dirTokens> : <dirName> : <flags>
]]
function AddHeaderDirectoryToPackage(dir_tokens, dir_name, flags)
    if dont_rebuild_current_package() then return end
    AddHeaderDirectoryToContainer(_current_package, dir_tokens, dir_name, flags)
end

--[[
    AddWifiFirmwareToPackage <driver> : <subDirToExtract> : <archive> : <extract>
]]
function AddWifiFirmwareToPackage(driver, sub_dir_to_extract, archive, extract)
    if dont_rebuild_current_package() then return end

    local package = _packages[_current_package]
    if not package then return end

    local haiku_top = get_haiku_top()
    local firmware_archive = path.join(haiku_top, "data", "system", "data",
                                        "firmware", driver, archive)

    local system_tokens = package.system_dir_tokens or {}
    local dir_tokens = {}
    for _, t in ipairs(system_tokens) do table.insert(dir_tokens, t) end
    table.insert(dir_tokens, "data")
    table.insert(dir_tokens, "firmware")
    table.insert(dir_tokens, driver)

    if extract == true or extract == "1" or extract == 1 then
        ExtractArchiveToContainer(_current_package, dir_tokens, firmware_archive,
                                   nil, sub_dir_to_extract)
    else
        AddFilesToContainer(_current_package, dir_tokens, {firmware_archive})
    end
end

--[[
    AddDriversToPackage <relativeDirectoryTokens> : <targets>
]]
function AddDriversToPackage(relative_dir_tokens, targets)
    if dont_rebuild_current_package() then return end
    AddDriversToContainer(_current_package, relative_dir_tokens, targets)
end

--[[
    AddNewDriversToPackage <relativeDirectoryTokens> : <targets>
]]
function AddNewDriversToPackage(relative_dir_tokens, targets)
    if dont_rebuild_current_package() then return end
    AddNewDriversToContainer(_current_package, relative_dir_tokens, targets)
end

--[[
    AddBootModuleSymlinksToPackage <targets>
]]
function AddBootModuleSymlinksToPackage(targets)
    if dont_rebuild_current_package() then return end
    AddBootModuleSymlinksToContainer(_current_package, targets)
end

--[[
    AddLibrariesToPackage <directory> : <libs>
]]
function AddLibrariesToPackage(directory, libs)
    if dont_rebuild_current_package() then return end
    AddLibrariesToContainer(_current_package, directory, libs)
end

-- ============================================================================
-- Script Generation Helpers
-- ============================================================================

--[[
    AddVariableToScript <script> : <variable> : <value>

    Adds a shell variable definition to a script.
]]
function AddVariableToScript(script_path, variable, value)
    -- This would generate: echo variable="value" >> script
    return string.format('echo %s=\\"%s\\" >> %s', variable, value or "", script_path)
end

--[[
    InitScript <script>

    Initializes a script file (creates empty file).
]]
function InitScript(script_path)
    os.mkdir(path.directory(script_path))
    local f = io.open(script_path, "w")
    if f then f:close() end
end

-- ============================================================================
-- Container Script Generation
-- ============================================================================

--[[
    CreateContainerMakeDirectoriesScript <container> : <script>

    Creates script that makes all directories in the container.
]]
function CreateContainerMakeDirectoriesScript(container_name, script_path)
    local container = _containers[container_name]
    if not container then return end

    InitScript(script_path)

    local lines = {}
    for dir_path, dir_entry in pairs(container.directories) do
        if not dir_entry.dont_create then
            table.insert(lines, string.format('$mkdir -p "${tPrefix}%s"', dir_path))
        end
    end

    return lines
end

--[[
    CreateContainerCopyFilesScript <container> : <script>

    Creates script that copies all files to the container.
]]
function CreateContainerCopyFilesScript(container_name, script_path)
    local container = _containers[container_name]
    if not container then return end

    InitScript(script_path)

    local lines = {}

    -- Copy files
    for _, file_entry in ipairs(container.files) do
        if file_entry.name_function then
            table.insert(lines, string.format(
                'name=`%s "%s" 2> /dev/null` || exit 1',
                file_entry.name_function, file_entry.target))
            table.insert(lines, string.format(
                '$cp "${sPrefix}%s" "${tPrefix}%s/${name}"',
                file_entry.target, file_entry.directory))
        elseif file_entry.install_name ~= path.filename(file_entry.target) then
            table.insert(lines, string.format(
                '$cp "${sPrefix}%s" "${tPrefix}%s/%s"',
                file_entry.target, file_entry.directory, file_entry.install_name))
        else
            table.insert(lines, string.format(
                '$cp "${sPrefix}%s" "${tPrefix}%s"',
                file_entry.target, file_entry.directory))
        end
    end

    -- Create symlinks
    for _, symlink_entry in ipairs(container.symlinks) do
        table.insert(lines, string.format(
            '$ln -sfn "%s" "${tPrefix}%s/%s"',
            symlink_entry.target,
            symlink_entry.directory,
            symlink_entry.name))
    end

    -- Copy directories
    for _, copy_entry in ipairs(container.directory_copies) do
        local exclude_args = ""
        if copy_entry.exclude_patterns and #copy_entry.exclude_patterns > 0 then
            exclude_args = table.concat(copy_entry.exclude_patterns, " ")
        end
        table.insert(lines, string.format(
            '$cp -r %s "${sPrefix}%s/." "${tPrefix}%s"',
            exclude_args, copy_entry.source, copy_entry.target_directory))
    end

    return lines
end

--[[
    CreateContainerExtractFilesScript <container> : <script>

    Creates script that extracts archives into the container.
]]
function CreateContainerExtractFilesScript(container_name, script_path)
    local container = _containers[container_name]
    if not container then return end

    InitScript(script_path)

    local lines = {}
    for _, extract_entry in ipairs(container.archive_extracts) do
        table.insert(lines, string.format(
            'extractFile "%s" "%s" "%s"',
            extract_entry.archive,
            extract_entry.target_directory,
            extract_entry.sub_directory))
    end

    return lines
end

-- ============================================================================
-- xmake Rules for Package Building
-- ============================================================================

rule("HaikuPackage")
    on_load(function (target)
        -- Initialize package
        local package_name = target:name()
        HaikuPackage(package_name)

        -- Set output directory
        local packages_dir = get_packages_dir()
        target:set("targetdir", packages_dir)
    end)

    on_build(function (target)
        local package_name = target:name()
        local package_info = target:values("package_info")

        if package_info then
            local build_info = BuildHaikuPackage(package_name, package_info)
            -- Here we would invoke the build scripts
            print("Building package: " .. package_name)
        end
    end)

-- ============================================================================
-- Convenience Functions for Jamfile Translation
-- ============================================================================

-- Create Haiku package target
function haiku_package(name, config)
    target(name)
        add_rules("HaikuPackage")
        if config.package_info then
            set_values("package_info", config.package_info)
        end
        if config.compression_level then
            set_values("compression_level", config.compression_level)
        end
end

-- Set current package context (for Add*ToPackage calls)
function set_current_package(package_name)
    _current_package = package_name
end

-- Get current package context
function get_current_package()
    return _current_package
end

-- Get container data (for ImageRules to use)
function get_container(container_name)
    return _containers[container_name]
end

-- Check if container exists
function container_exists(container_name)
    return _containers[container_name] ~= nil
end

-- Get or create container (exported for ImageRules)
function get_or_create_container(container_name)
    if not _containers[container_name] then
        _containers[container_name] = {
            name = container_name,
            grist = get_container_grist(container_name),
            directories = {},
            files = {},
            symlinks = {},
            directory_copies = {},
            archive_extracts = {},
            install_targets = {},
            update_only = false,
            strip_executables = false,
            system_dir_tokens = {},
            always_create_dirs = false,
        }
    end
    return _containers[container_name]
end
