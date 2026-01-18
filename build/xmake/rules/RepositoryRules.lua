--[[
    RepositoryRules.lua - Haiku package repository rules

    xmake equivalent of build/jam/RepositoryRules

    Rules defined:
    - PackageFamily               - Get package family identifier
    - SetRepositoryMethod         - Set method for repository
    - InvokeRepositoryMethod      - Call repository method
    - AddRepositoryPackage        - Add package to repository
    - AddRepositoryPackages       - Add multiple packages
    - PackageRepository           - Create base package repository
    - RemotePackageRepository     - Setup remote repository (HaikuPorts)
    - BootstrapPackageRepository  - Setup bootstrap repository
    - GeneratedRepositoryPackageList - Generate package list file
    - RepositoryConfig            - Create repository config
    - RepositoryCache             - Create repository cache
    - FSplitPackageName           - Split package name into parts
    - IsPackageAvailable          - Check if package is available
    - FetchPackage                - Fetch a package
    - HaikuRepository             - Build Haiku repository

    The package repository system manages:
    - Remote repositories (HaikuPorts downloads)
    - Bootstrap repositories (cross-compiled packages)
    - Package metadata and checksums
]]

-- ============================================================================
-- Configuration
-- ============================================================================

-- Default bootstrap sources profile
local HAIKU_BOOTSTRAP_SOURCES_PROFILE = "@minimum-raw"

-- Storage
local _repositories = {}              -- registered repositories
local _available_packages = {}        -- list of available package names
local _package_families = {}          -- package family -> versions mapping
local _repository_methods = {}        -- repository -> method name -> function

-- ============================================================================
-- Private Helpers
-- ============================================================================

--[[
    PackageFamily(package_base_name)

    Get package family identifier with grist.

    Parameters:
        package_base_name - Base package name

    Returns:
        Package family identifier
]]
function PackageFamily(package_base_name)
    return "<package-family>" .. package_base_name
end

--[[
    SetRepositoryMethod(repository, method_name, method_func)

    Set a method function for a repository.

    Parameters:
        repository - Repository identifier
        method_name - Method name (e.g., "PackageFamily", "FetchPackage")
        method_func - Function to call
]]
function SetRepositoryMethod(repository, method_name, method_func)
    if not _repository_methods[repository] then
        _repository_methods[repository] = {}
    end
    _repository_methods[repository][method_name] = method_func
end

--[[
    InvokeRepositoryMethod(repository, method_name, ...)

    Invoke a repository method.

    Parameters:
        repository - Repository identifier
        method_name - Method name
        ... - Arguments to pass

    Returns:
        Method result
]]
function InvokeRepositoryMethod(repository, method_name, ...)
    local methods = _repository_methods[repository]
    if not methods or not methods[method_name] then
        error(string.format("Method %s not defined for repository %s",
            method_name, repository))
    end
    return methods[method_name](repository, ...)
end

-- ============================================================================
-- Package Management
-- ============================================================================

--[[
    AddRepositoryPackage(repository, architecture, base_name, version)

    Add a package to a repository.

    Equivalent to Jam:
        rule AddRepositoryPackage { }

    Parameters:
        repository - Repository identifier
        architecture - Package architecture (e.g., "x86_64", "any", "source")
        base_name - Package base name
        version - Package version

    Returns:
        Package identifier or nil
]]
function AddRepositoryPackage(repository, architecture, base_name, version)
    local package_name = base_name
    if version then
        package_name = base_name .. "-" .. version
    end

    local package_id = "<package-in-" .. repository .. ">" .. package_name

    -- Store package metadata
    local package_data = {
        id = package_id,
        repository = repository,
        architecture = architecture,
        base_name = base_name,
        version = version,
        file_name = package_name .. "-" .. architecture .. ".hpkg"
    }

    -- Get package family (may be modified by repository method)
    local family = InvokeRepositoryMethod(repository, "PackageFamily", base_name)
    local family_name = family:gsub("^<package%-family>", "")

    -- Check if downloads are disabled (skip unavailable packages)
    local no_downloads = get_config("no_downloads")
    local is_bootstrap = get_config("is_bootstrap")
    local download_dir = get_config("download_dir") or "$(buildir)/download"

    if no_downloads and not is_bootstrap then
        local pkg_path = path.join(download_dir, package_data.file_name)
        if not os.isfile(pkg_path) then
            return nil
        end
    end

    -- Register package
    if not _available_packages[family_name] then
        _available_packages[family_name] = true
    end

    if not _package_families[family] then
        _package_families[family] = {}
    end
    table.insert(_package_families[family], package_data)

    if not _repositories[repository] then
        _repositories[repository] = {
            packages = {},
            config = {}
        }
    end
    table.insert(_repositories[repository].packages, package_data)

    return package_id
