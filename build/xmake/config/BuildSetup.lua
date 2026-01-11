--[[
    BuildSetup.lua - Build system configuration and setup

    xmake equivalent of build/jam/BuildSetup

    This module handles:
    - Version definitions
    - Container settings for images
    - Default image/CD/anyboot settings
    - Host and target platform configuration
    - Compiler flags and defines
    - Directory structure setup

    Variable naming conventions:
    TARGET_*:   Properties for building for target platform (usually Haiku)
    HOST_*:     Properties for the platform hosting the build
    HAIKU_*:    Build system properties (directory paths, etc.)
]]

-- ============================================================================
-- Version Information
-- ============================================================================

-- The Haiku (base) version. For development builds the revision will be attached.
HAIKU_VERSION = "r1~beta5"

-- ============================================================================
-- Container Settings
-- ============================================================================

-- Container configuration for different image types
HAIKU_CONTAINERS = {
    -- Haiku image
    image = {
        name = "haiku-image-container",
        grist = "HaikuImage",
        include_var = "HAIKU_INCLUDE_IN_IMAGE",
        install_targets_var = "HAIKU_IMAGE_INSTALL_TARGETS",
        system_dir_tokens = {"system", "non-packaged"},
    },

    -- Network boot archive
    netboot = {
        name = "haiku-netboot-archive-container",
        grist = "NetBootArchive",
        -- include_var not supported (update only mode not supported)
        install_targets_var = "HAIKU_NET_BOOT_ARCHIVE_INSTALL_TARGETS",
        system_dir_tokens = {"system"},
    },

    -- Boot CD image
    cd_boot = {
        name = "haiku-boot-cd-container",
        grist = "CDBootImage",
        -- include_var not supported (update only mode not supported)
        install_targets_var = "HAIKU_CD_BOOT_IMAGE_INSTALL_TARGETS",
        system_dir_tokens = {"system"},
    },

    -- Boot MMC image
    mmc_boot = {
        name = "haiku-boot-mmc-container",
        grist = "MMCImage",
        -- include_var not supported (update only mode not supported)
        install_targets_var = "HAIKU_MMC_BOOT_IMAGE_INSTALL_TARGETS",
        system_dir_tokens = {"system"},
    },
}

-- ============================================================================
-- Image Defaults
-- ============================================================================

-- Haiku image/install defaults
HAIKU_DEFAULT_IMAGE_NAME = "haiku.image"
HAIKU_DEFAULT_IMAGE_DIR = nil  -- Set to HAIKU_OUTPUT_DIR at runtime
HAIKU_DEFAULT_VMWARE_IMAGE_NAME = "haiku.vmdk"
HAIKU_DEFAULT_INSTALL_DIR = "/Haiku"
HAIKU_DEFAULT_IMAGE_SIZE = 4000  -- 4000 MB
HAIKU_DEFAULT_IMAGE_LABEL = "Haiku"

-- Haiku CD defaults
HAIKU_DEFAULT_CD_NAME = "haiku-cd.iso"
HAIKU_DEFAULT_CD_DIR = nil  -- Set to HAIKU_OUTPUT_DIR at runtime
HAIKU_DEFAULT_CD_LABEL = "Haiku"

-- Haiku Anyboot defaults
HAIKU_DEFAULT_ANYBOOT_NAME = "haiku-anyboot.iso"
HAIKU_DEFAULT_ANYBOOT_DIR = nil  -- Set to HAIKU_OUTPUT_DIR at runtime
HAIKU_DEFAULT_ANYBOOT_LABEL = "Haiku"

-- Haiku MMC defaults
HAIKU_DEFAULT_MMC_NAME = "haiku-mmc.image"
HAIKU_DEFAULT_MMC_DIR = nil  -- Set to HAIKU_OUTPUT_DIR at runtime
HAIKU_DEFAULT_MMC_LABEL = "Haiku"

-- ============================================================================
-- Debug Levels
-- ============================================================================

