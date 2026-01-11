--[[
    OptionalPackages.lua - Optional package definitions for Haiku image

    This file defines the optional packages that can be added to the Haiku image.
    It is equivalent to build/jam/OptionalPackages.

    Available Optional Packages:
        BeOSCompatibility       - creates links within the system to support old apps
        Development             - more complete dev environment (including autotools)
        DevelopmentBase         - basic development environment (gcc, headers, libs,...)
        DevelopmentMin          - development headers, libs, tools, from sources only
        Git                     - the distributed version control system
        WebPositive             - native, WebKit-based web browser
        Welcome                 - introductory documentation to Haiku
]]

-- ============================================================================
-- Optional Package Registry
-- ============================================================================

-- Set of optional packages that have been added to the image
OPTIONAL_PACKAGES_ADDED = OPTIONAL_PACKAGES_ADDED or {}

-- Dependencies between optional packages
OPTIONAL_PACKAGE_DEPENDENCIES = {
    Development = { "DevelopmentBase" },
    DevelopmentBase = { "DevelopmentMin" },
    DevelopmentPowerPC = { "DevelopmentMin" },
    NetFS = { "UserlandFS" },
}

-- ============================================================================
-- Optional Package Helper Functions
-- ============================================================================

-- Add an optional package to the image
function AddOptionalPackage(package_name)
    OPTIONAL_PACKAGES_ADDED[package_name] = true

    -- Also add dependencies
    local deps = OPTIONAL_PACKAGE_DEPENDENCIES[package_name]
    if deps then
        for _, dep in ipairs(deps) do
            AddOptionalPackage(dep)
        end
    end
end

-- Check if an optional package is added
function IsOptionalPackageAdded(package_name)
    return OPTIONAL_PACKAGES_ADDED[package_name] == true
end

-- Register optional package dependencies
function OptionalPackageDependencies(package_name, dependencies)
    OPTIONAL_PACKAGE_DEPENDENCIES[package_name] = dependencies
end

-- ============================================================================
-- Optional Package Definitions
-- ============================================================================

-- Define all optional packages with their contents
OPTIONAL_PACKAGE_DEFINITIONS = {}

-- ----------------------------------------------------------------------------
-- Haiku Sources
-- ----------------------------------------------------------------------------
OPTIONAL_PACKAGE_DEFINITIONS["HaikuSources"] = function(context)
    local image = context.image
    local include_sources = get_config("include_sources") or false

    if include_sources then
        AddPackageFilesToHaikuImage(image, "_sources_", {
            "haiku_source.hpkg",
        }, { nameFromMetaInfo = true })
    end
end

-- ----------------------------------------------------------------------------
-- BeBook
-- ----------------------------------------------------------------------------
OPTIONAL_PACKAGE_DEFINITIONS["BeBook"] = function(context)
    local image = context.image

    -- Add be_book system package
    AddHaikuImageSystemPackages(image, { "be_book" })

    -- Create desktop symlink to BeBook
    AddSymlinkToHaikuImage(image, "home/Desktop", {
        target = "/boot/system/documentation/BeBook/index.html",
        name = "BeBook",
    })
end

-- ----------------------------------------------------------------------------
-- BeOSCompatibility
-- ----------------------------------------------------------------------------
OPTIONAL_PACKAGE_DEFINITIONS["BeOSCompatibility"] = function(context)
    local image = context.image
    local target_arch = context.architecture or get_config("target_arch") or "x86_64"

    -- Only available for x86
    if target_arch ~= "x86" then
        cprint("${yellow}Warning: No optional package BeOSCompatibility available for %s${clear}", target_arch)
        return
    end

    cprint("${yellow}Warning: Adding BeOS compatibility symlinks. This will go away. Please fix your apps!${clear}")

    -- Create /beos symlinks pointing to system directories
    local beos_symlinks = {
        { dir = "beos", target = "../system/apps" },
        { dir = "beos", target = "../system/bin" },
        { dir = "beos", target = "../system/documentation" },
        { dir = "beos", target = "../system/settings/etc" },
        { dir = "beos", target = "../system/preferences" },
        { dir = "beos", target = "../system" },
    }

    for _, link in ipairs(beos_symlinks) do
        AddSymlinkToHaikuImage(image, link.dir, {
            target = link.target,
        })
    end

    -- Create /var directory and symlinks
    AddDirectoryToHaikuImage(image, "var")

    AddSymlinkToHaikuImage(image, "var", {
        target = "/boot/system/var/log",
        name = "log",
    })

    AddSymlinkToHaikuImage(image, "var", {
        target = "/boot/system/cache/tmp",
        name = "tmp",
    })
end