end

--[[
    AddRepositoryPackages(repository, architecture, packages, source_packages, debug_packages)

    Add multiple packages to a repository.

    Equivalent to Jam:
        rule AddRepositoryPackages { }

    Parameters:
        repository - Repository identifier
        architecture - Package architecture
        packages - List of "name-version" strings
        source_packages - List of packages that have source packages
        debug_packages - List of packages that have debuginfo packages

    Returns:
        List of package identifiers
]]
function AddRepositoryPackages(repository, architecture, packages, source_packages, debug_packages)
    source_packages = source_packages or {}
    debug_packages = debug_packages or {}

    if type(packages) == "string" then
        packages = {packages}
    end

    local package_targets = {}

    for _, pkg in ipairs(packages) do
        -- Split "name-version"
        local base_name, version = pkg:match("^([^-]+)-(.+)$")
        if not base_name then
            base_name = pkg
            version = nil
        end

        local pkg_id = AddRepositoryPackage(repository, architecture, base_name, version)
        if pkg_id then
            table.insert(package_targets, pkg_id)
        end

        -- Add source package if requested
        if source_packages[base_name] then
            AddRepositoryPackage(repository, "source", base_name .. "_source", version)
        end

        -- Add debuginfo package if requested
        if debug_packages[base_name] then
            local debug_id = AddRepositoryPackage(repository, architecture,
                base_name .. "_debuginfo", version)
            if debug_id then
                table.insert(package_targets, debug_id)
            end
        end
    end

    return package_targets
end

--[[
    PackageRepository(repository, architecture, any_packages, packages, source_packages, debug_packages)

    Create a package repository.

    Equivalent to Jam:
        rule PackageRepository { }

    Parameters:
        repository - Repository identifier
        architecture - Primary architecture
        any_packages - Architecture-independent packages
        packages - Architecture-specific packages
        source_packages - Source package list
        debug_packages - Debug info package list

    Returns:
        List of package identifiers
]]
function PackageRepository(repository, architecture, any_packages, packages, source_packages, debug_packages)
    local primary_arch = get_config("arch") or "x86_64"

    -- Only process for primary architecture
    if architecture ~= primary_arch then
        return {}
    end

    -- Register repository
    _repositories[repository] = _repositories[repository] or {
        packages = {},
        config = {}
    }

    local targets = {}

    -- Add any-arch packages
    if any_packages then
        local any_targets = AddRepositoryPackages(repository, "any", any_packages,
            source_packages, debug_packages)
        for _, t in ipairs(any_targets) do
            table.insert(targets, t)
        end
    end

    -- Add arch-specific packages
    if packages then
        local arch_targets = AddRepositoryPackages(repository, architecture, packages,
            source_packages, debug_packages)
        for _, t in ipairs(arch_targets) do
            table.insert(targets, t)
        end
    end

    return targets
end

-- ============================================================================
-- Remote Repository
-- ============================================================================

--[[
    RemoteRepositoryPackageFamily(repository, package_base_name)

    Get package family for remote repository (standard behavior).
]]
local function RemoteRepositoryPackageFamily(repository, package_base_name)
    return PackageFamily(package_base_name)
end

--[[
    RemoteRepositoryFetchPackage(repository, package, file_name)

    Fetch a package from remote repository.
]]
local function RemoteRepositoryFetchPackage(repository, package_data, file_name)
    local repo_data = _repositories[repository]
    local base_url = repo_data.config.url
    local checksum_file = repo_data.config.checksum_file
    local download_dir = get_config("download_dir") or "$(buildir)/download"

    -- Download using DownloadFile (from FileRules)
    local url = string.format("%s/packages/%s", base_url, file_name)
    local dest = path.join(download_dir, file_name)

    if DownloadFile then
        DownloadFile(dest, url)
    else
        os.mkdir(download_dir)
        os.exec(string.format("curl -fsSL -o %s %s", dest, url))
    end

    return dest
end

