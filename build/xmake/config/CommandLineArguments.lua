--[[
    CommandLineArguments.lua - Command line argument processing

    xmake equivalent of build/jam/CommandLineArguments

    This module handles:
    - Build profile selection (@nightly-anyboot, @release-raw, etc.)
    - Special targets (help, run, update-image, etc.)
    - Command line parsing and validation

    Usage:
        xmake haiku-image              -- Build default image
        xmake @nightly-anyboot         -- Build nightly anyboot image
        xmake @release-raw             -- Build release raw image
        xmake help                     -- Show help
        xmake run <command>            -- Run command with built targets
]]

-- ============================================================================
-- Global Variables
-- ============================================================================

-- Build profile state (equivalent to Jam variables)
HAIKU_ORIGINAL_TARGETS = {}
HAIKU_BUILD_PROFILE = nil
HAIKU_BUILD_PROFILE_ACTION = "build"
HAIKU_BUILD_PROFILE_PARAMETERS = {}
HAIKU_BUILD_PROFILE_DEFINED = false
HAIKU_PACKAGES_UPDATE_ONLY = false

-- ============================================================================
-- Help Task
-- ============================================================================

--[[
    xmake help

    Displays usage information for building Haiku.
]]
task("help")
    set_category("haiku")
    set_menu {
        usage = "xmake help",
        description = "Show Haiku build system help"
    }

    on_run(function ()
        print([[
Individual targets (applications, libraries, drivers, ...) can be built by just
passing them as arguments to xmake. The recommended method to build or update a
Haiku image or installation is to use a build profile with one of the build
profile actions. Typical command lines using a build profile looks like this:

  xmake @nightly-anyboot
  xmake @install update libbe.so
  xmake @nightly-vmware mount

Default build profiles (minimal set of packages and configuration):
  image            - A raw disk image.
  anyboot-image    - A custom image for either CD or disk.
  cd-image         - An ISO9660 CD image.
  vmware-image     - A VMware disk image.
  install          - A Haiku installation in a directory.

Nightly build profiles (small set of packages used in nightly builds
and default configuration):
  nightly-raw      - A raw disk image.
  nightly-anyboot  - A custom image for either CD or disk.
  nightly-cd       - An ISO9660 CD image.
  nightly-vmware   - A VMware disk image.

Release build profiles (bigger and more complete set of packages used in
releases and default configuration):
  release-raw      - A raw disk image.
  release-anyboot  - A custom image for either CD or disk.
  release-cd       - An ISO9660 CD image.
  release-vmware   - A VMware disk image.

Bootstrap build profiles (minimal image used for initial build of HPKG packages):
  bootstrap-raw    - A raw disk image.
  bootstrap-vmware - A VMware disk image.

Build profile actions:
  build            - Build a Haiku image/installation. This is the default
                     action, i.e. it can be omitted.
  update <target>  - Update the specified targets in the Haiku
                     image/installation.
  update-all       - Update all targets in the Haiku image/installation.
  mount            - Mount the Haiku image in the bfs_shell.

For more details on how to customize Haiku builds read
build/jam/UserBuildConfig.ReadMe.
]])
    end)

-- ============================================================================
-- Run Task
-- ============================================================================

--[[
    xmake run <command> [args...]

    Runs arbitrary command lines containing build system targets,
    which are built and replaced accordingly.
]]
task("run")
    set_category("haiku")
    set_menu {
        usage = "xmake run <command> [args...]",
        description = "Run command with built targets",
        options = {
            {nil, "command", "v", nil, "Command to run with targets"}
        }
    }

    on_run(function ()
        local args = option.get("command")
        if not args or #args == 0 then
            raise("\"xmake run\" requires parameters!")
        end

        -- Build targets first, then run command using RunCommandLine from MiscRules
        local result = RunCommandLine(args)
        if result and result.command then
            print("Running: " .. result.command)
            os.exec(result.command)
        end
    end)

-- ============================================================================
-- Build Profile Processing
-- ============================================================================

--[[
    ParseBuildProfile(target_name)

    Parses a build profile from target name starting with @.
    Returns profile name or nil.

    Examples:
        "@nightly-anyboot" -> "nightly-anyboot"
        "@release-raw" -> "release-raw"
        "haiku-image" -> nil
]]
function ParseBuildProfile(target_name)
    if not target_name then
        return nil
    end

    local profile = target_name:match("^@(.+)$")
    return profile
