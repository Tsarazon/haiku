--[[
    DefaultBuildProfiles.lua - Default build profile definitions

    xmake equivalent of build/jam/DefaultBuildProfiles

    This module defines:
    - Build type detection (bootstrap, minimum, regular)
    - Default build profiles (release, nightly, bootstrap, minimum)
    - Profile-specific package lists and settings
]]

-- ============================================================================
-- Build Type Detection
-- ============================================================================

--[[
    DetectBuildType(profile)

    Determines the build type from the profile name.
    Returns: "bootstrap", "minimum", or "regular"
]]
function DetectBuildType(profile)
    if not profile then
        return "regular"
    end

    if profile:match("^bootstrap%-") then
        return "bootstrap"
    elseif profile:match("^minimum%-") then
        return "minimum"
    else
        return "regular"
    end
end

--[[
    InitializeBuildType(profile)

    Sets up build type variables and enables appropriate features.
]]
function InitializeBuildType(profile)
    HAIKU_BUILD_TYPE = DetectBuildType(profile)

    if HAIKU_BUILD_TYPE == "bootstrap" then
        EnableBuildFeatures("bootstrap_image")
        -- Add pseudo target for bootstrap stage0
        -- NotFile equivalent handled by xmake's phony targets

        HAIKU_DEFINES = HAIKU_DEFINES or {}
        table.insert(HAIKU_DEFINES, "HAIKU_BOOTSTRAP_BUILD")
        TARGET_DEFINES = TARGET_DEFINES or {}
        table.insert(TARGET_DEFINES, "HAIKU_BOOTSTRAP_BUILD")

    elseif HAIKU_BUILD_TYPE == "minimum" then
        EnableBuildFeatures("minimum_image")

        HAIKU_DEFINES = HAIKU_DEFINES or {}
        table.insert(HAIKU_DEFINES, "HAIKU_MINIMUM_BUILD")
        TARGET_DEFINES = TARGET_DEFINES or {}
        table.insert(TARGET_DEFINES, "HAIKU_MINIMUM_BUILD")

    else  -- regular
        EnableBuildFeatures("regular_image")

        HAIKU_DEFINES = HAIKU_DEFINES or {}
        table.insert(HAIKU_DEFINES, "HAIKU_REGULAR_BUILD")
        TARGET_DEFINES = TARGET_DEFINES or {}
        table.insert(TARGET_DEFINES, "HAIKU_REGULAR_BUILD")
    end

    print("Starting build of type " .. HAIKU_BUILD_TYPE .. " ...")
end

-- ============================================================================
-- Default Build Profiles
-- ============================================================================

DEFAULT_BUILD_PROFILES = {
    -- Release profiles
    ["release-raw"] = {
        type = "image",
        output = "haiku-release.image",
    },
    ["release-vmware"] = {
        type = "vmware-image",
        output = "haiku-release.vmdk",
    },
    ["release-cd"] = {
        type = "cd-image",
        output = "haiku-release.iso",
    },
    ["release-anyboot"] = {
        type = "anyboot-image",
        output = "haiku-release-anyboot.iso",
    },

    -- Nightly profiles
    ["nightly-raw"] = {
        type = "image",
        output = "haiku-nightly.image",
    },
    ["nightly-mmc"] = {
        type = "haiku-mmc-image",
        output = "haiku-nightly.mmc",
    },
    ["nightly-vmware"] = {
        type = "vmware-image",
        output = "haiku-nightly.vmdk",
    },
    ["nightly-cd"] = {
        type = "cd-image",
        output = "haiku-nightly.iso",
    },
    ["nightly-anyboot"] = {
        type = "anyboot-image",
        output = "haiku-nightly-anyboot.iso",
    },

    -- Bootstrap profiles
    ["bootstrap-raw"] = {
        type = "image",
        output = "haiku-bootstrap.image",
    },
    ["bootstrap-mmc"] = {
        type = "haiku-mmc-image",
        output = "haiku-bootstrap.mmc",
    },
    ["bootstrap-vmware"] = {
        type = "vmware-image",
        output = "haiku-bootstrap.vmdk",
    },
    ["bootstrap-anyboot"] = {
        type = "anyboot-image",
        output = "haiku-bootstrap-anyboot.iso",
    },

    -- Minimum profiles
    ["minimum-raw"] = {
        type = "image",
        output = "haiku-minimum.image",
    },
    ["minimum-mmc"] = {
        type = "haiku-mmc-image",
        output = "haiku-minimum.mmc",
    },
    ["minimum-vmware"] = {
        type = "vmware-image",
        output = "haiku-minimum.vmdk",
    },
    ["minimum-cd"] = {
        type = "cd-image",
        output = "haiku-minimum.iso",
    },
    ["minimum-anyboot"] = {
        type = "anyboot-image",
        output = "haiku-minimum-anyboot.iso",
    },

    -- Obvious default profiles
    ["cd-image"] = {
        type = "cd-image",
        output = nil,  -- Use default
    },
    ["install"] = {
        type = "install",
        output = nil,
    },
    ["vmware-image"] = {
        type = "vmware-image",
        output = nil,
    },
}

