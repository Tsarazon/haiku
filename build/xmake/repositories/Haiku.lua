--[[
    Haiku.lua - Haiku local package repository

    xmake equivalent of build/jam/repositories/Haiku

    Builds the Haiku packages repository from locally built packages.
]]

import("rules.RepositoryRules")

-- ============================================================================
-- Haiku Repository Definition
-- ============================================================================

function main()
    local architecture = get_config("arch") or "x86_64"
    local secondary_archs = get_config("secondary_archs") or {}
    local is_bootstrap = get_config("is_bootstrap")
    local build_type = get_config("build_type") or "regular"

    -- Repository info template
    local haiku_top = os.projectdir()
    local repo_info = path.join(haiku_top, "src", "data", "repository_infos", "haiku")

    -- Base packages (always included)
    local packages = {
        "haiku",
        "haiku_datatranslators",
        "haiku_devel",
        "haiku_loader",
    }

    -- Secondary architecture packages
    for _, arch in ipairs(secondary_archs) do
        table.insert(packages, "haiku_" .. arch)
        table.insert(packages, "haiku_" .. arch .. "_devel")
    end

    -- Extra packages (not for bootstrap or minimum builds)
    if not is_bootstrap and build_type ~= "minimum" then
        local extra_packages = {
            "haiku_extras",
            "makefile_engine",
            "netfs",
            "userland_fs",
        }

        for _, pkg in ipairs(extra_packages) do
            -- Filter by build features would go here
            table.insert(packages, pkg)
        end

        -- Source package if requested
        if get_config("include_sources") then
            table.insert(packages, "haiku_source")
        end
    end

    -- WebPositive if available
    if IsBuildFeatureEnabled and IsBuildFeatureEnabled("webpositive") then
        table.insert(packages, "webpositive")
    end

    -- Convert to .hpkg file names
    local package_files = {}
    for _, pkg in ipairs(packages) do
        table.insert(package_files, pkg .. ".hpkg")
    end

    -- Build repository directory
    local buildir = get_config("buildir") or "$(buildir)"
    local repos_dir = path.join(buildir, "repositories", architecture)
    local repository = path.join(repos_dir, "Haiku")

    -- Create repository
    RepositoryRules.HaikuRepository(repository, repo_info, package_files)

    return repository
end

-- ============================================================================
-- Package Definitions
-- ============================================================================

-- These are the packages built from Haiku source
HAIKU_REPOSITORY_PACKAGES = {
    -- Core system
    haiku = {
        description = "The Haiku operating system",
        provides = {"haiku", "cmd:sh", "cmd:bash"},
    },

    haiku_devel = {
        description = "Haiku development files",
        provides = {"haiku_devel"},
        requires = {"haiku"},
    },

    haiku_loader = {
        description = "Haiku boot loader",
        provides = {"haiku_loader"},
    },

    haiku_datatranslators = {
        description = "Haiku data translators",
        provides = {"haiku_datatranslators"},
        requires = {"haiku"},
    },

    -- Extras
    haiku_extras = {
        description = "Extra Haiku applications",
        provides = {"haiku_extras"},
        requires = {"haiku"},
    },

    makefile_engine = {
        description = "Makefile engine for building Haiku applications",
        provides = {"makefile_engine"},
    },

    -- Filesystems
    netfs = {
        description = "Network filesystem",
        provides = {"netfs"},
        requires = {"haiku", "userland_fs"},
    },

    userland_fs = {
        description = "Userland filesystem support",
        provides = {"userland_fs"},
        requires = {"haiku"},
    },

    -- Source
    haiku_source = {
        description = "Haiku source code",
        provides = {"haiku_source"},
    },

    -- WebPositive browser
    webpositive = {
        description = "WebPositive web browser",
        provides = {"webpositive", "cmd:WebPositive"},
        requires = {"haiku", "haikuwebkit"},
    },
}