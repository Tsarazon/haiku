-- HaikuPortsCross bootstrap repository for x86 architecture
-- Ported from build/jam/repositories/HaikuPortsCross/x86

-- NOTE: import() must be inside functions when module is used via import()

-- Repository configuration
ARCHITECTURE = "x86"

-- Architecture "any" packages (architecture-independent)
ANY_PACKAGES = {
    "haikuporter-0-1",
    "noto-20170202-7",
}

-- Stage 0 packages (GCC bootstrap)
STAGE0_PACKAGES = {
    "gcc_bootstrap-13.2.0_2023_08_10-1",
    "gcc_bootstrap_syslibs-13.2.0_2023_08_10-1",
    "gcc_bootstrap_syslibs_devel-13.2.0_2023_08_10-1",
}

-- Stage 1 packages (build tools)
STAGE1_PACKAGES = {
    "bash_bootstrap-4.4.023-1",
    "binutils_bootstrap-2.41_2023_08_05-1",
    "bison_bootstrap-3.0.5-1",
    "coreutils_bootstrap-8.22-1",
    "curl_bootstrap-7.40.0-1",
    "curl_bootstrap_devel-7.40.0-1",
    "expat_bootstrap-2.5.0-1",
    "expat_bootstrap_devel-2.5.0-1",
    "findutils_bootstrap-4.6.0-1",
    "flex_bootstrap-2.5.35-1",
    "freetype_bootstrap-2.6.3-1",
    "freetype_bootstrap_devel-2.6.3-1",
    "gawk_bootstrap-3.1.8-2",
    "grep_bootstrap-2.14-1",
    "icu_bootstrap-67.1-2",
    "icu_bootstrap_devel-67.1-2",
    "less_bootstrap-451-1",
    "m4_bootstrap-1.4.19-1",
    "make_bootstrap-4.3-1",
    "ncurses6_bootstrap-6.2-1",
    "ncurses6_bootstrap_devel-6.2-1",
    "python_bootstrap-2.7.6-1",
    "sed_bootstrap-4.2.1-1",
    "texinfo_bootstrap-4.13a-1",
    "zlib_bootstrap-1.2.13-1",
    "zlib_bootstrap_devel-1.2.13-1",
}

-- Stage 2 packages (empty for this architecture)
STAGE2_PACKAGES = {}

-- Source packages
SOURCE_PACKAGES = {
    "bash_bootstrap",
    "binutils_bootstrap",
    "bison_bootstrap",
    "coreutils_bootstrap",
    "curl_bootstrap",
    "expat_bootstrap",
    "findutils_bootstrap",
    "flex_bootstrap",
    "freetype_bootstrap",
    "gawk_bootstrap",
    "gcc_bootstrap",
    "grep_bootstrap",
    "less_bootstrap",
    "m4_bootstrap",
    "make_bootstrap",
    "ncurses6_bootstrap",
    "python_bootstrap",
    "sed_bootstrap",
    "texinfo_bootstrap",
    "zlib_bootstrap",
}

-- Debug info packages (none for bootstrap)
DEBUGINFO_PACKAGES = {}

-- Combine all arch packages
local function get_all_arch_packages()
    local packages = {}

    -- Add stage 0 packages
    for _, pkg in ipairs(STAGE0_PACKAGES) do
        table.insert(packages, pkg)
    end

    -- Add stage 1 packages
    for _, pkg in ipairs(STAGE1_PACKAGES) do
        table.insert(packages, pkg)
    end

    -- Add stage 2 packages
    for _, pkg in ipairs(STAGE2_PACKAGES) do
        table.insert(packages, pkg)
    end

    return packages
end

-- Build version lookup table from arch packages
local function build_version_lookup()
    local lookup = {}
    local all_packages = get_all_arch_packages()

    for _, pkg in ipairs(all_packages) do
        local name = pkg:match("^(.+)%-%d")
        if name then
            local base_name = name:gsub("_devel$", ""):gsub("_syslibs$", "")
            lookup[base_name] = pkg
        end
    end

    for _, pkg in ipairs(ANY_PACKAGES) do
        local name = pkg:match("^(.+)%-%d")
        if name and not lookup[name] then
            lookup[name] = pkg
        end
    end

    return lookup
end

-- Build source package lookup
local function build_source_lookup()
    local lookup = {}
    local version_lookup = build_version_lookup()

    for _, name in ipairs(SOURCE_PACKAGES) do
        local versioned = version_lookup[name]
        if versioned then
            local version = versioned:match("^.+%-(%d.+)$")
            if version then
                lookup[name] = version
            end
        end
    end

    return lookup
end

-- Main function to initialize repository
function main()
    local RepositoryRules = import("rules.RepositoryRules")

    local arch_packages = get_all_arch_packages()
    local source_lookup = build_source_lookup()

    RepositoryRules.BootstrapPackageRepository(
        "HaikuPortsCross",
        ARCHITECTURE,
        ANY_PACKAGES,
        arch_packages,
        source_lookup,
        DEBUGINFO_PACKAGES
    )
end

return {
    ARCHITECTURE = ARCHITECTURE,
    ANY_PACKAGES = ANY_PACKAGES,
    STAGE0_PACKAGES = STAGE0_PACKAGES,
    STAGE1_PACKAGES = STAGE1_PACKAGES,
    STAGE2_PACKAGES = STAGE2_PACKAGES,
    SOURCE_PACKAGES = SOURCE_PACKAGES,
    DEBUGINFO_PACKAGES = DEBUGINFO_PACKAGES,
    get_all_arch_packages = get_all_arch_packages,
    main = main,
}