-- Supported debug levels
HAIKU_DEBUG_LEVELS = {0, 1, 2, 3, 4, 5}

-- ============================================================================
-- Network Libraries
-- ============================================================================

HAIKU_NETWORK_LIBS = {"network"}
HAIKU_NETAPI_LIB = "bnetapi"
HAIKU_SELECT_UNAME_ETC_LIB = nil  -- libroot, against which we link anyway

-- ============================================================================
-- MIME Types
-- ============================================================================

HAIKU_EXECUTABLE_MIME_TYPE = "application/x-vnd.Be-elfexecutable"

-- ============================================================================
-- Build Version
-- ============================================================================

-- Set version if not already set and mark it as developer build
if not HAIKU_BUILD_VERSION then
    HAIKU_BUILD_VERSION = "1 0 0 a 1"
    HAIKU_BUILD_DESCRIPTION = "Developer Build"
end

-- If HAIKU_BUILD_VERSION is set but HAIKU_BUILD_DESCRIPTION isn't,
-- mark it as unknown build
HAIKU_BUILD_DESCRIPTION = HAIKU_BUILD_DESCRIPTION or "Unknown Build"

-- ============================================================================
-- Distro Compatibility
-- ============================================================================

HAIKU_DISTRO_COMPATIBILITY = HAIKU_DISTRO_COMPATIBILITY or "default"

--[[
    GetDistroCompatibilityDefines()

    Returns defines based on distro compatibility level.
]]
function GetDistroCompatibilityDefines()
    local defines = {}
    local include_trademarks = nil

    if HAIKU_DISTRO_COMPATIBILITY == "official" then
        table.insert(defines, "HAIKU_DISTRO_COMPATIBILITY_OFFICIAL")
        include_trademarks = ""
    elseif HAIKU_DISTRO_COMPATIBILITY == "compatible" then
        table.insert(defines, "HAIKU_DISTRO_COMPATIBILITY_COMPATIBLE")
        include_trademarks = ""
    elseif HAIKU_DISTRO_COMPATIBILITY == "default" then
        table.insert(defines, "HAIKU_DISTRO_COMPATIBILITY_DEFAULT")
        include_trademarks = nil
    else
        raise("Invalid value for HAIKU_DISTRO_COMPATIBILITY: " .. tostring(HAIKU_DISTRO_COMPATIBILITY))
    end

    return defines, include_trademarks
end

-- ============================================================================
-- Host Platform Detection
-- ============================================================================

--[[
    DetectHost64Bit(gcc_machine)

    Analyzes host GCC machine spec to detect 64-bitness.
]]
function DetectHost64Bit(gcc_machine)
    if not gcc_machine then
        return false
    end

    local patterns_64bit = {
        "^i686%-apple%-darwin10",
        "^i686%-apple%-darwin11",
        "^x86_64%-",
        "^amd64%-",
        "^arm64%-",
        "^aarch64%-",
        "^riscv64%-",
    }

    for _, pattern in ipairs(patterns_64bit) do
        if gcc_machine:match(pattern) then
            return true
        end
    end

    return false
end

--[[
    GetHostCPU()

    Returns the host CPU architecture, accounting for 64-bit.
]]
function GetHostCPU(base_cpu, is_64bit)
    local cpu = base_cpu or os.arch()

    if cpu == "x86" and is_64bit then
        return "x86_64"
    elseif cpu == "arm" and is_64bit then
        return "arm64"
    elseif cpu == "riscv" and is_64bit then
        return "riscv64"
    end

    return cpu
end

-- ============================================================================
-- Host Platform Settings
-- ============================================================================