-- ============================================================================
-- Profile-Specific Package Lists
-- ============================================================================

PROFILE_PACKAGES = {
    -- Release profile packages
    ["release"] = {
        system_packages = {
            "bepdf",
            "keymapswitcher",
            "mandoc",
            "noto",
            "noto_sans_cjk_jp",
            "openssh",
            "pdfwriter",
            "pe",
            "timgmsoundfont",
            "vision",
            "wonderbrush",
            "wpa_supplicant",
            "nano",
            "p7zip",
            "python3.10",
            "xz_utils",
        },
        source_packages = {
            "bepdf",
            "nano",
            "p7zip",
        },
        optional_packages = {
            "BeBook",
            "Development",
            "Git",
            "Welcome",
        },
        -- WebPositive added conditionally for x86/x86_64
        image_size = 800,
        image_size_with_debug = 1400,
        root_user_name = "user",
        root_user_real_name = "Yourself",
        host_name = "shredder",
        groups = {
            {name = "party", gid = 101, members = {"user", "sshd"}},
        },
    },

    -- Nightly profile packages
    ["nightly"] = {
        system_packages = {
            "mandoc",
            "noto",
            "openssh",
            "openssl3",
            "pe",
            "vision",
            "wpa_supplicant",
            "nano",
            "p7zip",
            "xz_utils",
        },
        source_packages = {
            "nano",
            "p7zip",
        },
        optional_packages = {
            "Development",
            "Git",
        },
        image_size = 650,
        image_size_with_debug = 850,
        root_user_name = "user",
        root_user_real_name = "Yourself",
        host_name = "shredder",
        groups = {
            {name = "party", gid = 101, members = {"user", "sshd"}},
        },
        nightly_build = true,
    },

    -- Minimum profile packages
    ["minimum"] = {
        system_packages = {
            "openssl3",
        },
        source_packages = {},
        optional_packages = {},
        image_size = nil,  -- Use default
        image_size_with_debug = 450,
        host_name = "shredder",
    },

    -- Bootstrap profile packages
    ["bootstrap"] = {
        system_packages = {
            "binutils",
            "bison",
            "expat",
            "flex",
            "gcc",
            "grep",
            "haikuporter",
            "less",
            "libedit",
            "make",
            "ncurses6",
            "noto",
            "python",
            "sed",
            "texinfo",
            "gawk",
        },
        disabled_packages = {
            "freetype_devel",
            "libedit_devel",
            "ncurses6_devel",
            "zlib_devel",
        },
        optional_packages = {
            "DevelopmentMin",
        },
        -- Secondary architecture packages
        secondary_arch_packages = {
            "binutils",
            "expat",
            "freetype",
            "gcc",
            "icu74",
            "libedit",
            "ncurses6",
            "zlib",
        },
        secondary_arch_disabled = {
            "freetype_devel",
            "libedit_devel",
            "ncurses6_devel",
            "zlib_devel",
        },
        image_size = 20000,
        host_name = "shredder",
    },
}

-- ============================================================================
-- Profile Configuration Functions
-- ============================================================================

--[[
    GetProfilePackageConfig(profile)

    Returns the package configuration for a profile type.
]]
function GetProfilePackageConfig(profile)
    if profile:match("^release%-") then
        return PROFILE_PACKAGES["release"]
    elseif profile:match("^nightly%-") then
        return PROFILE_PACKAGES["nightly"]
    elseif profile:match("^minimum%-") then
        return PROFILE_PACKAGES["minimum"]
    elseif profile:match("^bootstrap%-") then
        return PROFILE_PACKAGES["bootstrap"]
    end
    return nil
end

