--[[
    RepositoryRules.lua - Package repository management for Haiku build system

    xmake equivalent of build/jam/RepositoryRules (1:1 migration)

    Functions exported:
    -- Private helpers
    - PackageFamily(baseName)                            - Get family identifier
    - AddRepositoryPackage(repo, arch, baseName, version)
    - AddRepositoryPackages(repo, arch, packages, srcPkgs, dbgPkgs)
    - PackageRepository(repo, arch, anyPkgs, pkgs, srcPkgs, dbgPkgs)

    -- Remote repository
    - RemotePackageRepository(repo, arch, url, anyPkgs, pkgs, srcPkgs, dbgPkgs)
    - GeneratedRepositoryPackageList(target_path, repo)
    - RepositoryConfig(configPath, infoPath, checksumFile)
    - RepositoryCache(cachePath, infoPath, packageFiles)

    -- Bootstrap repository
    - BootstrapPackageRepository(repo, arch, anyPkgs, stage0, stage1, stage2, srcPkgs, dbgPkgs)
    - BuildBootstrapRepositoryConfig(configPath, repo)
    - BuildHaikuPortsSourcePackageDirectory()
    - BuildHaikuPortsRepositoryConfig(treePath)

    -- Public
    - FSplitPackageName(packageName)                     - Split package name into base + suffix
    - IsPackageAvailable(packageName, flags)             - Check if package is available
    - FetchPackage(packageName, flags)                   - Fetch a package file
    - HaikuRepository(repo, infoTemplate, packages)      - Build Haiku repository

    Usage:
        import("rules.RepositoryRules")
        local file = RepositoryRules.FetchPackage("openssl_devel")
]]

-- ============================================================================
-- Module-level state
-- ============================================================================

-- Default bootstrap sources profile
local _bootstrap_sources_profile = "@minimum-raw"

-- All registered repositories: { name = repo_table, ... }
local _repositories = {}

-- Ordered list of repository names
local _repository_names = {}

-- Available packages: set of package base names
local _available_packages = {}

-- Package families: { baseName = { versions = {pkg, ...}, ... } }
local _package_families = {}

-- Package data: { pkg_id = { repository = repo, architecture = arch,
--                             file_name = str, family = str,
--                             cross_devel_packages = {...} } }
local _packages = {}

-- ============================================================================
-- Private helpers
-- ============================================================================

-- PackageFamily: create/get a family identifier for a package base name
function PackageFamily(baseName)
    return "package-family:" .. baseName
end

-- _get_family: get or create family entry
local function _get_family(baseName)
    local key = PackageFamily(baseName)
    if not _package_families[key] then
        _package_families[key] = {
            versions = {}
        }
    end
    return _package_families[key]
end

-- _make_package_id: create a unique package identifier
local function _make_package_id(repo_name, baseName, version, architecture)
    local pkg_str = baseName
    if version and version ~= "" then
        pkg_str = baseName .. "-" .. version
    end
    return string.format("package-in-%s:%s:%s", repo_name, pkg_str, architecture)
end


-- AddRepositoryPackage: register a single package in a repository
-- Returns the package ID or nil if skipped
function AddRepositoryPackage(repo, architecture, baseName, version)
    import("core.project.config")

    local repo_name = repo.name

    local pkg_str = baseName
    if version and version ~= "" then
        pkg_str = baseName .. "-" .. version
    end
    local pkg_id = _make_package_id(repo_name, baseName, version, architecture)

    -- Determine package file name
    local packageFileName = pkg_str .. "-" .. architecture .. ".hpkg"

    -- Get family name via repository's PackageFamily method
    local family_baseName = baseName
    if repo.methods and repo.methods.PackageFamily then
        family_baseName = repo.methods.PackageFamily(repo, baseName)
    end

    -- Check download availability if downloads are disabled
    local no_downloads = config.get("haiku_no_downloads")
    local is_bootstrap = config.get("haiku_is_bootstrap")
    if no_downloads and not is_bootstrap then
        local download_dir = config.get("haiku_download_dir")
            or path.join(config.get("haiku_top") or os.projectdir(), "download")
        if not os.isfile(path.join(download_dir, packageFileName)) then
            return nil
        end
    end

    -- Register as available
    _available_packages[family_baseName] = true

    -- Create package entry
    _packages[pkg_id] = {
        repository = repo,
        architecture = architecture,
        file_name = packageFileName,
        base_name = baseName,
        version = version,
        family = family_baseName,
        cross_devel_packages = {}
    }

    -- Add to family versions
    local family = _get_family(family_baseName)
    table.insert(family.versions, pkg_id)

    -- Add to repository packages
    if not repo.packages then
        repo.packages = {}
    end
    table.insert(repo.packages, pkg_id)

    return pkg_id
end