end

--[[
    ProcessCommandLineArguments(targets)

    Analyzes and optionally replaces build targets.
    This is the main entry point for command line processing.

    Equivalent to Jam rule ProcessCommandLineArguments.

    Parameters:
        targets - Array of target names from command line

    Returns:
        processed_targets - Modified target list
        build_profile - Build profile name (or nil)
        action - Build action (build, update, mount, etc.)
        parameters - Additional parameters
]]
function ProcessCommandLineArguments(targets)
    if not targets then
        targets = {}
    end

    -- Store original targets
    HAIKU_ORIGINAL_TARGETS = targets

    -- Reset build profile state
    HAIKU_BUILD_PROFILE = nil
    HAIKU_BUILD_PROFILE_ACTION = "build"
    HAIKU_BUILD_PROFILE_PARAMETERS = {}
    HAIKU_BUILD_PROFILE_DEFINED = false

    if #targets == 0 then
        -- No targets specified - default behavior
        return {"haiku-image"}, nil, "build", {}
    end

    local first_target = targets[1]

    -- Check for special targets
    if first_target == "all" then
        -- Default target in root directory becomes haiku-image
        local invocation_subdir = os.getenv("INVOCATION_SUBDIR") or ""
        if invocation_subdir == "" or invocation_subdir == "src" then
            return {"haiku-image"}, nil, "build", {}
        end
        return targets, nil, "build", {}

    elseif first_target == "help" then
        -- Help is handled by task, but we can also process it here
        return {"help"}, nil, "help", {}

    elseif first_target == "run" then
        -- Run command - requires parameters
        if #targets < 2 then
            raise("\"xmake run\" requires parameters!")
        end
        local run_args = {}
        for i = 2, #targets do
            table.insert(run_args, targets[i])
        end
        return run_args, nil, "run", run_args

    elseif first_target:match("^@") then
        -- Build profile: @nightly-anyboot, @release-raw, etc.
        HAIKU_BUILD_PROFILE = ParseBuildProfile(first_target)

        -- Get action (default: build)
        local remaining_targets = {}
        for i = 2, #targets do
            table.insert(remaining_targets, targets[i])
        end

        if #remaining_targets > 0 then
            HAIKU_BUILD_PROFILE_ACTION = remaining_targets[1]
            for i = 2, #remaining_targets do
                table.insert(HAIKU_BUILD_PROFILE_PARAMETERS, remaining_targets[i])
            end
        end

        return remaining_targets, HAIKU_BUILD_PROFILE, HAIKU_BUILD_PROFILE_ACTION, HAIKU_BUILD_PROFILE_PARAMETERS

    elseif first_target == "update-image" or first_target == "update-vmware-image"
           or first_target == "update-install" then
        -- Update specific targets in image/installation
        HAIKU_PACKAGES_UPDATE_ONLY = true

        local update_targets = {}
        for i = 2, #targets do
            table.insert(update_targets, targets[i])
        end

        -- Determine actual target based on update type
        local actual_target
        if first_target == "update-image" then
            actual_target = "haiku-image"
        elseif first_target == "update-vmware-image" then
            actual_target = "haiku-vmware-image"
        else
            actual_target = "install-haiku"
        end

        return {actual_target}, nil, "update", update_targets
    end

    -- Default: return targets as-is
    return targets, nil, "build", {}
end

-- ============================================================================
-- Build Profile Validation
-- ============================================================================

-- Known build profiles
local KNOWN_BUILD_PROFILES = {
    -- Default profiles
    ["image"] = true,
    ["anyboot-image"] = true,
    ["cd-image"] = true,
    ["vmware-image"] = true,
    ["install"] = true,

    -- Nightly profiles
    ["nightly-raw"] = true,
    ["nightly-anyboot"] = true,
    ["nightly-cd"] = true,
    ["nightly-vmware"] = true,

    -- Release profiles
    ["release-raw"] = true,
    ["release-anyboot"] = true,
    ["release-cd"] = true,
    ["release-vmware"] = true,

    -- Bootstrap profiles
    ["bootstrap-raw"] = true,
    ["bootstrap-vmware"] = true,
}

