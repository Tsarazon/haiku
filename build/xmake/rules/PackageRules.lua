--[[
    PackageRules.lua - Haiku HPKG package build rules

    xmake equivalent of build/jam/PackageRules (1:1 migration)

    Rules/functions for building Haiku packages (HPKG format):
    - HaikuPackage(package)                  - Initialize a package target
    - PreprocessPackageInfo(source, dir, arch, secondary_arch)
    - PreprocessPackageOrRepositoryInfo(target, source, arch, secondary_arch, flags)
    - BuildHaikuPackage(package, package_info) - Build the actual HPKG
    - AddDirectoryToPackage(dir_tokens, attr_files)
    - AddFilesToPackage(directory, targets, dest_name)
    - AddSymlinkToPackage(dir_tokens, link_target, link_name)
    - CopyDirectoryToPackage(dir_tokens, source_dir, target_dir, excludes, flags)
    - AddHeaderDirectoryToPackage(dir_tokens, dir_name, flags)
    - AddWifiFirmwareToPackage(driver, subdir, archive, extract)
    - AddDriversToPackage(rel_dir_tokens, targets)
    - AddNewDriversToPackage(rel_dir_tokens, targets)
    - AddBootModuleSymlinksToPackage(targets)
    - AddLibrariesToPackage(directory, libs)

    Default package compression level: 9
]]

-- Currently being built package (for convenience wrappers)
local _current_package = nil

-- Package registry
local _packages = {}

-- Default compression level
local HAIKU_PACKAGE_COMPRESSION_LEVEL = 9

-- FHaikuPackageGrist: compute unique grist string for a package
function FHaikuPackageGrist(package_name, package_grist)
    local grist = package_grist or ""
    return string.format("hpkg_%s-%s", grist, package_name)
end

-- HaikuPackage: initialize a package target
function HaikuPackage(package_name, opts)
    opts = opts or {}

    local grist = FHaikuPackageGrist(package_name, opts.grist)
    local pkg = {
        name = package_name,
        grist = grist,
        update_only = opts.update_only or false,
        dont_rebuild = opts.dont_rebuild or false,
        always_create_dirs = true,
        directories = {},
        files = {},
        symlinks = {},
        copy_dirs = {},
        drivers = {},
        libraries = {},
        boot_module_symlinks = {},
    }

    _packages[package_name] = pkg
    _current_package = pkg
    return pkg
end

-- GetCurrentPackage: get the currently being built package
function GetCurrentPackage()
    return _current_package
end

-- GetPackage: get a package by name
function GetPackage(name)
    return _packages[name]
end

-- PreprocessPackageOrRepositoryInfo: preprocess a package/repository info template
-- Performs placeholder substitutions and optional C preprocessor filtering
function PreprocessPackageOrRepositoryInfo(target_path, source_path, architecture,
    secondary_architecture, use_cpp)
    import("core.project.config")
    import("core.project.depend")

    depend.on_changed(function()
        os.mkdir(path.directory(target_path))

        local content = io.readfile(source_path)

        -- Determine revision
        local haiku_top = config.get("haiku_top") or os.projectdir()
        local revision_file = path.join(
            config.get("build_output_dir") or path.join(haiku_top, "generated"),
            "haiku-revision")
        local revision = "0"
        if os.isfile(revision_file) then
            revision = io.readfile(revision_file):trim()
        end
        revision = revision:gsub("[+-]", "_")
        local haiku_version = config.get("haiku_version") or "r1~beta5"
        local version = haiku_version .. "_" .. revision

        -- Sed replacements
        content = content:gsub("%%HAIKU_PACKAGING_ARCH%%", architecture)
        content = content:gsub("%%HAIKU_VERSION%%", version .. "-1")
        content = content:gsub("%%HAIKU_VERSION_NO_REVISION%%", version)

        if secondary_architecture then
            content = content:gsub("%%HAIKU_SECONDARY_PACKAGING_ARCH%%",
                secondary_architecture)
            content = content:gsub("%%HAIKU_SECONDARY_PACKAGING_ARCH_SUFFIX%%",
                "_" .. secondary_architecture)
        else
            content = content:gsub("%%HAIKU_SECONDARY_PACKAGING_ARCH_SUFFIX%%", "")
        end

        -- Optional C preprocessor pass
        if use_cpp then
            local cc = config.get("host_cc") or "cc"
            local defines = {
                "HAIKU_PACKAGING_ARCH=" .. architecture,
                "HAIKU_PACKAGING_ARCH_" .. architecture,
            }
            local build_type = config.get("haiku_build_type") or "regular"
            table.insert(defines, "HAIKU_" .. build_type:upper() .. "_BUILD")

            if secondary_architecture then
                table.insert(defines,
                    "HAIKU_SECONDARY_PACKAGING_ARCH=" .. secondary_architecture)
                table.insert(defines,
                    "HAIKU_SECONDARY_PACKAGING_ARCH_" .. secondary_architecture:upper())
            end

            -- Write intermediate file
            local tmp = target_path .. ".pre"
            io.writefile(tmp, content)

            local args = {"-E", "-w"}
            for _, d in ipairs(defines) do
                table.insert(args, "-D" .. d)
            end
            table.insert(args, tmp)

            local output = os.iorunv(cc, args)
            if output then
                content = output
            end
            os.rm(tmp)
        end

        io.writefile(target_path, content)
    end, {
        files = {source_path},
        dependfile = target_path .. ".d"
    })

    return target_path