--[[
    RemotePackageRepository(repository, architecture, repository_url,
        any_packages, packages, source_packages, debug_packages)

    Setup a remote package repository (like HaikuPorts).

    Equivalent to Jam:
        rule RemotePackageRepository { }

    Parameters:
        repository - Repository name
        architecture - Target architecture
        repository_url - Base URL for downloads
        any_packages - Any-arch packages
        packages - Arch-specific packages
        source_packages - Source packages
        debug_packages - Debug info packages
]]
function RemotePackageRepository(repository, architecture, repository_url,
    any_packages, packages, source_packages, debug_packages)

    repository = "<repository>" .. repository

    -- Set repository methods
    SetRepositoryMethod(repository, "PackageFamily", RemoteRepositoryPackageFamily)
    SetRepositoryMethod(repository, "FetchPackage", RemoteRepositoryFetchPackage)

    -- Store URL
    _repositories[repository] = _repositories[repository] or {packages = {}, config = {}}
    _repositories[repository].config.url = repository_url

    -- Add packages
    PackageRepository(repository, architecture, any_packages, packages,
        source_packages, debug_packages)

    -- Build package list file
    local buildir = get_config("buildir") or "$(buildir)"
    local repos_dir = path.join(buildir, "repositories", architecture)
    os.mkdir(repos_dir)

    local package_list_file = path.join(repos_dir, repository:gsub("[<>]", "") .. "-packages")
    GeneratedRepositoryPackageList(package_list_file, repository)

    -- Build checksum file
    local checksum_file = path.join(repos_dir, repository:gsub("[<>]", "") .. "-checksum")
    _repositories[repository].config.checksum_file = checksum_file

    -- Repository info and config
    local repo_info = path.join(repos_dir, repository:gsub("[<>]", "") .. "-info")
    local repo_config = path.join(repos_dir, repository:gsub("[<>]", "") .. "-config")

    _repositories[repository].config.info_file = repo_info
    _repositories[repository].config.config_file = repo_config
end

-- ============================================================================
-- Bootstrap Repository
-- ============================================================================

--[[
    BootstrapRepositoryPackageFamily(repository, package_base_name)

    Get package family for bootstrap repository.
    Strips "_bootstrap" suffix if present.
]]
local function BootstrapRepositoryPackageFamily(repository, package_base_name)
    -- Strip _bootstrap suffix
    local stripped = package_base_name:gsub("_bootstrap", "")
    return PackageFamily(stripped)
end

--[[
    BootstrapRepositoryFetchPackage(repository, package, file_name)

    Build a package using haikuporter for bootstrap.
]]
local function BootstrapRepositoryFetchPackage(repository, package_data, file_name)
    local repo_data = _repositories[repository]
    local output_dir = repo_data.config.build_directory
    local config_file = repo_data.config.config_file

    local package_file = path.join(output_dir, "packages", file_name)

    -- Skip if already built
    if os.isfile(package_file) then
        return package_file
    end

    -- Get port spec from package name
    local port_spec = file_name:gsub("-.*", "")

    -- Build using haikuporter
    local porter = get_config("haiku_porter") or "haikuporter"
    local jobs = get_config("porter_jobs") or 4

    print(string.format("Building bootstrap package: %s", port_spec))

    local cmd = string.format(
        "cd %s && %s -j%d --all-dependencies %s",
        output_dir, porter, jobs, port_spec
    )
    os.exec(cmd)

    if not os.isfile(package_file) then
        error(string.format("Package %s not built; version mismatch?", package_file))
    end

    return package_file
end

--[[
    BootstrapPackageRepository(repository, architecture, any_packages,
        packages_stage0, packages_stage1, packages_stage2,
        source_packages, debug_packages)

    Setup a bootstrap package repository for cross-compilation.

    Equivalent to Jam:
        rule BootstrapPackageRepository { }
]]
function BootstrapPackageRepository(repository, architecture, any_packages,
    packages_stage0, packages_stage1, packages_stage2,
    source_packages, debug_packages)

    repository = "<repository>" .. repository

    -- Set repository methods
    SetRepositoryMethod(repository, "PackageFamily", BootstrapRepositoryPackageFamily)
    SetRepositoryMethod(repository, "FetchPackage", BootstrapRepositoryFetchPackage)

    -- Register stage 0 packages (basic cross-devel)
    local stage0_targets = PackageRepository(repository, architecture, any_packages,
        packages_stage0, source_packages, debug_packages)

    if #stage0_targets == 0 then
        return
    end

    -- Register stage 1 packages
    local stage1_targets = PackageRepository(repository, architecture, any_packages,
        packages_stage1, source_packages, debug_packages)

    -- Register stage 2 packages
    local stage2_targets = AddRepositoryPackages(repository, architecture,
        packages_stage2, source_packages, debug_packages)

    -- Setup build directory
    local buildir = get_config("buildir") or "$(buildir)"
    local output_dir = path.join(buildir, "repositories", architecture,
        repository:gsub("[<>]", "") .. "-build")
    os.mkdir(output_dir)

    _repositories[repository] = _repositories[repository] or {packages = {}, config = {}}
    _repositories[repository].config.build_directory = output_dir

    -- Create haikuports.conf
    local config_file = path.join(output_dir, "haikuports.conf")
    BuildBootstrapRepositoryConfig(config_file, architecture, output_dir)

    _repositories[repository].config.config_file = config_file