--[[
    IsKnownBuildProfile(profile)

    Checks if a build profile name is known/valid.
]]
function IsKnownBuildProfile(profile)
    return KNOWN_BUILD_PROFILES[profile] == true
end

--[[
    ValidateBuildProfile(profile)

    Validates a build profile and raises error if invalid.
]]
function ValidateBuildProfile(profile)
    if not profile then
        return true
    end

    if not IsKnownBuildProfile(profile) then
        print("Warning: Unknown build profile '" .. profile .. "'")
        print("Known profiles:")
        for name, _ in pairs(KNOWN_BUILD_PROFILES) do
            print("  @" .. name)
        end
        return false
    end

    return true
end

-- ============================================================================
-- Build Profile Type Detection
-- ============================================================================

--[[
    GetBuildProfileType(profile)

    Returns the type of build profile:
    - "bootstrap" for bootstrap-* profiles
    - "minimum" for minimum-* profiles (if any)
    - "nightly" for nightly-* profiles
    - "release" for release-* profiles
    - "default" for default profiles
    - nil for unknown
]]
function GetBuildProfileType(profile)
    if not profile then
        return "default"
    end

    if profile:match("^bootstrap%-") then
        return "bootstrap"
    elseif profile:match("^minimum%-") then
        return "minimum"
    elseif profile:match("^nightly%-") then
        return "nightly"
    elseif profile:match("^release%-") then
        return "release"
    elseif KNOWN_BUILD_PROFILES[profile] then
        return "default"
    end

    return nil
end

--[[
    GetBuildProfileImageType(profile)

    Returns the image type for a build profile:
    - "raw" for raw disk images
    - "anyboot" for anyboot images
    - "cd" for ISO CD images
    - "vmware" for VMware images
    - "install" for directory installation
    - nil for unknown
]]
function GetBuildProfileImageType(profile)
    if not profile then
        return nil
    end

    if profile:match("%-raw$") or profile == "image" then
        return "raw"
    elseif profile:match("%-anyboot$") or profile == "anyboot-image" then
        return "anyboot"
    elseif profile:match("%-cd$") or profile == "cd-image" then
        return "cd"
    elseif profile:match("%-vmware$") or profile == "vmware-image" then
        return "vmware"
    elseif profile == "install" then
        return "install"
    end

    return nil
end

-- ============================================================================
-- Utility Functions
-- ============================================================================

--[[
    SetUpdateHaikuImageOnly(value)

    Sets the update-only mode for Haiku image building.
    When enabled, only specified targets are updated in the image.
]]
function SetUpdateHaikuImageOnly(value)
    HAIKU_PACKAGES_UPDATE_ONLY = (value == 1 or value == true)
end

--[[
    GetBuildProfileInfo()

    Returns current build profile information as a table.
]]
function GetBuildProfileInfo()
    return {
        profile = HAIKU_BUILD_PROFILE,
        action = HAIKU_BUILD_PROFILE_ACTION,
        parameters = HAIKU_BUILD_PROFILE_PARAMETERS,
        defined = HAIKU_BUILD_PROFILE_DEFINED,
        update_only = HAIKU_PACKAGES_UPDATE_ONLY,
        original_targets = HAIKU_ORIGINAL_TARGETS,
        profile_type = GetBuildProfileType(HAIKU_BUILD_PROFILE),
        image_type = GetBuildProfileImageType(HAIKU_BUILD_PROFILE),
    }
end

-- ============================================================================
-- xmake Options for Build Profiles
-- ============================================================================

option("build_profile")
    set_default("")
    set_showmenu(true)
    set_category("haiku")
    set_description("Build profile (nightly-anyboot, release-raw, etc.)")
    set_values(
        "image", "anyboot-image", "cd-image", "vmware-image", "install",
        "nightly-raw", "nightly-anyboot", "nightly-cd", "nightly-vmware",
        "release-raw", "release-anyboot", "release-cd", "release-vmware",
        "bootstrap-raw", "bootstrap-vmware"
    )

option("build_action")
    set_default("build")
    set_showmenu(true)
    set_category("haiku")
    set_description("Build action (build, update, update-all, mount)")
    set_values("build", "update", "update-all", "mount")

option("update_targets")
    set_default("")
    set_showmenu(true)
    set_category("haiku")
    set_description("Targets to update (for update action)")