HOST_PLATFORM_SETTINGS = {
    -- Debug flags per platform
    debug_flags = {
        haiku = "-ggdb",
        haiku_host = "-ggdb",
        linux = "-ggdb",
        freebsd = "-ggdb",
        netbsd = "-ggdb",
        darwin = "-ggdb",
        default = "-g",
    },

    -- Platform-specific header paths
    extra_hdrs = {
        freebsd = {"/usr/local/include", "/usr/include/gnu"},
        netbsd = {"/usr/pkg/include"},
        darwin = {
            "/opt/local/include",      -- MacPorts
            "/usr/local/include",      -- Homebrew (Intel)
            "/opt/homebrew/include",   -- Homebrew (Apple Silicon)
        },
    },

    -- Platform-specific link flags
    extra_linkflags = {
        freebsd = {"-L/usr/local/lib"},
        netbsd = {"-L/usr/pkg/lib", "-Wl,-rpath,/usr/pkg/lib"},
        darwin = {
            "-L/opt/local/lib",
            "-L/usr/local/lib",
            "-L/opt/homebrew/lib",
        },
    },
}

--[[
    GetHostDebugFlags(platform)

    Returns debug flags for the specified host platform.
]]
function GetHostDebugFlags(platform)
    return HOST_PLATFORM_SETTINGS.debug_flags[platform]
        or HOST_PLATFORM_SETTINGS.debug_flags.default
end

-- ============================================================================
-- Warning Flags
-- ============================================================================

-- Host warning flags
HOST_WARNING_CCFLAGS = {
    "-Wall", "-Wno-trigraphs", "-Wmissing-prototypes",
    "-Wpointer-arith", "-Wcast-align", "-Wsign-compare"
}

HOST_WARNING_CXXFLAGS = {
    "-Wall", "-Wno-trigraphs", "-Wno-ctor-dtor-privacy",
    "-Woverloaded-virtual", "-Wpointer-arith", "-Wcast-align", "-Wsign-compare"
}

-- ============================================================================
-- Common Build Variables
-- ============================================================================

-- Build variables that apply to all architectures
BUILD_VARS = {
    "ARCH", "ARCHS", "KERNEL_ARCH", "KERNEL_ARCH_DIR",
    "PACKAGING_ARCH", "PACKAGING_ARCHS",
    "DEFINES", "KERNEL_DEFINES", "BOOT_DEFINES",
    "KERNEL_CCFLAGS", "KERNEL_CXXFLAGS",
    "KERNEL_PIC_CCFLAGS", "KERNEL_PIC_LINKFLAGS", "KERNEL_ADDON_LINKFLAGS",
    "BOOT_CCFLAGS", "BOOT_CXXFLAGS", "BOOT_LINKFLAGS", "BOOT_LDFLAGS",
    "KERNEL_WARNING_CCFLAGS", "KERNEL_WARNING_CXXFLAGS",
    "PRIVATE_KERNEL_HEADERS",
    "NETWORK_LIBS", "NETAPI_LIB", "SELECT_UNAME_ETC_LIB",
    "EXECUTABLE_MIME_TYPE",
    "OBJECT_BASE_DIR", "COMMON_ARCH_OBJECT_DIR",
}

-- Architecture-dependent build variables
ARCH_DEPENDENT_BUILD_VARS = {
    "ARCH", "CPU",
    "AR", "CC", "CXX", "ELFEDIT", "LD", "OBJCOPY", "RANLIB", "STRIP",
    "CC_IS_LEGACY_GCC", "CC_IS_CLANG",
    "ARFLAGS", "ASFLAGS", "UNARFLAGS", "CPPFLAGS", "CCFLAGS", "CXXFLAGS",
    "HDRS", "LDFLAGS", "LINK", "LINKFLAGS",
    "WARNING_CCFLAGS", "WARNING_CXXFLAGS", "WERROR_FLAGS",
    "INCLUDES_SEPARATOR", "LOCAL_INCLUDES_OPTION", "SYSTEM_INCLUDES_OPTION",
    "PRIVATE_SYSTEM_HEADERS",
    "ARCH_OBJECT_DIR", "COMMON_DEBUG_OBJECT_DIR",
}

-- ============================================================================
-- Default Build Settings
-- ============================================================================

-- Defaults for warnings, optimization, and debugging
WARNINGS = 1
OPTIM = "-O2"
DEBUG = 0