end

--[[
    BuildBootstrapRepositoryConfig(config_file, architecture, output_dir)

    Generate haikuports.conf for bootstrap builds.
]]
function BuildBootstrapRepositoryConfig(config_file, architecture, output_dir)
    local haiku_top = os.projectdir()
    local tree_path = get_config("haiku_ports_cross") or
        path.join(haiku_top, "3rdparty", "haikuports")

    local content = string.format([[
PACKAGER="The Haiku build system <build-system@haiku-os.org>"
TREE_PATH="%s"
TARGET_ARCHITECTURE="%s"

DOWNLOAD_IN_PORT_DIRECTORY="yes"
LICENSES_DIRECTORY="%s/data/system/data/licenses"
OUTPUT_DIRECTORY="%s"
CREATE_SOURCE_PACKAGES="yes"
]], tree_path, architecture, haiku_top, output_dir)

    -- Write config file
    local f = io.open(config_file, "w")
    if f then
        f:write(content)
        f:close()
    end
end

-- ============================================================================
-- Repository File Generation
-- ============================================================================

--[[
    GeneratedRepositoryPackageList(target, repository)

    Generate a file listing all package file names in a repository.

    Equivalent to Jam:
        rule GeneratedRepositoryPackageList { }
]]
function GeneratedRepositoryPackageList(target, repository)
    local repo_data = _repositories[repository]
    if not repo_data then
        return
    end

    local file_names = {}
    for _, pkg in ipairs(repo_data.packages) do
        table.insert(file_names, pkg.file_name)
    end

    -- Sort and deduplicate
    table.sort(file_names)
    local unique = {}
    local prev = nil
    for _, name in ipairs(file_names) do
        if name ~= prev then
            table.insert(unique, name)
            prev = name
        end
    end

    -- Write to file
    local f = io.open(target, "w")
    if f then
        for _, name in ipairs(unique) do
            f:write(name .. "\n")
        end
        f:close()
    end
end

--[[
    RepositoryConfig(repo_config, repo_info, checksum_file)

    Create repository configuration file.

    Equivalent to Jam:
        rule RepositoryConfig { }
]]
function RepositoryConfig(repo_config, repo_info, checksum_file)
    local haiku_top = os.projectdir()
    local create_config = path.join(haiku_top, "build", "tools", "create_repository_config")

    local cmd = string.format("%s %s %s", create_config, repo_info, repo_config)
    os.exec(cmd)
end

--[[
    RepositoryCache(repo_cache, repo_info, package_files)

    Create repository cache file.

    Equivalent to Jam:
        rule RepositoryCache { }
]]
function RepositoryCache(repo_cache, repo_info, package_files)
    local haiku_top = os.projectdir()
    local package_repo = path.join(haiku_top, "build", "tools", "package_repo")

    if type(package_files) == "table" then
        package_files = table.concat(package_files, " ")
    end

    local cmd = string.format("%s create -q %s %s", package_repo, repo_info, package_files)
    os.exec(cmd)

    -- Rename output
    local repo_file = path.join(path.directory(repo_cache), "repo")
    if os.isfile(repo_file) then
        os.mv(repo_file, repo_cache)
    end
end

-- ============================================================================
-- Public Package Functions
-- ============================================================================

--[[
    FSplitPackageName(package_name)

    Split package name into base name and suffix.

    Equivalent to Jam:
        rule FSplitPackageName { }

    Parameters:
        package_name - Package name (e.g., "foo_devel", "bar_source")

    Returns:
        {base_name, suffix} or {package_name} if no known suffix
]]
function FSplitPackageName(package_name)
    local known_suffixes = {"devel", "doc", "source", "debuginfo"}

    for _, suffix in ipairs(known_suffixes) do
        local pattern = "(.*)_" .. suffix .. "$"
        local base = package_name:match(pattern)
        if base then
            return {base, suffix}
        end
    end

    return {package_name}
