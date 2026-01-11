--[[
    BuildFeatures.lua - External package dependencies

    xmake equivalent of build/jam/BuildFeatures

    This module defines setup for build features that require external packages.
    It is processed for each configured packaging architecture.

    Each build feature specifies:
    - Package name(s) to check for availability
    - Runtime libraries
    - Development libraries
    - Header directories
]]

-- ============================================================================
-- Build Feature Definitions
-- ============================================================================

--[[
    BUILD_FEATURES table defines all external package dependencies.

    Structure:
    {
        feature_name = {
            packages = {"pkg_devel", ...},    -- Packages to check (in priority order)
            base_package = "pkg",             -- Runtime package name
            libraries = {"libfoo.so", ...},   -- Libraries in developLibDir
            headers = {"dir1", "dir2"},       -- Header dirs (relative to developHeadersDir)
            static_only = false,              -- true if only static libs
            depends_on = {"other_feature"},   -- Other features this depends on
            arch_specific = {...},            -- Per-architecture overrides
        }
    }
]]

BUILD_FEATURES = {
    -- ========================================================================
    -- SSL (OpenSSL)
    -- ========================================================================
    openssl = {
        packages = {"openssl3_devel"},
        base_package = "openssl3",
        libraries = {"libcrypto.so", "libssl.so"},
        headers = {},
        auto_enable_with = {"openssl3"},  -- Auto-enable when this image package is added
    },

    -- ========================================================================
    -- GCC System Libraries
    -- ========================================================================
    gcc_syslibs = {
        packages = {"gcc_syslibs"},
        libraries_in_lib = {  -- In lib/ instead of develop/lib/
            "libgcc_s.so.1",
            "libstdc++.so",
            "libsupc++.so",
        },
    },

    gcc_syslibs_devel = {
        packages = {"gcc_syslibs_devel"},
        libraries = {
            "libgcc.a",
            "libgcc_eh.a",
            "libgcc-kernel.a",
            "libgcc_eh-kernel.a",  -- Actually libgcc_eh.a
            "libgcc-boot.a",
            "libgcc_eh-boot.a",
            "libstdc++.a",
            "libsupc++.a",
            "libsupc++-kernel.a",
            "libsupc++-boot.a",
        },
        headers = {"c++", "gcc"},
    },

    -- ========================================================================
    -- ICU (International Components for Unicode)
    -- ========================================================================
    icu = {
        packages = {"icu_devel", "icu74_devel", "icu70_devel"},  -- Priority order
        base_packages = {"icu", "icu74", "icu70"},               -- Corresponding base packages
        libraries = {
            "libicudata.so",
            "libicui18n.so",
            "libicuio.so",
            "libicuuc.so",
        },
        headers = {},
    },

    -- ========================================================================
    -- Image Format Libraries
    -- ========================================================================
    giflib = {
        packages = {"giflib_devel"},
        base_package = "giflib",
        libraries = {"libgif.so.7"},
        headers = {},
    },

    libpng = {
        packages = {"libpng16_devel"},
        base_package = "libpng16",
        libraries = {"libpng16.so"},
        headers = {},
    },

    jpeg = {
        packages = {"libjpeg_turbo_devel"},
        base_package = "libjpeg_turbo",
        libraries = {"libjpeg.so"},
        headers = {},
    },

    tiff = {
        packages = {"tiff_devel"},
        base_package = "tiff",
        libraries = {"libtiff.so"},
        headers = {"tiff"},
    },

    jasper = {
        packages = {"jasper_devel"},
        base_package = "jasper",
        libraries = {"libjasper.so.4"},
        headers = {"jasper"},
    },

    libicns = {
        packages = {"libicns_devel"},
        base_package = "libicns",
        libraries = {"libicns.so.1"},
        headers = {},
    },

    libraw = {
        packages = {"libraw_devel"},
        base_package = "libraw",
        libraries = {"libraw.so"},
        headers = {},
    },

    libwebp = {
        packages = {"libwebp_devel"},
        base_package = "libwebp",
        libraries = {"libwebp.so.7"},  -- Default version
        headers = {"webp"},
        arch_specific = {
            x86 = {
                libraries = {"libwebp.so.6"},
            },
        },
    },

    libavif = {
        packages = {"libavif1.0_devel"},
        base_package = "libavif1.0",
        libraries = {"libavif.so"},
        headers = {"avif"},
    },

    -- ========================================================================
    -- Graphics Libraries
    -- ========================================================================
    glu = {
        packages = {"glu_devel"},
        base_package = "glu",
        libraries = {"libGLU.so"},
        headers = {},
    },

    mesa = {
        packages = {"mesa_devel"},
        base_package = "mesa",
        libraries = {"libGL.so"},
        headers = {"os/opengl"},
    },

    -- ========================================================================
    -- Font Libraries
    -- ========================================================================
    freetype = {
        packages = {"freetype_devel"},
        base_package = "freetype",
        libraries = {"libfreetype.so"},
        headers = {"freetype2"},
    },

    fontconfig = {
        packages = {"fontconfig_devel"},
        base_package = "fontconfig",
        libraries = {"libfontconfig.so"},
        headers = {"fontconfig"},
    },

    -- ========================================================================
    -- Multimedia Libraries
    -- ========================================================================
    ffmpeg = {
        packages = {"ffmpeg6_devel"},
        base_package = "ffmpeg6",
        libraries = {
            "libavformat.so",
            "libavcodec.so",
            "libavfilter.so",
            "libswscale.so",
            "libavutil.so",
            "libswresample.so",
        },
        headers = {},
    },

    fluidlite = {
        packages = {"fluidlite_devel"},
        libraries = {"libfluidlite-static.a"},
        headers = {},
        static_only = true,
        depends_on = {"libvorbis"},
    },

    libvorbis = {
        packages = {"libvorbis_devel"},
        base_package = "libvorbis",
        libraries = {"libvorbisfile.so.3"},
        headers = {},
    },

    live555 = {
        packages = {"live555_devel"},
        libraries = {
            "libliveMedia.a",
            "libBasicUsageEnvironment.a",
            "libgroupsock.a",
            "libUsageEnvironment.a",
        },
        headers = {
            "liveMedia",
            "BasicUsageEnvironment",
            "groupsock",
            "UsageEnvironment",
        },
        static_only = true,
    },

    libdvdread = {
        packages = {"libdvdread_devel"},
        base_package = "libdvdread",
        libraries = {"libdvdread.so.4"},
        headers = {},
    },

    libdvdnav = {
        packages = {"libdvdnav_devel"},
        base_package = "libdvdnav",
        libraries = {"libdvdnav.so.4"},
        headers = {},
    },

    -- ========================================================================
    -- Compression Libraries
    -- ========================================================================
    zlib = {
        packages = {"zlib_devel"},
        base_package = "zlib",
        libraries = {"libz.so"},
        headers = {},
        has_sources = true,  -- Sources required for primary architecture
        source_package = "zlib_source",
    },

    zstd = {
        packages = {"zstd_devel"},
        base_package = "zstd",
        libraries = {"libzstd.so"},
        headers = {},
        has_sources = true,
        source_package = "zstd_source",
    },

    -- ========================================================================
    -- Text/Terminal Libraries
    -- ========================================================================
    ncurses = {
        packages = {"ncurses6_devel"},
        base_package = "ncurses6",
        libraries = {"libncurses.so.6"},
        headers = {},
    },

    libedit = {
        packages = {"libedit_devel"},
        base_package = "libedit",
        libraries = {"libedit.so"},
        headers = {},
    },

    expat = {
        packages = {"expat_devel"},
        base_package = "expat",
        libraries = {"libexpat.so.1"},
        headers = {},
    },

    -- ========================================================================
    -- Kernel/Boot Libraries
    -- ========================================================================
    libqrencode_kdl = {
        packages = {"qrencode_kdl_devel"},
        libraries = {"libqrencode_kdl.a"},
        headers = {},
        static_only = true,
    },

    -- ========================================================================
    -- Printing
    -- ========================================================================
    gutenprint = {
        packages = {"gutenprint9_devel"},
        base_package = "gutenprint9",
        libraries = {"libgutenprint.so"},
        headers = {"gutenprint"},
    },

    -- ========================================================================
    -- Web/Browser
    -- ========================================================================
    webkit = {
        packages = {"haikuwebkit_devel"},
        base_package = "haikuwebkit",
        libraries = {"libWebKitLegacy.so"},
        headers = {},
    },
}