-- CI build flag
HAIKU_CONTINUOUS_INTEGRATION_BUILD = 0

-- Default platform settings
PLATFORM = nil  -- Set to TARGET_PLATFORM at runtime
SUPPORTED_PLATFORMS = {"haiku"}

-- Keep object files (needed when objects are used in both static lib and executable)
KEEPOBJS = true

-- File permissions for image
EXEMODE = "755"
FILEMODE = "644"
SHELLMODE = "755"

-- ============================================================================
-- Directories That Cannot Be Compiled with DEBUG=1
-- ============================================================================

DEBUG_DISABLED_DIRS = {
    "src/add-ons/disk_systems/bfs",
    "src/add-ons/kernel/drivers/audio/hda",
    "src/add-ons/kernel/file_systems/btrfs",
    "src/add-ons/kernel/file_systems/ntfs/libntfs",
    "src/add-ons/kernel/file_systems/userlandfs/server",
    "src/add-ons/kernel/file_systems/userlandfs/server/haiku",
    "src/add-ons/print/drivers/gutenprint",
    "src/servers/input",
    "src/system/boot/loader/file_systems/bfs",
}

--[[
    IsDebugDisabledDir(dir)

    Checks if a directory should have DEBUG disabled.
]]
function IsDebugDisabledDir(dir)
    for _, disabled_dir in ipairs(DEBUG_DISABLED_DIRS) do
        if dir:find(disabled_dir, 1, true) then
            return true
        end
    end
    return false
end

-- ============================================================================
-- Be API Headers (for host build compatibility)
-- ============================================================================

--[[
    GetBeAPIHeaders()

    Returns the list of Be API header directories for host builds.
]]
function GetBeAPIHeaders()
    local haiku_top = get_config("haiku_top") or os.getenv("HAIKU_TOP") or "."

    return {
        path.join(haiku_top, "headers/build"),
        path.join(haiku_top, "headers/build/os"),
        path.join(haiku_top, "headers/build/os/add-ons/registrar"),
        path.join(haiku_top, "headers/build/os/app"),
        path.join(haiku_top, "headers/build/os/bluetooth"),
        path.join(haiku_top, "headers/build/os/drivers"),
        path.join(haiku_top, "headers/build/os/kernel"),
        path.join(haiku_top, "headers/build/os/interface"),
        path.join(haiku_top, "headers/build/os/locale"),
        path.join(haiku_top, "headers/build/os/storage"),
        path.join(haiku_top, "headers/build/os/support"),
        path.join(haiku_top, "headers/build/private"),
    }
end

-- ============================================================================
-- Configuration Header Directories
-- ============================================================================

--[[
    GetConfigHeaderDirs()

    Returns configuration header directories.
]]
function GetConfigHeaderDirs()
    local haiku_top = get_config("haiku_top") or os.getenv("HAIKU_TOP") or "."

    return {
        path.join(haiku_top, "build/user_config_headers"),
        path.join(haiku_top, "build/config_headers"),
    }
end

-- ============================================================================
-- Host Compatibility Libraries
-- ============================================================================

HOST_LIBROOT = {"libroot_build_function_remapper.a", "libroot_build.so"}
HOST_STATIC_LIBROOT = {"libroot_build_function_remapper.a", "libroot_build.a"}
HOST_LIBBE = "libbe_build.so"

-- ============================================================================
-- xmake Options for Build Setup
-- ============================================================================

option("haiku_version")
    set_default(HAIKU_VERSION)
    set_showmenu(true)
    set_category("haiku")
    set_description("Haiku version string")

option("haiku_build_description")
    set_default("Developer Build")
    set_showmenu(true)
    set_category("haiku")
    set_description("Haiku build description")

option("distro_compatibility")
    set_default("default")
    set_showmenu(true)
    set_category("haiku")
    set_description("Distribution compatibility level")
    set_values("official", "compatible", "default")

option("image_size")
    set_default(4000)
    set_showmenu(true)
    set_category("haiku")
    set_description("Default image size in MB")