-- AddRepositoryPackages: register multiple packages in a repository
-- Returns list of package IDs
function AddRepositoryPackages(repo, architecture, packages, sourcePackages, debugInfoPackages)
    sourcePackages = sourcePackages or {}
    debugInfoPackages = debugInfoPackages or {}

    local source_set = {}
    for _, s in ipairs(sourcePackages) do source_set[s] = true end
    local debug_set = {}
    for _, d in ipairs(debugInfoPackages) do debug_set[d] = true end

    local packageTargets = {}

    for _, pkg in ipairs(packages) do
        -- Split "baseName-version" format
        local baseName, version = pkg:match("^([^-]+)-(.+)$")
        if not baseName then
            baseName = pkg
            version = nil
        end

        local pkg_id = AddRepositoryPackage(repo, architecture, baseName, version)
        if pkg_id then
            table.insert(packageTargets, pkg_id)
        end

        -- Source package
        if source_set[baseName] then
            AddRepositoryPackage(repo, "source", baseName .. "_source", version)
        end

        -- Debug info package
        if debug_set[baseName] then
            local dbg_id = AddRepositoryPackage(repo, architecture,
                baseName .. "_debuginfo", version)
            if dbg_id then
                table.insert(packageTargets, dbg_id)
            end
        end
    end

    return packageTargets
end


-- PackageRepository: register packages with a repository (base function)
-- Only processes for the primary packaging architecture
function PackageRepository(repo, architecture, anyPackages, packages,
        sourcePackages, debugInfoPackages)
    import("core.project.config")

    local packaging_archs = config.get("haiku_packaging_archs") or {}
    if type(packaging_archs) == "string" then
        packaging_archs = {packaging_archs}
    end

    if #packaging_archs > 0 and architecture ~= packaging_archs[1] then
        return {}
    end

    -- Register repository
    if not _repositories[repo.name] then
        _repositories[repo.name] = repo
        table.insert(_repository_names, repo.name)
    end

    -- Add "any" architecture packages + architecture-specific packages
    local targets = {}
    if anyPackages and #anyPackages > 0 then
        local any_targets = AddRepositoryPackages(repo, "any", anyPackages,
            sourcePackages, debugInfoPackages)
        for _, t in ipairs(any_targets) do
            table.insert(targets, t)
        end
    end
    if packages and #packages > 0 then
        local arch_targets = AddRepositoryPackages(repo, architecture, packages,
            sourcePackages, debugInfoPackages)
        for _, t in ipairs(arch_targets) do
            table.insert(targets, t)
        end
    end

    return targets
end


-- ============================================================================
-- Remote Repository
-- ============================================================================

-- RemoteRepositoryPackageFamily: family for remote packages (just delegates)
local function _remote_package_family(repo, baseName)
    return baseName
end

-- RemoteRepositoryFetchPackage: download a package from a remote repository
local function _remote_fetch_package(repo, pkg_id)
    import("core.project.config")
    import("core.project.depend")

    local pkg = _packages[pkg_id]
    if not pkg then
        raise("RemoteRepositoryFetchPackage: unknown package " .. tostring(pkg_id))
    end

    local fileName = pkg.file_name
    local baseUrl = repo.url
    local download_dir = config.get("haiku_download_dir")
        or path.join(config.get("haiku_top") or os.projectdir(), "download")

    local downloadedFile = path.join(download_dir, fileName)

    -- Download if not already present
    if not os.isfile(downloadedFile) then
        local checksumFile = repo.packages_checksum_file
        local url
        if checksumFile and os.isfile(checksumFile) then
            local checksum = io.readfile(checksumFile):trim()
            url = string.format("%s/%s/packages/%s", baseUrl, checksum, fileName)
        else
            url = string.format("%s/packages/%s", baseUrl, fileName)
        end

        os.mkdir(download_dir)
        import("rules.FileRules")
        FileRules.DownloadFile(downloadedFile, url)
    end

    return downloadedFile
end