-- ----------------------------------------------------------------------------
-- Development (full development environment)
-- ----------------------------------------------------------------------------
OPTIONAL_PACKAGE_DEFINITIONS["Development"] = function(context)
    local image = context.image

    -- Autotools
    AddHaikuImageDisabledPackages(image, {
        "autoconf",
        "automake",
        "texinfo",
    })
    AddHaikuImageSourcePackages(image, {
        "autoconf",
        "automake",
        "texinfo",
    })

    -- Other build tools
    AddHaikuImageDisabledPackages(image, { "pkgconfig" })
    AddHaikuImageSourcePackages(image, { "pkgconfig" })

    -- Devel packages for base set (for all architectures)
    ForEachArchitecture(function(arch)
        AddHaikuImageDisabledPackages(image, {
            "openssl3_devel",
            "libjpeg_turbo_devel",
            "libpng16_devel",
            "zlib_devel",
            "zstd_devel",
        }, { architecture = arch })
    end)
end

-- ----------------------------------------------------------------------------
-- DevelopmentBase (basic development environment)
-- ----------------------------------------------------------------------------
OPTIONAL_PACKAGE_DEFINITIONS["DevelopmentBase"] = function(context)
    local image = context.image

    -- GCC and binutils (for all target architectures)
    ForEachArchitecture(function(arch)
        AddHaikuImageDisabledPackages(image, {
            "binutils",
            "gcc",
            "mpc",
            "mpfr",
        }, { architecture = arch })

        -- GMP only for non-gcc2
        if arch ~= "x86_gcc2" then
            AddHaikuImageSystemPackages(image, { "gmp" }, { architecture = arch })
        end

        AddHaikuImageSourcePackages(image, {
            "binutils",
            "gcc",
            "mpc",
            "mpfr",
        }, { architecture = arch })

        if arch ~= "x86_gcc2" then
            AddHaikuImageSourcePackages(image, { "gmp" }, { architecture = arch })
        end
    end)

    -- Other commonly used tools
    local common_tools = {
        "bison",
        "cdrtools",
        "flex",
        "jam",
        "make",
        "mawk",
        "mkdepend",
        "nasm",
        "patch",
    }
    AddHaikuImageDisabledPackages(image, common_tools)

    -- m4 for non-gcc2
    local target_arch = context.architecture or get_config("target_arch") or "x86_64"
    if target_arch ~= "x86_gcc2" then
        AddHaikuImageDisabledPackages(image, { "m4" })
    end

    -- Secondary x86 packages
    local secondary_arch = get_config("secondary_arch")
    if secondary_arch == "x86" then
        AddHaikuImageDisabledPackages(image, { "m4_x86", "nasm_x86" })
        AddHaikuImageSourcePackages(image, { "m4_x86" })
    end

    -- Source packages
    AddHaikuImageSourcePackages(image, {
        "bison",
        "cdrtools",
        "make",
        "patch",
    })

    if target_arch ~= "x86_gcc2" then
        AddHaikuImageSourcePackages(image, { "m4" })
    end
end

-- ----------------------------------------------------------------------------
-- DevelopmentMin (minimal development from sources)
-- ----------------------------------------------------------------------------
OPTIONAL_PACKAGE_DEFINITIONS["DevelopmentMin"] = function(context)
    local image = context.image
    local target_arch = context.architecture or get_config("target_arch") or "x86_64"

    -- Only for x86 and x86_64
    if target_arch ~= "x86" and target_arch ~= "x86_64" then
        return
    end

    -- Get packaging architectures
    local packaging_archs = GetPackagingArchitectures()

    -- Add haiku_devel package
    AddPackageFilesToHaikuImage(image, "_packages_", {
        "haiku_devel.hpkg",
    }, { nameFromMetaInfo = true })

    -- Add secondary architecture devel packages
    for i = 2, #packaging_archs do
        local arch = packaging_archs[i]
        AddPackageFilesToHaikuImage(image, "_packages_", {
            "haiku_" .. arch .. "_devel.hpkg",
        }, { nameFromMetaInfo = true })
    end

    -- Non-bootstrap additions
    local is_bootstrap = get_config("bootstrap") or false
    if not is_bootstrap then
        AddPackageFilesToHaikuImage(image, "_packages_", {
            "makefile_engine.hpkg",
        }, { nameFromMetaInfo = true })

        AddHaikuImageDisabledPackages(image, { "make", "mkdepend" })
        AddHaikuImageSourcePackages(image, { "make" })
    end
end