option("image_label")
    set_default("Haiku")
    set_showmenu(true)
    set_category("haiku")
    set_description("Default image label")

option("debug_level")
    set_default(0)
    set_showmenu(true)
    set_category("haiku")
    set_description("Debug level (0-5)")
    set_values("0", "1", "2", "3", "4", "5")

option("warnings")
    set_default(true)
    set_showmenu(true)
    set_category("haiku")
    set_description("Enable compiler warnings")

option("continuous_integration")
    set_default(false)
    set_showmenu(true)
    set_category("haiku")
    set_description("CI build mode")

-- ============================================================================
-- Initialization Function
-- ============================================================================

--[[
    InitializeBuildSetup()

    Initializes build setup variables. Called during build configuration.
]]
function InitializeBuildSetup()
    local haiku_top = get_config("haiku_top") or os.getenv("HAIKU_TOP") or "."
    local haiku_output_dir = get_config("haiku_output") or path.join(haiku_top, "generated")

    -- Set directory defaults
    HAIKU_DEFAULT_IMAGE_DIR = haiku_output_dir
    HAIKU_DEFAULT_CD_DIR = haiku_output_dir
    HAIKU_DEFAULT_ANYBOOT_DIR = haiku_output_dir
    HAIKU_DEFAULT_MMC_DIR = haiku_output_dir

    -- Process command line arguments
    ProcessCommandLineArguments(os.argv())

    -- Get distro compatibility defines
    local distro_defines, include_trademarks = GetDistroCompatibilityDefines()
    HAIKU_DEFINES = HAIKU_DEFINES or {}
    for _, def in ipairs(distro_defines) do
        table.insert(HAIKU_DEFINES, def)
    end
    HAIKU_INCLUDE_TRADEMARKS = include_trademarks

    return true
end

-- ============================================================================
-- Platform Target Defines
-- ============================================================================

--[[
    GetTargetPlatformDefines(platform)

    Returns defines for identifying the target platform.
]]
function GetTargetPlatformDefines(platform)
    local defines = {}

    if platform == "haiku" then
        table.insert(defines, "HAIKU_TARGET_PLATFORM_HAIKU")
    elseif platform == "libbe_test" then
        table.insert(defines, "HAIKU_TARGET_PLATFORM_LIBBE_TEST")
    end

    return defines
end

--[[
    GetHostPlatformDefines(platform, is_64bit)

    Returns defines for identifying the host platform.
]]
function GetHostPlatformDefines(platform, is_64bit)
    local defines = {}

    if platform == "haiku_host" then
        table.insert(defines, "HAIKU_HOST_PLATFORM_HAIKU")
    elseif platform == "linux" then
        table.insert(defines, "HAIKU_HOST_PLATFORM_LINUX")
    elseif platform == "freebsd" then
        table.insert(defines, "HAIKU_HOST_PLATFORM_FREEBSD")
    elseif platform == "netbsd" then
        table.insert(defines, "HAIKU_HOST_PLATFORM_NETBSD")
    elseif platform == "darwin" then
        table.insert(defines, "HAIKU_HOST_PLATFORM_DARWIN")
    end

    if is_64bit then
        table.insert(defines, "HAIKU_HOST_PLATFORM_64_BIT")
    end

    return defines
end

-- ============================================================================
-- UserBuildConfig Helper Functions
-- ============================================================================

-- Configuration variables storage
CONFIG_VARS = CONFIG_VARS or {}

--[[
    SetConfigVar(var, path, value, scope)

    Sets a configuration variable for a specific path in the source tree.

    Parameters:
        var   - Variable name (e.g., "DEBUG", "WARNINGS", "DEFINES")
        path  - Path components as table (e.g., {"HAIKU_TOP", "src", "kernel"})
        value - Value to set
        scope - "local" (only this dir) or "global" (recursive to subdirs)

    Example:
        SetConfigVar("DEBUG", {"HAIKU_TOP", "src", "system", "boot"}, 1, "global")
]]
function SetConfigVar(var, path_parts, value, scope)
    local path_key = table.concat(path_parts, "/")
    scope = scope or "local"

    if not CONFIG_VARS[var] then
        CONFIG_VARS[var] = {}
    end

    CONFIG_VARS[var][path_key] = {
        value = value,
        scope = scope,
        path = path_parts,
    }