-- ============================================================================
-- Helper Functions
-- ============================================================================

--[[
    GetLibDir(architecture, is_primary)

    Returns the library directory path for an architecture.
]]
function GetLibDir(architecture, is_primary)
    if is_primary then
        return "lib"
    else
        return "lib/" .. architecture
    end
end

--[[
    GetDevelopLibDir(architecture, is_primary)

    Returns the development library directory path.
]]
function GetDevelopLibDir(architecture, is_primary)
    if is_primary then
        return "develop/lib"
    else
        return "develop/lib/" .. architecture
    end
end

--[[
    GetDevelopHeadersDir(architecture, is_primary)

    Returns the development headers directory path.
]]
function GetDevelopHeadersDir(architecture, is_primary)
    if is_primary then
        return "develop/headers"
    else
        return "develop/headers/" .. architecture
    end
end

--[[
    GetBuildFeatureInfo(feature_name, architecture)

    Returns information about a build feature for the specified architecture.

    Returns table with:
        - available: boolean
        - package: string (the available package name)
        - base_package: string (runtime package)
        - libraries: table of library paths
        - headers: table of header paths
]]
function GetBuildFeatureInfo(feature_name, architecture)
    local feature = BUILD_FEATURES[feature_name]
    if not feature then
        return {available = false}
    end

    local is_primary = (architecture == get_config("arch"))
    local lib_dir = GetLibDir(architecture, is_primary)
    local develop_lib_dir = GetDevelopLibDir(architecture, is_primary)
    local develop_headers_dir = GetDevelopHeadersDir(architecture, is_primary)

    -- Check package availability
    local available_package = nil
    local base_package = nil

    for i, pkg in ipairs(feature.packages or {}) do
        if IsPackageAvailable(pkg) then
            available_package = pkg
            if feature.base_packages then
                base_package = feature.base_packages[i]
            else
                base_package = feature.base_package
            end
            break
        end
    end

    if not available_package then
        return {available = false, feature = feature_name}
    end

    -- Check dependencies
    if feature.depends_on then
        for _, dep in ipairs(feature.depends_on) do
            local dep_info = GetBuildFeatureInfo(dep, architecture)
            if not dep_info.available then
                return {available = false, feature = feature_name, missing_dep = dep}
            end
        end
    end

    -- Build library paths
    local libraries = {}

    -- Architecture-specific overrides
    local arch_feature = feature
    if feature.arch_specific and feature.arch_specific[architecture] then
        arch_feature = table.clone(feature)
        for k, v in pairs(feature.arch_specific[architecture]) do
            arch_feature[k] = v
        end
    end

    -- Libraries in develop/lib
    for _, lib in ipairs(arch_feature.libraries or {}) do
        table.insert(libraries, path.join(develop_lib_dir, lib))
    end

    -- Libraries directly in lib (e.g., gcc_syslibs)
    for _, lib in ipairs(arch_feature.libraries_in_lib or {}) do
        table.insert(libraries, path.join(lib_dir, lib))
    end

    -- Build header paths
    local headers = {develop_headers_dir}
    for _, hdr in ipairs(arch_feature.headers or {}) do
        table.insert(headers, path.join(develop_headers_dir, hdr))
    end

    return {
        available = true,
        feature = feature_name,
        package = available_package,
        base_package = base_package,
        libraries = libraries,
        headers = headers,
        static_only = arch_feature.static_only,
        has_sources = arch_feature.has_sources,
        source_package = arch_feature.source_package,
    }