-- RemotePackageRepository: set up a remote package repository
-- repo_name: repository name
-- architecture: target architecture
-- repositoryUrl: base URL of the repository
-- anyPackages, packages, sourcePackages, debugInfoPackages: package lists
function RemotePackageRepository(repo_name, architecture, repositoryUrl,
        anyPackages, packages, sourcePackages, debugInfoPackages)
    import("core.project.config")
    import("core.project.depend")

    local host_sed = config.get("host_extended_regex_sed")
    if not host_sed then
        raise("Variable host_extended_regex_sed not set. Please run configure or "
            .. "specify it manually.")
    end

    -- Create the repository object
    local repo = {
        name = repo_name,
        url = repositoryUrl,
        packages = {},
        methods = {
            PackageFamily = _remote_package_family,
            FetchPackage = _remote_fetch_package
        }
    }

    -- Register packages
    PackageRepository(repo, architecture, anyPackages, packages,
        sourcePackages, debugInfoPackages)

    local haiku_top = config.get("haiku_top") or os.projectdir()
    local repos_dir = config.get("haiku_package_repositories_dir_" .. architecture)
        or path.join(haiku_top, "generated", "objects",
            "haiku", architecture, "packaging", "repositories")

    -- Build package list file
    local packageListFile = path.join(repos_dir, repo_name .. "-packages")
    GeneratedRepositoryPackageList(packageListFile, repo)

    -- Build packages checksum file
    local build_rules_dir = config.get("haiku_build_rules_dir")
        or path.join(haiku_top, "build", "jam")
    local packaging_arch = config.get("haiku_packaging_arch") or architecture
    local repo_def_file = path.join(build_rules_dir, "repositories",
        "HaikuPorts", packaging_arch)
    local packagesChecksumFile = path.join(repos_dir, repo_name .. "-checksum")

    import("rules.FileRules")
    if os.isfile(repo_def_file) then
        FileRules.ChecksumFileSHA256(packagesChecksumFile, repo_def_file)
    end

    repo.packages_checksum_file = packagesChecksumFile

    -- Build repository info
    local repositoryInfo = path.join(repos_dir, repo_name .. "-info")
    local repoInfoTemplate = path.join(haiku_top, "src", "data",
        "repository_infos", "haikuports")
    import("rules.PackageRules")
    if os.isfile(repoInfoTemplate) then
        PackageRules.PreprocessPackageOrRepositoryInfo(
            repositoryInfo, repoInfoTemplate, architecture)
    end

    -- Build repository config
    local repositoryConfig = path.join(repos_dir, repo_name .. "-config")
    RepositoryConfig(repositoryConfig, repositoryInfo, packagesChecksumFile)

    -- Build or download repository cache
    local repositoryFile = path.join(repos_dir, repo_name)
    local no_downloads = config.get("haiku_no_downloads")
    if no_downloads then
        -- Build a local dummy repository
        local download_dir = config.get("haiku_download_dir")
            or path.join(haiku_top, "download")
        local packageFiles = {}
        for _, pkg_id in ipairs(repo.packages or {}) do
            local pkg = _packages[pkg_id]
            if pkg then
                local pf = path.join(download_dir, pkg.file_name)
                table.insert(packageFiles, pf)
            end
        end
        RepositoryCache(repositoryFile, repositoryInfo, packageFiles)
    else
        -- Download repository file
        if os.isfile(packagesChecksumFile) then
            local checksum = io.readfile(packagesChecksumFile):trim()
            local url = string.format("%s/%s/repo", repositoryUrl, checksum)
            FileRules.DownloadFile(repositoryFile, url)
        end
    end

    -- Store computed paths on the repo object
    repo.cache_file = repositoryFile
    repo.config_file = repositoryConfig
    repo.packages_checksum_file = packagesChecksumFile
    repo.definition_file = repo_def_file

    _repositories[repo_name] = repo
end


-- GeneratedRepositoryPackageList: generate a sorted package list file
function GeneratedRepositoryPackageList(target_path, repo)
    import("core.project.depend")

    local fileNames = {}
    for _, pkg_id in ipairs(repo.packages or {}) do
        local pkg = _packages[pkg_id]
        if pkg then
            table.insert(fileNames, pkg.file_name)
        end
    end

    -- Sort and deduplicate
    table.sort(fileNames)
    local unique = {}
    local prev = nil
    for _, fn in ipairs(fileNames) do
        if fn ~= prev then
            table.insert(unique, fn)
            prev = fn
        end
    end

    depend.on_changed(function()
        os.mkdir(path.directory(target_path))
        local content = table.concat(unique, "\n") .. "\n"
        io.writefile(target_path, content)
    end, {
        dependfile = target_path .. ".d"
    })
end


-- RepositoryConfig: build repository config file using create_repository_config
function RepositoryConfig(configPath, infoPath, checksumFile)
    import("core.project.config")
    import("core.project.depend")

    local create_repo_config = config.get("build_create_repository_config")
        or "create_repository_config"
    local compat_lib_dir = config.get("host_add_build_compatibility_lib_dir") or ""

    local depfiles = {infoPath}
    if checksumFile and os.isfile(checksumFile) then
        table.insert(depfiles, checksumFile)
    end

    depend.on_changed(function()
        os.mkdir(path.directory(configPath))

        local args = {infoPath, configPath}
        local envs = nil
        if compat_lib_dir ~= "" then
            envs = {LD_LIBRARY_PATH = compat_lib_dir}
        end

        if checksumFile and os.isfile(checksumFile) then
            -- pass checksumFile as additional arg if needed
        end

        os.vrunv(create_repo_config, args, {envs = envs})
    end, {
        files = depfiles,
        dependfile = configPath .. ".d"
    })
end