-- ----------------------------------------------------------------------------
-- Git
-- ----------------------------------------------------------------------------
OPTIONAL_PACKAGE_DEFINITIONS["Git"] = function(context)
    local image = context.image

    -- Git and Perl packages
    AddHaikuImageSystemPackages(image, { "git", "perl" })
    AddHaikuImageSourcePackages(image, { "git", "perl" })

    -- Secondary x86 packages
    local secondary_arch = get_config("secondary_arch")
    if secondary_arch == "x86" then
        AddHaikuImageSystemPackages(image, { "git_x86", "perl_x86" })
        AddHaikuImageSourcePackages(image, { "git_x86", "perl_x86" })
    end
end

-- ----------------------------------------------------------------------------
-- WebPositive
-- ----------------------------------------------------------------------------
OPTIONAL_PACKAGE_DEFINITIONS["WebPositive"] = function(context)
    local image = context.image

    -- Check if webpositive feature is enabled for any architecture
    local webpositive_enabled = false

    ForEachArchitecture(function(arch)
        if IsBuildFeatureEnabled("webpositive", arch) then
            webpositive_enabled = true
            return false  -- break
        end
    end)

    if webpositive_enabled then
        AddPackageFilesToHaikuImage(image, "system/packages", {
            "webpositive.hpkg",
        }, { nameFromMetaInfo = true })
    end
end

-- ----------------------------------------------------------------------------
-- Welcome (user documentation)
-- ----------------------------------------------------------------------------
OPTIONAL_PACKAGE_DEFINITIONS["Welcome"] = function(context)
    local image = context.image

    -- User guide packages for all supported languages
    local userguide_packages = {
        "haiku_userguide_ca",
        "haiku_userguide_de",
        "haiku_userguide_en",
        "haiku_userguide_es",
        "haiku_userguide_fi",
        "haiku_userguide_fur",
        "haiku_userguide_fr",
        "haiku_userguide_hu",
        "haiku_userguide_id",
        "haiku_userguide_jp",
        "haiku_userguide_pl",
        "haiku_userguide_pt_br",
        "haiku_userguide_pt_pt",
        "haiku_userguide_ro",
        "haiku_userguide_ru",
        "haiku_userguide_sk",
        "haiku_userguide_sv_se",
        "haiku_userguide_tr",
        "haiku_userguide_uk",
        "haiku_userguide_zh_cn",
        "haiku_welcome",
    }

    AddHaikuImageSystemPackages(image, userguide_packages)

    -- Desktop symlinks
    AddSymlinkToHaikuImage(image, "home/Desktop", {
        target = "/boot/system/bin/quicktour",
        name = "Quick Tour",
    })

    AddSymlinkToHaikuImage(image, "home/Desktop", {
        target = "/boot/system/bin/userguide",
        name = "User Guide",
    })
end

-- ============================================================================
-- Image Helper Functions (stubs - implement in ImageRules.lua)
-- ============================================================================

-- These functions should be implemented in ImageRules.lua or as part of image creation

-- Add system packages to the image
function AddHaikuImageSystemPackages(image, packages, options)
    options = options or {}
    image = image or CURRENT_IMAGE

    if not HAIKU_IMAGE_SYSTEM_PACKAGES then
        HAIKU_IMAGE_SYSTEM_PACKAGES = {}
    end

    for _, pkg in ipairs(packages) do
        local pkg_entry = {
            name = pkg,
            architecture = options.architecture,
        }
        table.insert(HAIKU_IMAGE_SYSTEM_PACKAGES, pkg_entry)
    end
end

-- Add disabled packages to the image
function AddHaikuImageDisabledPackages(image, packages, options)
    options = options or {}
    image = image or CURRENT_IMAGE

    if not HAIKU_IMAGE_DISABLED_PACKAGES then
        HAIKU_IMAGE_DISABLED_PACKAGES = {}
    end

    for _, pkg in ipairs(packages) do
        local pkg_entry = {
            name = pkg,
            architecture = options.architecture,
        }
        table.insert(HAIKU_IMAGE_DISABLED_PACKAGES, pkg_entry)
    end
end

-- Add source packages to the image
function AddHaikuImageSourcePackages(image, packages, options)
    options = options or {}
    image = image or CURRENT_IMAGE

    if not HAIKU_IMAGE_SOURCE_PACKAGES then
        HAIKU_IMAGE_SOURCE_PACKAGES = {}
    end

    for _, pkg in ipairs(packages) do
        local pkg_entry = {
            name = pkg,
            architecture = options.architecture,
        }
        table.insert(HAIKU_IMAGE_SOURCE_PACKAGES, pkg_entry)
    end
end