end

--[[
    AppendToConfigVar(var, path, value, scope)

    Appends a value to a configuration variable for a specific path.

    Example:
        AppendToConfigVar("DEFINES", {"HAIKU_TOP", "src", "kits"}, "MY_DEFINE", "global")
]]
function AppendToConfigVar(var, path_parts, value, scope)
    local path_key = table.concat(path_parts, "/")
    scope = scope or "local"

    if not CONFIG_VARS[var] then
        CONFIG_VARS[var] = {}
    end

    local existing = CONFIG_VARS[var][path_key]
    if existing then
        if type(existing.value) == "table" then
            table.insert(existing.value, value)
        else
            existing.value = {existing.value, value}
        end
    else
        CONFIG_VARS[var][path_key] = {
            value = {value},
            scope = scope,
            path = path_parts,
            append = true,
        }
    end
end

--[[
    GetConfigVar(var, current_path)

    Gets a configuration variable value for the current path, considering
    inherited values from parent directories.

    Returns: value or nil
]]
function GetConfigVar(var, current_path)
    if not CONFIG_VARS[var] then
        return nil
    end

    -- Normalize path
    if type(current_path) == "string" then
        current_path = current_path:gsub("\\", "/")
    elseif type(current_path) == "table" then
        current_path = table.concat(current_path, "/")
    end

    -- Check exact match first
    local entry = CONFIG_VARS[var][current_path]
    if entry and entry.scope == "local" then
        return entry.value
    end

    -- Check parent paths for global scope
    local best_match = nil
    local best_match_len = 0

    for path_key, entry_data in pairs(CONFIG_VARS[var]) do
        if entry_data.scope == "global" then
            -- Check if current_path starts with this path
            if current_path:find(path_key, 1, true) == 1 then
                local len = #path_key
                if len > best_match_len then
                    best_match = entry_data.value
                    best_match_len = len
                end
            end
        end
    end

    return best_match or (entry and entry.value)
end

--[[
    SetDebugOnTarget(target_name, level)

    Sets debug level on a specific target/object file.

    Example:
        SetDebugOnTarget("<src!bin!gdb!gdb!>haiku-nat.o", 1)
]]
function SetDebugOnTarget(target_name, level)
    if not TARGET_DEBUG_LEVELS then
        TARGET_DEBUG_LEVELS = {}
    end
    TARGET_DEBUG_LEVELS[target_name] = level
end

--[[
    GetDebugOnTarget(target_name)

    Gets debug level for a specific target.
]]
function GetDebugOnTarget(target_name)
    if TARGET_DEBUG_LEVELS then
        return TARGET_DEBUG_LEVELS[target_name]
    end
    return nil
end

--[[
    SetUpdateHaikuImageOnly(enabled)

    When enabled, only targets with HAIKU_INCLUDE_IN_IMAGE set will be updated.
]]
function SetUpdateHaikuImageOnly(enabled)
    HAIKU_UPDATE_ONLY = enabled
end

--[[
    SetHaikuIncludeInImage(targets)

    Marks targets for inclusion in image updates.

    Example:
        SetHaikuIncludeInImage({"libbe.so", "kernel"})
]]
function SetHaikuIncludeInImage(targets)
    if not HAIKU_INCLUDE_IN_IMAGE_TARGETS then
        HAIKU_INCLUDE_IN_IMAGE_TARGETS = {}
    end

    for _, target in ipairs(targets) do
        HAIKU_INCLUDE_IN_IMAGE_TARGETS[target] = true
    end
end