-- RepositoryCache: build repository cache using package_repo
function RepositoryCache(cachePath, infoPath, packageFiles)
    import("core.project.config")
    import("core.project.depend")

    local package_repo = config.get("build_package_repo") or "package_repo"
    local compat_lib_dir = config.get("host_add_build_compatibility_lib_dir") or ""

    local depfiles = {infoPath}
    for _, pf in ipairs(packageFiles or {}) do
        if os.isfile(pf) then
            table.insert(depfiles, pf)
        end
    end

    depend.on_changed(function()
        os.mkdir(path.directory(cachePath))

        local args = {"create", "-q", infoPath}
        for _, pf in ipairs(packageFiles or {}) do
            table.insert(args, pf)
        end

        local envs = nil
        if compat_lib_dir ~= "" then
            envs = {LD_LIBRARY_PATH = compat_lib_dir}
        end

        os.vrunv(package_repo, args, {envs = envs})

        -- package_repo creates "repo" file; move it to the desired cache path
        local repo_file = path.join(path.directory(cachePath), "repo")
        if os.isfile(repo_file) then
            os.mv(repo_file, cachePath)
        end
    end, {
        files = depfiles,
        dependfile = cachePath .. ".d"
    })
end


-- ============================================================================
-- Bootstrap Repository
-- ============================================================================

-- BootstrapRepositoryPackageFamily: strip "_bootstrap" from package names
local function _bootstrap_package_family(repo, baseName)
    local stripped = baseName:match("^(.*)_bootstrap(.*)$")
    if stripped then
        local suffix = baseName:match("^.*_bootstrap(.*)$") or ""
        baseName = stripped .. suffix
    end
    return baseName
end


-- BootstrapRepositoryFetchPackage: build packages via haikuporter
local function _bootstrap_fetch_package(repo, pkg_id)
    import("core.project.config")
    import("core.project.depend")

    local pkg = _packages[pkg_id]
    if not pkg then
        raise("BootstrapRepositoryFetchPackage: unknown package " .. tostring(pkg_id))
    end

    local fileName = pkg.file_name
    local outputDir = repo.build_directory
    local configFile = repo.build_config_file
    local crossDevelPackages = pkg.cross_devel_packages or {}

    local packageFile = path.join(outputDir, "packages", fileName)

    -- Don't rebuild if already exists
    if os.isfile(packageFile) then
        return packageFile
    end

    depend.on_changed(function()
        os.mkdir(path.directory(packageFile))

        -- Get primary cross-devel package
        if #crossDevelPackages == 0 then
            raise(packageFile .. " does not have a cross-devel package defined!")
        end

        local primaryCrossDevel = path.absolute(crossDevelPackages[1])

        -- Build secondary cross-devel args
        local secondaryArgs = {}
        for i = 2, #crossDevelPackages do
            local abs = path.absolute(crossDevelPackages[i])
            if #secondaryArgs == 0 then
                table.insert(secondaryArgs,
                    "--secondary-cross-devel-package=" .. abs)
            else
                table.insert(secondaryArgs, abs)
            end
        end

        -- Determine port spec from package name
        local portSpec = fileName:match("^([^-]+)")

        local haiku_porter = config.get("host_haiku_porter") or "haikuporter"
        local porter_jobs = config.get("haiku_porter_concurrent_jobs") or "1"
        local porter_extra = config.get("haiku_porter_extra_options") or ""
        local compat_lib_dir = config.get("host_add_build_compatibility_lib_dir") or ""

        local args = {
            "-j" .. porter_jobs,
            "--all-dependencies"
        }
        if porter_extra ~= "" then
            table.insert(args, porter_extra)
        end
        table.insert(args, "--cross-devel-package")
        table.insert(args, primaryCrossDevel)
        for _, sa in ipairs(secondaryArgs) do
            table.insert(args, sa)
        end
        table.insert(args, portSpec)

        local envs = nil
        if compat_lib_dir ~= "" then
            envs = {LD_LIBRARY_PATH = compat_lib_dir}
        end

        os.execv(haiku_porter, args, {curdir = outputDir, envs = envs})

        if not os.isfile(packageFile) then
            raise("Supposedly built package " .. packageFile
                .. " does not exist; version mismatch?")
        end
    end, {
        files = {configFile},
        dependfile = packageFile .. ".d"
    })

    return packageFile
end