end

-- PreprocessPackageInfo: preprocess a package info file
function PreprocessPackageInfo(source, directory, architecture, secondary_architecture)
    local target_name = path.basename(source) .. "-package-info"
    local target_path = path.join(directory, target_name)

    return PreprocessPackageOrRepositoryInfo(
        target_path, source, architecture, secondary_architecture, true)
end

-- BuildHaikuPackage: build the actual HPKG file
function BuildHaikuPackage(target, package_name, package_info_path)
    import("core.project.config")
    import("core.project.depend")

    local pkg = _packages[package_name]
    if not pkg then
        raise("Package '" .. package_name .. "' not initialized")
    end

    if pkg.dont_rebuild then
        return  -- package already exists, skip
    end

    local haiku_top = config.get("haiku_top") or os.projectdir()
    local main_script = path.join(haiku_top, "build", "scripts", "build_haiku_package")
    local targetfile = target:targetfile()

    depend.on_changed(function()
        os.mkdir(path.directory(targetfile))
        os.vrunv(main_script, {targetfile, package_info_path})
    end, {
        files = {package_info_path, main_script},
        dependfile = target:dependfile(targetfile)
    })
end

-- AddDirectoryToPackage: register a directory in the current package
function AddDirectoryToPackage(dir_tokens, attribute_files)
    local pkg = _current_package
    if not pkg or pkg.dont_rebuild then return end

    table.insert(pkg.directories, {
        tokens = dir_tokens,
        attribute_files = attribute_files
    })
end

-- AddFilesToPackage: register files in the current package
function AddFilesToPackage(directory, targets, dest_name)
    local pkg = _current_package
    if not pkg or pkg.dont_rebuild then return end

    table.insert(pkg.files, {
        directory = directory,
        targets = targets,
        dest_name = dest_name
    })
end

-- AddSymlinkToPackage: register a symlink in the current package
function AddSymlinkToPackage(dir_tokens, link_target, link_name)
    local pkg = _current_package
    if not pkg or pkg.dont_rebuild then return end

    if type(link_target) == "table" then
        link_target = table.concat(link_target, "/")
    end

    table.insert(pkg.symlinks, {
        dir_tokens = dir_tokens,
        link_target = link_target,
        link_name = link_name
    })
end

-- CopyDirectoryToPackage: register a directory copy in the current package
function CopyDirectoryToPackage(dir_tokens, source_directory, target_dir_name,
    exclude_patterns, flags)
    local pkg = _current_package
    if not pkg or pkg.dont_rebuild then return end

    table.insert(pkg.copy_dirs, {
        dir_tokens = dir_tokens,
        source_directory = source_directory,
        target_dir_name = target_dir_name,
        exclude_patterns = exclude_patterns,
        flags = flags
    })
end

-- AddHeaderDirectoryToPackage: add header directory to current package
function AddHeaderDirectoryToPackage(dir_tokens, dir_name, flags)
    CopyDirectoryToPackage(dir_tokens, dir_name, nil, nil, flags)
end

-- AddWifiFirmwareToPackage: add wifi firmware to current package
function AddWifiFirmwareToPackage(driver, subdir_to_extract, archive, extract)
    local pkg = _current_package
    if not pkg or pkg.dont_rebuild then return end

    table.insert(pkg.files, {
        directory = {"data", "firmware", driver},
        targets = {archive},
        extract = extract,
        subdir = subdir_to_extract
    })
end

-- AddDriversToPackage: add drivers to current package
function AddDriversToPackage(rel_dir_tokens, targets)
    local pkg = _current_package
    if not pkg or pkg.dont_rebuild then return end

    table.insert(pkg.drivers, {
        rel_dir_tokens = rel_dir_tokens,
        targets = targets
    })
end

-- AddNewDriversToPackage: add new-style drivers to current package
function AddNewDriversToPackage(rel_dir_tokens, targets)
    AddDriversToPackage(rel_dir_tokens, targets)
end

-- AddBootModuleSymlinksToPackage: add boot module symlinks
function AddBootModuleSymlinksToPackage(targets)
    local pkg = _current_package
    if not pkg or pkg.dont_rebuild then return end

    for _, t in ipairs(targets) do
        table.insert(pkg.boot_module_symlinks, t)
    end
end

-- AddLibrariesToPackage: add libraries to current package
function AddLibrariesToPackage(directory, libs)
    local pkg = _current_package
    if not pkg or pkg.dont_rebuild then return end

    table.insert(pkg.libraries, {
        directory = directory,
        libs = libs
    })
end

-- SetPackageCompressionLevel: set compression level for packages
function SetPackageCompressionLevel(level)
    HAIKU_PACKAGE_COMPRESSION_LEVEL = level
end

-- GetPackageCompressionLevel: get current compression level
function GetPackageCompressionLevel()
    return HAIKU_PACKAGE_COMPRESSION_LEVEL
end