--[[
    AddFilesToHaikuImage(directory, files)

    Adds files to the Haiku image.

    Example:
        AddFilesToHaikuImage("system/bin", {"crashing_app", "my_tool"})
]]
function AddFilesToHaikuImage(directory, files)
    if not HAIKU_IMAGE_FILES then
        HAIKU_IMAGE_FILES = {}
    end

    for _, file in ipairs(files) do
        table.insert(HAIKU_IMAGE_FILES, {
            directory = directory,
            file = file,
        })
    end
end

--[[
    AddSourceDirectoryToHaikuImage(path_parts, options)

    Adds a source directory to the image.

    Options:
        alwaysUpdate - Also copy during update mode
]]
function AddSourceDirectoryToHaikuImage(path_parts, options)
    options = options or {}

    if not HAIKU_IMAGE_SOURCE_DIRS then
        HAIKU_IMAGE_SOURCE_DIRS = {}
    end

    table.insert(HAIKU_IMAGE_SOURCE_DIRS, {
        path = path_parts,
        alwaysUpdate = options.alwaysUpdate or false,
    })
end

--[[
    CopyDirectoryToHaikuImage(target_dir, options)

    Copies a directory to the image.

    Options:
        source          - Source directory path
        dest            - Destination name (optional)
        exclude         - List of names to exclude
        excludePatterns - List of patterns to exclude
]]
function CopyDirectoryToHaikuImage(target_dir, options)
    options = options or {}

    if not HAIKU_IMAGE_COPY_DIRS then
        HAIKU_IMAGE_COPY_DIRS = {}
    end

    table.insert(HAIKU_IMAGE_COPY_DIRS, {
        target = target_dir,
        source = options.source,
        dest = options.dest,
        exclude = options.exclude or {},
        excludePatterns = options.excludePatterns or {},
    })
end

--[[
    ExtractArchiveToHaikuImage(target_dir, options)

    Extracts an archive to the image.

    Options:
        archive - Path to archive file
]]
function ExtractArchiveToHaikuImage(target_dir, options)
    options = options or {}

    if not HAIKU_IMAGE_ARCHIVES then
        HAIKU_IMAGE_ARCHIVES = {}
    end

    table.insert(HAIKU_IMAGE_ARCHIVES, {
        target = target_dir,
        archive = options.archive,
    })
end

--[[
    AddUserToHaikuImage(user_info)

    Adds a user to the image.

    user_info fields:
        name, uid, gid, home, shell, realname
]]
function AddUserToHaikuImage(user_info)
    if not HAIKU_IMAGE_USERS then
        HAIKU_IMAGE_USERS = {}
    end

    table.insert(HAIKU_IMAGE_USERS, user_info)
end

--[[
    AddGroupToHaikuImage(group_info)

    Adds a group to the image.

    group_info fields:
        name, gid, members (table of usernames)
]]
function AddGroupToHaikuImage(group_info)
    if not HAIKU_IMAGE_GROUPS then
        HAIKU_IMAGE_GROUPS = {}
    end

    table.insert(HAIKU_IMAGE_GROUPS, group_info)
end

--[[
    SuppressHaikuImagePackages(packages)

    Suppresses automatic addition of dependent packages.
]]
function SuppressHaikuImagePackages(packages)
    if not HAIKU_IMAGE_SUPPRESSED_PACKAGES then
        HAIKU_IMAGE_SUPPRESSED_PACKAGES = {}
    end

    for _, pkg in ipairs(packages) do
        HAIKU_IMAGE_SUPPRESSED_PACKAGES[pkg] = true
    end
end

--[[
    AddOptionalHaikuImagePackages(packages)

    Adds optional packages without their dependencies.
]]
function AddOptionalHaikuImagePackages(packages)
    for _, pkg in ipairs(packages) do
        AddOptionalPackage(pkg)
    end
end