-- BuildBootstrapRepositoryConfig: generate haikuports.conf for bootstrap build
function BuildBootstrapRepositoryConfig(configPath, repo)
    import("core.project.config")
    import("core.project.depend")

    local architecture = config.get("haiku_packaging_arch")
        or config.get("arch") or "x86_64"
    local treePath = repo.tree_path
        or config.get("haiku_ports_cross") or ""
    local haiku_top = config.get("haiku_top") or os.projectdir()
    local outputDir = repo.build_directory or path.directory(configPath)

    local package_tool = config.get("build_package") or "package"
    local mimeset_tool = config.get("build_mimeset") or "mimeset"
    local mime_db = config.get("mimedb_mime_db") or ""

    depend.on_changed(function()
        os.mkdir(path.directory(configPath))

        local lines = {
            'PACKAGER="The Haiku build system <build-system@haiku-os.org>"',
            'TREE_PATH="' .. treePath .. '"',
            'TARGET_ARCHITECTURE="' .. architecture .. '"',
            '',
            'DOWNLOAD_IN_PORT_DIRECTORY="yes"',
            'PACKAGE_COMMAND="' .. path.absolute(package_tool) .. '"',
            'MIMESET_COMMAND="' .. path.absolute(mimeset_tool) .. '"',
            'SYSTEM_MIME_DB="' .. path.absolute(mime_db) .. '"',
            'LICENSES_DIRECTORY="' .. path.join(path.absolute(haiku_top),
                "data", "system", "data", "licenses") .. '"',
            'OUTPUT_DIRECTORY="' .. outputDir .. '"',
            'CREATE_SOURCE_PACKAGES="yes"',
        }

        -- Add cross tools directory if we have cross tools
        local cc = config.get("haiku_cc_" .. architecture)
        if cc and path.is_absolute(cc) then
            local gcc_machine = config.get("haiku_gcc_machine_" .. architecture) or ""
            local cc_basename = path.filename(cc)
            if cc_basename == gcc_machine .. "-gcc" then
                local cross_dir = path.directory(path.directory(cc))
                table.insert(lines, 'CROSS_TOOLS="' .. cross_dir .. '"')
            end
        end

        -- Add secondary architectures
        local packaging_archs = config.get("haiku_packaging_archs") or {}
        if type(packaging_archs) == "string" then
            packaging_archs = {packaging_archs}
        end
        if #packaging_archs > 1 then
            local secondary = {}
            for i = 2, #packaging_archs do
                table.insert(secondary, packaging_archs[i])
            end
            table.insert(lines, 'SECONDARY_TARGET_ARCHITECTURES="')
            for _, arch in ipairs(secondary) do
                table.insert(lines, "  " .. arch)
            end
            table.insert(lines, '"')

            table.insert(lines, 'SECONDARY_CROSS_TOOLS="')
            for _, arch in ipairs(secondary) do
                local gcc = config.get("haiku_cc_" .. arch)
                if gcc then
                    local dir = path.directory(path.directory(gcc))
                    table.insert(lines, "  " .. dir)
                end
            end
            table.insert(lines, '"')
        end

        io.writefile(configPath, table.concat(lines, "\n") .. "\n")
    end, {
        dependfile = configPath .. ".d"
    })
end


-- BootstrapPackageRepository: set up a bootstrap package repository
-- Packages are built locally via haikuporter using cross-compilation
function BootstrapPackageRepository(repo_name, architecture,
        anyPackages, packagesStage0, packagesStage1, packagesStage2,
        sourcePackages, debugInfoPackages)
    import("core.project.config")
    import("rules.BuildFeatureRules")

    -- Filter packages by build features
    packagesStage0 = BuildFeatureRules.FFilterByBuildFeatures(packagesStage0 or {})
    packagesStage1 = BuildFeatureRules.FFilterByBuildFeatures(packagesStage1 or {})
    packagesStage2 = BuildFeatureRules.FFilterByBuildFeatures(packagesStage2 or {})
    sourcePackages = BuildFeatureRules.FFilterByBuildFeatures(sourcePackages or {})
    debugInfoPackages = BuildFeatureRules.FFilterByBuildFeatures(debugInfoPackages or {})

    -- Create repository object
    local repo = {
        name = repo_name,
        packages = {},
        methods = {
            PackageFamily = _bootstrap_package_family,
            FetchPackage = _bootstrap_fetch_package
        }
    }

    -- Register stage 0 packages
    local stage0Targets = PackageRepository(repo, architecture, anyPackages,
        packagesStage0, sourcePackages, debugInfoPackages)
    if #stage0Targets == 0 then
        return
    end

    -- Set cross-devel packages for stage 0
    local packaging_archs = config.get("haiku_packaging_archs") or {}
    if type(packaging_archs) == "string" then
        packaging_archs = {packaging_archs}
    end
    local crossDevelSuffixes = {architecture}
    for i = 2, #packaging_archs do
        table.insert(crossDevelSuffixes,
            architecture .. "_" .. packaging_archs[i])
    end

    for _, pkg_id in ipairs(stage0Targets) do
        local pkg = _packages[pkg_id]
        if pkg then
            pkg.cross_devel_packages = {}
            for _, suffix in ipairs(crossDevelSuffixes) do
                table.insert(pkg.cross_devel_packages,
                    "haiku_cross_devel_sysroot_stage0_" .. suffix .. ".hpkg")
            end
        end
    end

    -- Register stage 1 packages
    local stage1Targets = PackageRepository(repo, architecture, anyPackages,
        packagesStage1, sourcePackages, debugInfoPackages)
    if #stage1Targets == 0 then
        return
    end

    for _, pkg_id in ipairs(stage1Targets) do
        local pkg = _packages[pkg_id]
        if pkg then
            pkg.cross_devel_packages = {}
            for _, suffix in ipairs(crossDevelSuffixes) do
                table.insert(pkg.cross_devel_packages,
                    "haiku_cross_devel_sysroot_stage1_" .. suffix .. ".hpkg")
            end
        end
    end

    -- Register stage 2 packages
    local stage2Targets = AddRepositoryPackages(repo, architecture,
        packagesStage2, sourcePackages, debugInfoPackages)

    for _, pkg_id in ipairs(stage2Targets) do
        local pkg = _packages[pkg_id]
        if pkg then
            pkg.cross_devel_packages = {}
            for _, suffix in ipairs(crossDevelSuffixes) do
                table.insert(pkg.cross_devel_packages,
                    "haiku_cross_devel_sysroot_" .. suffix .. ".hpkg")
            end
        end
    end

    -- Set up build directory and config file
    local haiku_top = config.get("haiku_top") or os.projectdir()
    local repos_dir = config.get("haiku_package_repositories_dir_" .. architecture)
        or path.join(haiku_top, "generated", "objects",
            "haiku", architecture, "packaging", "repositories")

    local outputDir = path.absolute(path.join(repos_dir, repo_name .. "-build"))
    local configFile = path.join(outputDir, "haikuports.conf")

    repo.build_directory = outputDir
    repo.build_config_file = configFile
    repo.tree_path = config.get("haiku_ports_cross") or ""

    BuildBootstrapRepositoryConfig(configFile, repo)

    _repositories[repo_name] = repo