end

--[[
    IsPackageAvailable(package_name, flags)

    Check if a package is available in any repository.

    Equivalent to Jam:
        rule IsPackageAvailable { }

    Parameters:
        package_name - Package name to check
        flags - Optional flags table (e.g., {nameResolved = true})

    Returns:
        Resolved package name or nil
]]
function IsPackageAvailable(package_name, flags)
    flags = flags or {}

    local target_arch = get_config("arch") or "x86_64"
    local primary_arch = get_config("primary_arch") or target_arch

    -- For secondary architecture, try with arch suffix
    if target_arch ~= primary_arch and not flags.nameResolved then
        local parts = FSplitPackageName(package_name)
        local name_with_arch = parts[1] .. "_" .. target_arch
        if parts[2] then
            name_with_arch = name_with_arch .. "_" .. parts[2]
        end

        if _available_packages[name_with_arch] then
            return name_with_arch
        end
    end

    if _available_packages[package_name] then
        return package_name
    end

    return nil
end

--[[
    FetchPackage(package_name, flags)

    Fetch a package from its repository.

    Equivalent to Jam:
        rule FetchPackage { }

    Parameters:
        package_name - Package name
        flags - Optional flags

    Returns:
        Path to downloaded/built package file
]]
function FetchPackage(package_name, flags)
    local found_name = IsPackageAvailable(package_name, flags)
    if not found_name then
        error(string.format("FetchPackage: package %s not available!", package_name))
    end
    package_name = found_name

    local family = PackageFamily(package_name)
    local versions = _package_families[family]

    if not versions or #versions == 0 then
        error(string.format("FetchPackage: no versions for %s", package_name))
    end

    local package_data = versions[1]  -- Use first (latest) version
    local repository = package_data.repository

    if get_config("dont_fetch_packages") then
        error(string.format("FetchPackage: file %s not found and fetching disabled!",
            package_data.file_name))
    end

    return InvokeRepositoryMethod(repository, "FetchPackage", package_data,
        package_data.file_name)
end

-- ============================================================================
-- HaikuRepository
-- ============================================================================

--[[
    HaikuRepository(repository, repo_info_template, packages, url, version_file)

    Build a complete Haiku repository.

    Equivalent to Jam:
        rule HaikuRepository { }

    Parameters:
        repository - Repository target (directory path)
        repo_info_template - Repository info template file
        packages - List of package files
        url - Optional repository URL
        version_file - Optional version file
]]
function HaikuRepository(repository, repo_info_template, packages, url, version_file)
    local architecture = get_config("arch") or "x86_64"
    local buildir = get_config("buildir") or "$(buildir)"
    local repos_dir = path.join(buildir, "repositories", architecture)

    os.mkdir(repos_dir)
    os.mkdir(repository)

    -- Build repository info
    local repo_info = path.join(repos_dir, path.basename(repository) .. "-info")
    if PreprocessPackageOrRepositoryInfo then
        PreprocessPackageOrRepositoryInfo(repo_info, repo_info_template, architecture)
    end

    -- Build repository config
    local repo_config = path.join(repos_dir, path.basename(repository) .. "-config")
    RepositoryConfig(repo_config, repo_info)

    -- Build repository cache
    local repo_cache = path.join(repository, "repo")
    RepositoryCache(repo_cache, repo_info, packages)

    -- Register repository
    _repositories[repository] = _repositories[repository] or {packages = {}, config = {}}
    _repositories[repository].config.cache_file = repo_cache
    _repositories[repository].config.config_file = repo_config

    return repository
end

-- ============================================================================
-- Accessor Functions
-- ============================================================================

--[[
    GetRepositories()

    Get list of all registered repositories.
]]
function GetRepositories()
    local repos = {}
    for name, _ in pairs(_repositories) do
        table.insert(repos, name)
    end
    return repos
end

--[[
    GetRepositoryPackages(repository)

    Get packages in a repository.
]]
function GetRepositoryPackages(repository)
    if _repositories[repository] then
        return _repositories[repository].packages
    end
    return {}
end

--[[
    GetAvailablePackages()

    Get list of all available package names.
]]
function GetAvailablePackages()
    local packages = {}
    for name, _ in pairs(_available_packages) do
        table.insert(packages, name)
    end
    return packages
end

--[[
    ClearRepositoryData()

    Clear all repository data (for testing).
]]
function ClearRepositoryData()
    _repositories = {}
    _available_packages = {}
    _package_families = {}
    _repository_methods = {}
end