--[[
    DeferredSubInclude(path_parts, options)

    Schedules a subdirectory for late inclusion.

    Options:
        jamfile - Alternative Jamfile name
        scope   - "local" or "global"
]]
function DeferredSubInclude(path_parts, options)
    options = options or {}

    if not HAIKU_DEFERRED_INCLUDES then
        HAIKU_DEFERRED_INCLUDES = {}
    end

    table.insert(HAIKU_DEFERRED_INCLUDES, {
        path = path_parts,
        jamfile = options.jamfile,
        scope = options.scope or "global",
    })
end

--[[
    GetTargetLocation(target_name)

    Returns the output location for a target.
]]
function GetTargetLocation(target_name)
    -- This would need integration with xmake's target system
    local t = target.target(target_name)
    if t then
        return t:targetfile()
    end
    return nil
end

-- ============================================================================
-- Shell Script Generation Functions
-- ============================================================================

SHELL_SCRIPTS = SHELL_SCRIPTS or {}

--[[
    MakeLocate(script_name, directory)

    Sets the output location for a generated script.
]]
function MakeLocate(script_name, directory)
    if not SHELL_SCRIPTS[script_name] then
        SHELL_SCRIPTS[script_name] = {}
    end
    SHELL_SCRIPTS[script_name].directory = directory
end

--[[
    Always(script_name)

    Marks a script to always be regenerated.
]]
function Always(script_name)
    if not SHELL_SCRIPTS[script_name] then
        SHELL_SCRIPTS[script_name] = {}
    end
    SHELL_SCRIPTS[script_name].always = true
end

--[[
    AddVariableToScript(script_name, var_name, value)

    Adds a variable definition to a shell script.
]]
function AddVariableToScript(script_name, var_name, value)
    if not SHELL_SCRIPTS[script_name] then
        SHELL_SCRIPTS[script_name] = {}
    end
    if not SHELL_SCRIPTS[script_name].variables then
        SHELL_SCRIPTS[script_name].variables = {}
    end
    SHELL_SCRIPTS[script_name].variables[var_name] = value
end

--[[
    AddTargetVariableToScript(script_name, target_name, var_name)

    Adds a variable pointing to a build target to a shell script.
]]
function AddTargetVariableToScript(script_name, target_name, var_name)
    if not SHELL_SCRIPTS[script_name] then
        SHELL_SCRIPTS[script_name] = {}
    end
    if not SHELL_SCRIPTS[script_name].targets then
        SHELL_SCRIPTS[script_name].targets = {}
    end

    -- If no var_name given, use target name (without grist)
    if not var_name then
        var_name = target_name:gsub("^<[^>]+>", "")
    end

    SHELL_SCRIPTS[script_name].targets[var_name] = target_name
end

--[[
    CreateShellScript(script_name, config)

    Convenience function to create a shell script with variables and targets.

    config:
        variables - table of name = value pairs
        targets   - table of var_name = target_name pairs
]]
function CreateShellScript(script_name, config)
    config = config or {}

    if config.variables then
        for name, value in pairs(config.variables) do
            AddVariableToScript(script_name, name, value)
        end
    end

    if config.targets then
        for var_name, target_name in pairs(config.targets) do
            AddTargetVariableToScript(script_name, target_name, var_name)
        end
    end
end

-- ============================================================================
-- User Hook Functions (can be overridden in UserBuildConfig)
-- ============================================================================

-- Called after all build targets are defined
function UserBuildConfigRulePostBuildTargets()
    -- Override in UserBuildConfig.lua
end

-- Called after image contents defined, before image scripts
function UserBuildConfigRulePreImage()
    -- Override in UserBuildConfig.lua
end

-- Called after image target fully defined
function UserBuildConfigRulePostImage()
    -- Override in UserBuildConfig.lua
end

-- ============================================================================
-- Include User Build Config
-- ============================================================================

-- Try to load user build config if it exists
local function LoadUserBuildConfig()
    local user_config_path = path.join(os.scriptdir(), "UserBuildConfig.lua")
    if os.isfile(user_config_path) then
        includes("UserBuildConfig.lua")
    end
end

-- Load user config at the end of BuildSetup initialization
LoadUserBuildConfig()