end


-- BuildHaikuPortsSourcePackageDirectory: build source packages
function BuildHaikuPortsSourcePackageDirectory()
    import("core.project.config")
    import("core.project.depend")

    local architecture = config.get("target_packaging_arch")
        or config.get("arch") or "x86_64"
    local haiku_top = config.get("haiku_top") or os.projectdir()
    local repos_dir = config.get("haiku_package_repositories_dir_" .. architecture)
        or path.join(haiku_top, "generated", "objects",
            "haiku", architecture, "packaging", "repositories")

    local outputDir = path.absolute(path.join(repos_dir, "HaikuPorts-sources-build"))
    local sourcePackageDir = path.join(outputDir, "packages")

    -- Build package list
    local packageList = path.join(outputDir, "package_list")
    local build_rules_dir = config.get("haiku_build_rules_dir")
        or path.join(haiku_top, "build", "jam")
    local repo_def_file = path.join(build_rules_dir, "repositories",
        "HaikuPorts", architecture)

    depend.on_changed(function()
        os.mkdir(path.directory(packageList))
        local jam = config.get("jam") or "jam"
        local additional_packages = config.get(
            "haiku_repository_build_additional_packages") or ""
        os.execv(jam, {_bootstrap_sources_profile, "build-package-list",
            packageList, additional_packages})
    end, {
        files = {repo_def_file},
        dependfile = packageList .. ".d"
    })

    -- Prepare config file
    local configFile = path.join(outputDir, "haikuports.conf")

    local pseudo_repo = {
        name = "HaikuPorts-sources",
        build_directory = outputDir,
        tree_path = config.get("haiku_ports") or "",
    }
    BuildBootstrapRepositoryConfig(configFile, pseudo_repo)

    -- Build source packages via haikuporter
    local packaging_archs = config.get("haiku_packaging_archs") or {}
    if type(packaging_archs) == "string" then
        packaging_archs = {packaging_archs}
    end
    local crossDevelSuffixes = {architecture}
    for i = 2, #packaging_archs do
        table.insert(crossDevelSuffixes,
            architecture .. "_" .. packaging_archs[i])
    end
    local crossDevelPackages = {}
    for _, suffix in ipairs(crossDevelSuffixes) do
        table.insert(crossDevelPackages,
            "haiku_cross_devel_sysroot_stage1_" .. suffix .. ".hpkg")
    end

    depend.on_changed(function()
        os.mkdir(sourcePackageDir)

        local primaryCrossDevel = path.absolute(crossDevelPackages[1])
        local haiku_porter = config.get("host_haiku_porter") or "haikuporter"
        local porter_extra = config.get("haiku_porter_extra_options") or ""
        local compat_lib_dir = config.get("host_add_build_compatibility_lib_dir") or ""

        local args = {
            "--cross-devel-package", primaryCrossDevel,
        }

        -- Secondary cross-devel
        for i = 2, #crossDevelPackages do
            local abs = path.absolute(crossDevelPackages[i])
            table.insert(args, "--secondary-cross-devel-package=" .. abs)
        end

        table.insert(args, "--all-dependencies")
        if porter_extra ~= "" then
            table.insert(args, porter_extra)
        end
        table.insert(args, "--create-source-packages-for-bootstrap")
        table.insert(args, "--portsfile")
        table.insert(args, packageList)

        local envs = nil
        if compat_lib_dir ~= "" then
            envs = {LD_LIBRARY_PATH = compat_lib_dir}
        end

        os.execv(haiku_porter, args, {curdir = outputDir, envs = envs})
    end, {
        files = {packageList, configFile},
        dependfile = sourcePackageDir .. ".d"
    })

    return sourcePackageDir