end

--[[
    InitializeBuildFeatures(architecture)

    Initialize build features for the specified architecture.
    Returns table of enabled features and unavailable features.
]]
function InitializeBuildFeatures(architecture)
    local enabled = {}
    local unavailable = {}

    for name, _ in pairs(BUILD_FEATURES) do
        local info = GetBuildFeatureInfo(name, architecture)
        if info.available then
            table.insert(enabled, name)
            EnableBuildFeatures(name)
        else
            table.insert(unavailable, name)
        end
    end

    -- Report unavailable features
    if #unavailable > 0 then
        print("Build-feature packages unavailable on " .. architecture .. ":")
        for _, name in ipairs(unavailable) do
            print("  " .. name)
        end
    end

    return {
        enabled = enabled,
        unavailable = unavailable,
    }
end

-- ============================================================================
-- SSL Auto-Enable Logic
-- ============================================================================

--[[
    CheckSSLAutoEnable()

    Automatically enable SSL feature when OpenSSL package is in image.
]]
function CheckSSLAutoEnable()
    if IsHaikuImagePackageAdded("openssl3") then
        HAIKU_BUILD_FEATURE_SSL = true
    end
end

-- ============================================================================
-- Feature Queries
-- ============================================================================

--[[
    IsBuildFeatureEnabled(feature_name)

    Check if a build feature is enabled.
]]
function IsBuildFeatureEnabled(feature_name)
    -- Check global enabled features table
    return ENABLED_BUILD_FEATURES and ENABLED_BUILD_FEATURES[feature_name]
end

--[[
    GetBuildFeatureLibraries(feature_name, architecture)

    Get the library paths for a build feature.
]]
function GetBuildFeatureLibraries(feature_name, architecture)
    local info = GetBuildFeatureInfo(feature_name, architecture or get_config("arch"))
    return info.libraries or {}
end

--[[
    GetBuildFeatureHeaders(feature_name, architecture)

    Get the header paths for a build feature.
]]
function GetBuildFeatureHeaders(feature_name, architecture)
    local info = GetBuildFeatureInfo(feature_name, architecture or get_config("arch"))
    return info.headers or {}
end

-- ============================================================================
-- Global State
-- ============================================================================

-- Track enabled build features globally
ENABLED_BUILD_FEATURES = ENABLED_BUILD_FEATURES or {}

--[[
    Helper to enable a build feature (called by BuildFeatureRules)
]]
function MarkBuildFeatureEnabled(feature_name)
    ENABLED_BUILD_FEATURES[feature_name] = true
end