-- Add package files to the image
function AddPackageFilesToHaikuImage(image, location, packages, options)
    options = options or {}
    image = image or CURRENT_IMAGE

    if not HAIKU_IMAGE_PACKAGE_FILES then
        HAIKU_IMAGE_PACKAGE_FILES = {}
    end

    for _, pkg in ipairs(packages) do
        local pkg_entry = {
            name = pkg,
            location = location,
            nameFromMetaInfo = options.nameFromMetaInfo or false,
        }
        table.insert(HAIKU_IMAGE_PACKAGE_FILES, pkg_entry)
    end
end

-- Add symlink to the image
function AddSymlinkToHaikuImage(image, directory, options)
    image = image or CURRENT_IMAGE

    if not HAIKU_IMAGE_SYMLINKS then
        HAIKU_IMAGE_SYMLINKS = {}
    end

    local symlink_entry = {
        directory = directory,
        target = options.target,
        name = options.name,
    }
    table.insert(HAIKU_IMAGE_SYMLINKS, symlink_entry)
end

-- Add directory to the image
function AddDirectoryToHaikuImage(image, directory)
    image = image or CURRENT_IMAGE

    if not HAIKU_IMAGE_DIRECTORIES then
        HAIKU_IMAGE_DIRECTORIES = {}
    end

    table.insert(HAIKU_IMAGE_DIRECTORIES, directory)
end

-- ============================================================================
-- Architecture Helper Functions
-- ============================================================================

-- Iterate over all build architectures
function ForEachArchitecture(callback)
    local target_arch = get_config("target_arch") or "x86_64"
    local secondary_arch = get_config("secondary_arch")

    -- Primary architecture
    local result = callback(target_arch)
    if result == false then
        return
    end

    -- Secondary architecture
    if secondary_arch and secondary_arch ~= "" then
        callback(secondary_arch)
    end
end

-- Get list of packaging architectures
function GetPackagingArchitectures()
    local archs = {}
    local target_arch = get_config("target_arch") or "x86_64"
    local secondary_arch = get_config("secondary_arch")

    table.insert(archs, target_arch)

    if secondary_arch and secondary_arch ~= "" then
        table.insert(archs, secondary_arch)
    end

    return archs
end

-- Check if build feature is enabled
function IsBuildFeatureEnabled(feature, architecture)
    -- This should be implemented in BuildFeatures.lua
    -- Stub implementation
    if HAIKU_BUILD_FEATURES and HAIKU_BUILD_FEATURES[feature] then
        local feat = HAIKU_BUILD_FEATURES[feature]
        if type(feat) == "table" then
            return feat.enabled ~= false
        end
        return feat == true
    end
    return false
end

-- ============================================================================
-- Main Processing Function
-- ============================================================================

-- Process all added optional packages
function ProcessOptionalPackages(image_context)
    local context = image_context or {}
    context.image = context.image or CURRENT_IMAGE

    -- Process each added optional package
    for package_name, _ in pairs(OPTIONAL_PACKAGES_ADDED) do
        local definition = OPTIONAL_PACKAGE_DEFINITIONS[package_name]
        if definition then
            cprint("${dim}Processing optional package: %s${clear}", package_name)
            definition(context)
        else
            cprint("${yellow}Warning: Unknown optional package: %s${clear}", package_name)
        end
    end
end

-- ============================================================================
-- Package List for Configuration Menu
-- ============================================================================

-- Available optional packages with descriptions
AVAILABLE_OPTIONAL_PACKAGES = {
    {
        name = "BeBook",
        description = "The BeOS API documentation",
    },
    {
        name = "BeOSCompatibility",
        description = "Creates symlinks for BeOS app compatibility (x86 only)",
    },
    {
        name = "Development",
        description = "Full development environment including autotools",
        dependencies = { "DevelopmentBase" },
    },
    {
        name = "DevelopmentBase",
        description = "Basic development environment (gcc, headers, libs)",
        dependencies = { "DevelopmentMin" },
    },
    {
        name = "DevelopmentMin",
        description = "Minimal development headers, libs, tools from sources",
    },
    {
        name = "Git",
        description = "The Git distributed version control system",
    },
    {
        name = "HaikuSources",
        description = "Haiku source code package",
    },
    {
        name = "WebPositive",
        description = "Native WebKit-based web browser",
    },
    {
        name = "Welcome",
        description = "Introductory documentation and user guides",
    },
}

-- Print available optional packages
function PrintAvailableOptionalPackages()
    print("\nAvailable Optional Packages:")
    print(string.rep("-", 60))

    for _, pkg in ipairs(AVAILABLE_OPTIONAL_PACKAGES) do
        local deps_str = ""
        if pkg.dependencies then
            deps_str = " (requires: " .. table.concat(pkg.dependencies, ", ") .. ")"
        end
        printf("  %-20s - %s%s", pkg.name, pkg.description, deps_str)
    end

    print("")
end