end


-- BuildHaikuPortsRepositoryConfig: generate bootstrap repository config
function BuildHaikuPortsRepositoryConfig(treePath)
    import("core.project.config")
    import("core.project.depend")

    local architecture = config.get("target_packaging_arch")
        or config.get("arch") or "x86_64"
    local repos_dir = config.get("haiku_package_repositories_dir_" .. architecture)
        or path.join(config.get("haiku_top") or os.projectdir(), "generated",
            "objects", "haiku", architecture, "packaging", "repositories")

    local outputDir = path.join(repos_dir, "HaikuPorts-bootstrap")
    local configFile = path.join(outputDir, "haikuports.conf")

    local ci_build = config.get("haiku_continuous_integration_build")

    depend.on_changed(function()
        os.mkdir(outputDir)

        local lines = {}
        if ci_build then
            table.insert(lines,
                'PACKAGER="Haiku buildmaster <buildmaster@haiku-os.org>"')
        else
            table.insert(lines,
                '#PACKAGER="Joe Hacker <user@host.com>"')
        end
        table.insert(lines, 'TREE_PATH="' .. (treePath or "") .. '"')
        table.insert(lines, 'TARGET_ARCHITECTURE="' .. architecture .. '"')

        io.writefile(configFile, table.concat(lines, "\n") .. "\n")
    end, {
        dependfile = configFile .. ".d"
    })

    return configFile
end


-- ============================================================================
-- Public
-- ============================================================================

-- FSplitPackageName: split package name into base name + known suffix
-- Returns {baseName, suffix} if suffix is known, otherwise {packageName}
function FSplitPackageName(packageName)
    local base, suffix = packageName:match("^(.*)_([^_]*)$")
    local knownSuffixes = {devel = true, doc = true, source = true, debuginfo = true}

    if base and suffix and knownSuffixes[suffix] then
        return {base, suffix}
    end

    return {packageName}
end


-- IsPackageAvailable: check if a package is available
-- For secondary architectures, tries inserting arch into the package name
-- Returns the resolved package name or nil
function IsPackageAvailable(packageName, flags)
    import("core.project.config")

    flags = flags or {}
    local flags_set = {}
    for _, f in ipairs(flags) do flags_set[f] = true end

    local packaging_arch = config.get("target_packaging_arch")
        or config.get("arch") or "x86_64"
    local packaging_archs = config.get("target_packaging_archs") or {}
    if type(packaging_archs) == "string" then
        packaging_archs = {packaging_archs}
    end

    -- For secondary architecture, adjust package name
    if #packaging_archs > 0 and packaging_arch ~= packaging_archs[1]
            and not flags_set["nameResolved"] then
        -- Scan for correct position to insert architecture suffix
        local head = packageName
        local tail_parts = {}
        while head and head ~= "" do
            local split = FSplitPackageName(head)
            -- Try with arch inserted after the base
            local parts = {split[1], packaging_arch}
            if split[2] then
                table.insert(parts, split[2])
            end
            for _, tp in ipairs(tail_parts) do
                table.insert(parts, tp)
            end
            local candidate = table.concat(parts, "_")
            if _available_packages[candidate] then
                return candidate
            end

            -- Move last component of head to tail
            local new_base, last_part = head:match("^(.*)_([^_]*)$")
            if not new_base then break end
            head = new_base
            table.insert(tail_parts, 1, last_part)
        end
    end

    if _available_packages[packageName] then
        return packageName
    end

    return nil
end