--[[
    ConfigureProfilePackages(profile, architectures)

    Configures packages for the specified build profile.
]]
function ConfigureProfilePackages(profile, architectures)
    local config = GetProfilePackageConfig(profile)
    if not config then
        return
    end

    -- Set image properties
    if config.root_user_name then
        HAIKU_ROOT_USER_NAME = HAIKU_ROOT_USER_NAME or config.root_user_name
    end
    if config.root_user_real_name then
        HAIKU_ROOT_USER_REAL_NAME = HAIKU_ROOT_USER_REAL_NAME or config.root_user_real_name
    end
    if config.host_name then
        HAIKU_IMAGE_HOST_NAME = HAIKU_IMAGE_HOST_NAME or config.host_name
    end

    -- Set image size
    local debug_level = get_config("debug_level") or 0
    if debug_level ~= 0 and config.image_size_with_debug then
        HAIKU_IMAGE_SIZE = HAIKU_IMAGE_SIZE or config.image_size_with_debug
    elseif config.image_size then
        HAIKU_IMAGE_SIZE = HAIKU_IMAGE_SIZE or config.image_size
    end

    -- Add groups
    if config.groups then
        for _, group in ipairs(config.groups) do
            AddGroupToHaikuImage(group.name, group.gid, group.members)
        end
    end

    -- Add system packages
    if config.system_packages then
        AddHaikuImageSystemPackages(config.system_packages)
    end

    -- Add source packages
    if config.source_packages then
        AddHaikuImageSourcePackages(config.source_packages)
    end

    -- Add disabled packages
    if config.disabled_packages then
        AddHaikuImageDisabledPackages(config.disabled_packages)
    end

    -- Add optional packages
    if config.optional_packages then
        AddOptionalHaikuImagePackages(config.optional_packages)
    end

    -- Handle WebPositive for release/nightly (only x86/x86_64)
    if profile:match("^release%-") or profile:match("^nightly%-") then
        local primary_arch = architectures and architectures[1]
        if primary_arch == "x86" or primary_arch == "x86_64" then
            AddOptionalHaikuImagePackages({"WebPositive"})
        else
            print("WebPositive not available on " .. tostring(primary_arch))
        end
    end

    -- Handle secondary architecture packages for bootstrap
    if config.secondary_arch_packages and architectures and #architectures > 1 then
        for i = 2, #architectures do
            -- Add packages for secondary architectures
            AddHaikuImageSystemPackages(config.secondary_arch_packages)
            if config.secondary_arch_disabled then
                AddHaikuImageDisabledPackages(config.secondary_arch_disabled)
            end
        end
    end

    -- Mark nightly build
    if config.nightly_build then
        HAIKU_NIGHTLY_BUILD = 1
    end

    -- Print build message
    if profile:match("^release%-") then
        print("Building Haiku R1/development preview")
    elseif profile:match("^nightly%-") then
        print("Building Haiku Nightly")
    elseif profile:match("^minimum%-") then
        print("Building Haiku Minimum Target")
    elseif profile:match("^bootstrap%-") then
        print("Building Haiku Bootstrap")
    end
end

-- ============================================================================
-- Profile Definition and Validation
-- ============================================================================

--[[
    DefineDefaultBuildProfiles()

    Defines all default build profiles.
]]
function DefineDefaultBuildProfiles()
    for name, config in pairs(DEFAULT_BUILD_PROFILES) do
        DefineBuildProfile(name, config.type, config.output)
    end
end

--[[
    ValidateAndDefineBuildProfile(profile)

    Validates a build profile and defines it if it's a known default.
    Returns true if profile is valid, false otherwise.
]]
function ValidateAndDefineBuildProfile(profile)
    if not profile then
        return true
    end

    -- Check if already defined
    if HAIKU_BUILD_PROFILE_DEFINED then
        return true
    end

    -- Check if it's a known default profile
    if DEFAULT_BUILD_PROFILES[profile] then
        local config = DEFAULT_BUILD_PROFILES[profile]
        DefineBuildProfile(profile, config.type, config.output)
        return true
    end

    -- Check obvious default profiles
    if profile == "cd-image" or profile == "install" or profile == "vmware-image" then
        DefineBuildProfile(profile, profile, nil)
        return true
    end

    -- Unknown profile
    print("Build profile '" .. profile .. "' not defined.")
    return false
end

-- ============================================================================
-- Official Release Flag (commented out by default)
-- ============================================================================

-- Uncomment for official release branch:
-- table.insert(HAIKU_DEFINES, "HAIKU_OFFICIAL_RELEASE")
-- table.insert(TARGET_DEFINES, "HAIKU_OFFICIAL_RELEASE")