-- FetchPackage: fetch a package file
-- Returns the path to the downloaded/built package file
function FetchPackage(packageName, flags)
    local foundName = IsPackageAvailable(packageName, flags)
    if not foundName then
        raise("FetchPackage: package " .. packageName .. " not available!")
    end
    packageName = foundName

    local family_key = PackageFamily(packageName)
    local family = _package_families[family_key]
    if not family or #family.versions == 0 then
        raise("FetchPackage: no versions for package " .. packageName)
    end

    -- Get the first (most recent) version
    local pkg_id = family.versions[1]
    local pkg = _packages[pkg_id]
    if not pkg then
        raise("FetchPackage: package data not found for " .. pkg_id)
    end

    local repo = pkg.repository

    import("core.project.config")
    if config.get("haiku_dont_fetch_packages") then
        raise("FetchPackage: file " .. pkg.file_name
            .. " not found and fetching disabled!")
    end

    -- Invoke the repository's fetch method
    if repo.methods and repo.methods.FetchPackage then
        return repo.methods.FetchPackage(repo, pkg_id)
    end

    raise("FetchPackage: no FetchPackage method for repository " .. repo.name)
end


-- HaikuRepository: build a Haiku repository from packages and info template
-- repo_name: repository target name
-- infoTemplate: repository info template file
-- packages: list of package file paths
function HaikuRepository(repo_name, infoTemplate, packages)
    import("core.project.config")
    import("core.project.depend")

    local architecture = config.get("haiku_packaging_arch")
        or config.get("arch") or "x86_64"
    local haiku_top = config.get("haiku_top") or os.projectdir()
    local repos_dir = config.get("haiku_package_repositories_dir_" .. architecture)
        or path.join(haiku_top, "generated", "objects",
            "haiku", architecture, "packaging", "repositories")
    local packaging_arch = config.get("target_packaging_arch")
        or config.get("arch") or "x86_64"

    local secondaryArchitecture = nil
    if packaging_arch ~= architecture then
        secondaryArchitecture = packaging_arch
    end

    -- Build repository info
    local repositoryInfo = path.join(repos_dir, repo_name .. "-info")
    import("rules.PackageRules")
    PackageRules.PreprocessPackageOrRepositoryInfo(
        repositoryInfo, infoTemplate, architecture, secondaryArchitecture)

    -- Build repository config
    local repositoryConfig = path.join(repos_dir, repo_name .. "-config")
    RepositoryConfig(repositoryConfig, repositoryInfo)

    -- Repository cache
    local repositoryDir = path.join(repos_dir, repo_name)
    local repositoryCache = path.join(repositoryDir, "repo")

    -- Prepare init-variables script
    local initVarsScript = path.join(repos_dir, repo_name .. "-repository-init-vars")
    local sha256 = config.get("host_sha256") or "sha256sum"
    local sed = config.get("host_extended_regex_sed") or "sed -E"
    local compat_lib_dir = config.get("host_add_build_compatibility_lib_dir") or ""
    local package_tool = config.get("build_package") or "package"
    local package_repo_tool = config.get("build_package_repo") or "package_repo"

    depend.on_changed(function()
        os.mkdir(path.directory(initVarsScript))

        local lines = {
            'addBuildCompatibilityLibDir="export LD_LIBRARY_PATH='
                .. compat_lib_dir .. '"',
            'sha256="' .. sha256 .. '"',
            'sedExtendedRegex="' .. sed .. '"',
            'package="' .. path.absolute(package_tool) .. '"',
            'packageRepo="' .. path.absolute(package_repo_tool) .. '"',
        }
        io.writefile(initVarsScript, table.concat(lines, "\n") .. "\n")
    end, {
        dependfile = initVarsScript .. ".d"
    })

    -- Run build_haiku_repository script
    local mainScript = path.join(haiku_top, "build", "scripts",
        "build_haiku_repository")

    local allDeps = {mainScript, initVarsScript, repositoryInfo}
    for _, pkg in ipairs(packages or {}) do
        table.insert(allDeps, pkg)
    end

    depend.on_changed(function()
        os.mkdir(repositoryDir)

        local args = {initVarsScript, repositoryDir, repositoryInfo}
        for _, pkg in ipairs(packages or {}) do
            table.insert(args, pkg)
        end

        os.vrunv(mainScript, args)
    end, {
        files = allDeps,
        dependfile = repositoryDir .. ".build.d"
    })

    -- Register
    local repo = _repositories[repo_name] or {name = repo_name}
    repo.cache_file = repositoryCache
    repo.config_file = repositoryConfig
    _repositories[repo_name] = repo
    if not _repositories[repo_name] then
        table.insert(_repository_names, repo_name)
    end
end


-- ============================================================================
-- Accessors
-- ============================================================================

-- GetRepository: get a registered repository by name
function GetRepository(name)
    return _repositories[name]
end

-- GetAllRepositories: get all registered repositories
function GetAllRepositories()
    return _repositories
end

-- GetAvailablePackages: get the set of available package names
function GetAvailablePackages()
    return _available_packages
end

-- GetPackage: get package data by ID
function GetPackage(pkg_id)
    return _packages[pkg_id]
end

-- GetPackageFamily: get a package family by base name
function GetPackageFamily(baseName)
    local key = PackageFamily(baseName)
    return _package_families[key]
end
