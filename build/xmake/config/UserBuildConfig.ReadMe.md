--[[
    UserBuildConfig ReadMe
    ----------------------

    UserBuildConfig.lua can be used to customize the build according to your needs.
    If it exists, it is included by the build system, but it is ignored by Git.

    This file documents all available options. Don't rename this file to
    UserBuildConfig.lua -- you don't want all these things to happen.
    Rather create an empty UserBuildConfig.lua and copy the lines you need,
    or start with UserBuildConfig.sample.lua.

    ERROR: If you copy this file directly and run xmake, you will get an error!
]]

error("You must NOT copy UserBuildConfig.ReadMe.lua directly! Use parts of it in UserBuildConfig.lua")

-- ============================================================================
-- ADJUSTING BUILD VARIABLES
-- ============================================================================

--[[
    The following variables can be configured per subdirectory (or subtree)
    or even per object file:

        CCFLAGS, CXXFLAGS, DEBUG, DEFINES, HDRS, LINKFLAGS, OPTIM, OPTIMIZE,
        SYSHDRS, WARNINGS, HOST_WARNING_CCFLAGS, HOST_WARNING_CXXFLAGS,
        TARGET_WARNING_CCFLAGS, TARGET_WARNING_CXXFLAGS, PLATFORM,
        SUPPORTED_PLATFORMS

    The following examples work analogously for any of these variables.
]]

-- Turn off warnings in src/system/kernel (local scope = only this directory)
SetConfigVar("WARNINGS", {"HAIKU_TOP", "src", "system", "kernel"}, 0, "local")

-- Set debug level for src/system/boot/loader and all subdirectories (global scope)
-- Generated files go to a separate subtree for fast debug/release switching
SetConfigVar("DEBUG", {"HAIKU_TOP", "src", "system", "boot", "loader"}, 1, "global")

-- Append define for src/kits and all subdirectories
AppendToConfigVar("DEFINES", {"HAIKU_TOP", "src", "kits"}, "RUN_WITHOUT_REGISTRAR", "global")

-- Set debug level for a specific object file
-- Note: Only the object affects placement; executable location needs separate DEBUG setting
SetDebugOnTarget("<src!bin!gdb!gdb!>haiku-nat.o", 1)

-- Enable SSL build feature
HAIKU_BUILD_FEATURE_SSL = true


-- ============================================================================
-- HAIKU IMAGE SETTINGS
-- ============================================================================

-- Create a 100 MB image at /tmp/walter.image
HAIKU_IMAGE_NAME = "walter.image"
HAIKU_IMAGE_DIR = "/tmp"
HAIKU_IMAGE_SIZE = 100

-- Set image volume label (default is "Haiku")
HAIKU_IMAGE_LABEL = "Walter"

-- VMWare image name (in HAIKU_IMAGE_DIR)
HAIKU_VMWARE_IMAGE_NAME = "walter.vmdk"

-- Install Haiku to directory
HAIKU_INSTALL_DIR = "/Haiku"

-- Don't zero out existing image (useful for partitions)
-- Image will still be freshly initialized with BFS
HAIKU_DONT_CLEAR_IMAGE = true

-- Package compression level (0-9)
-- 0 = no compression (fastest, largest files)
-- 1 = fastest compression (good for development)
-- 9 = best compression (default, for official builds)
HAIKU_PACKAGE_COMPRESSION_LEVEL = 1

-- Override Haiku revision (useful if git repo lacks hrev tags)
-- Remove <generated>/build/haiku-revision when changing this
HAIKU_REVISION = "hrev66666"

-- Only update targets with HAIKU_INCLUDE_IN_IMAGE set
-- Used by update-image, update-vmware-image targets
SetUpdateHaikuImageOnly(true)

-- Mark targets for image inclusion during updates
SetHaikuIncludeInImage({"libbe.so", "kernel"})


-- ============================================================================
-- ADDING FILES TO IMAGE
-- ============================================================================

-- Add target to system/bin directory
AddFilesToHaikuImage("system/bin", {"crashing_app"})

-- Create symlink in home/config/bin
AddSymlinkToHaikuImage("home/config/bin", {
    target = "/bin/crashing_app",
    name = "crash"
})

-- Add keymap settings with custom destination name
AddFilesToHaikuImage("home/config/settings", {
    {source = "<keymap>US-International", dest = "Key_map"}
})

-- Add source directories to image as /boot/home/HaikuSources/...
AddSourceDirectoryToHaikuImage({"src", "kits", "storage"})
-- This one also copies during update mode
AddSourceDirectoryToHaikuImage({"src", "tests", "servers", "debug"}, {alwaysUpdate = true})

-- Copy external directory to image
-- Excludes .git directory and files matching pattern
CopyDirectoryToHaikuImage("home/Desktop", {
    source = "$(HAIKU_TOP)/../buildtools/jam",
    dest = "jam-src",
    exclude = {".git"},
    excludePatterns = {"*/jam/./bin.*"}
})

-- Extract archive to image under /boot/develop/tools
ExtractArchiveToHaikuImage("develop/tools", {
    archive = "/home/user/gcc-2.95.3.zip"
})


-- ============================================================================
-- OPTIONAL PACKAGES
-- ============================================================================

-- Add optional package to image (downloaded if needed)
AddHaikuImageSystemPackages({"WonderBrush"})

-- Add optional package without dependencies
AddOptionalHaikuImagePackages({"WebPositive"})

-- Suppress automatic dependency (allows testing different versions)
SuppressHaikuImagePackages({"SQLite"})


-- ============================================================================
-- USERS AND GROUPS
-- ============================================================================

-- Root user settings (defaults: "baron", "Root User")
HAIKU_ROOT_USER_NAME = "bond"
HAIKU_ROOT_USER_REAL_NAME = "James Bond"

-- Host name (no default)
HAIKU_IMAGE_HOST_NAME = "mybox"

-- Add user to image
-- Group ID 100 = "users" group (pre-existing)
AddUserToHaikuImage({
    name = "walter",
    uid = 1000,
    gid = 100,
    home = "/boot/home",
    shell = "/bin/bash",
    realname = "Just Walter"
})

-- Add group to image
AddGroupToHaikuImage({
    name = "party",
    gid = 101,
    members = {"baron", "walter"}
})


-- ============================================================================
-- BUILD PROFILES
-- ============================================================================

--[[
    Build profiles are named sets of settings for building images.
    Profile types:
        "disk"          - Install to device (implies HAIKU_DONT_CLEAR_IMAGE)
        "image"         - Plain disk image
        "vmware-image"  - VMware disk image
        "anyboot-image" - Custom bootable image (+4MB overhead)
        "install"       - Install to directory
]]

-- Define profiles
DefineBuildProfile("disk", "disk", "/dev/sda57")
DefineBuildProfile("qemu", "image", "haiku-qemu.image")
DefineBuildProfile("vmware", "vmware-image")
DefineBuildProfile("anyboot", "anyboot-image", "haiku-anyboot.iso")
DefineBuildProfile("crash", "vmware-image", "/home/user/vmware/CrashOMatic.vmdk")
DefineBuildProfile("install", "install", "/Haiku2")

-- Profile-specific settings
local profile = get_config("build_profile")
if profile == "disk" then
    AddOptionalHaikuImagePackages({"BeBook", "BeHappy", "Bluetooth", "Development", "OpenSSH", "OpenSSL", "Welcome"})
    AddHaikuImageSystemPackages({"bepdf", "git", "p7zip", "pe", "vision", "wonderbrush"})
elseif profile == "qemu" then
    HAIKU_IMAGE_SIZE = 200
elseif profile == "vmware" then
    HAIKU_IMAGE_SIZE = 400
    HAIKU_DONT_CLEAR_IMAGE = true
    AddOptionalHaikuImagePackages({"Development"})
    AddHaikuImageSystemPackages({"pe"})
elseif profile == "anyboot" then
    HAIKU_IMAGE_SIZE = 400
elseif profile == "crash" then
    HAIKU_IMAGE_SIZE = 1024
    HAIKU_DONT_CLEAR_IMAGE = true
    AddOptionalHaikuImagePackages({"Development"})
    AddHaikuImageSystemPackages({"pe"})
    CopyDirectoryToHaikuImage("home/Desktop", {source = "$(HAIKU_TOP)/../crash-tests"})
end

--[[
    Usage with xmake:
        xmake build @disk          -- Build with disk profile
        xmake build @vmware        -- Build with vmware profile
        xmake update @crash kernel -- Update kernel in crash profile image

    Default profiles are auto-defined:
        nightly-raw, nightly-anyboot, nightly-vmware
        minimal-raw, minimal-anyboot, minimal-vmware
        release-raw, release-anyboot, release-vmware
        install
]]


-- ============================================================================
-- CUSTOM SCRIPTS
-- ============================================================================

-- Scripts run when populating image
-- "early" - before anything copied
-- "late"  - after everything copied, before MIME database
HAIKU_IMAGE_EARLY_USER_SCRIPTS = {"$(HAIKU_TOP)/../early-image-script.sh"}
HAIKU_IMAGE_LATE_USER_SCRIPTS = {"$(HAIKU_TOP)/../late-image-script.sh"}


-- ============================================================================
-- SHELL SCRIPT GENERATION
-- ============================================================================

-- Generate sourceable shell scripts for testing
-- Creates file with variable definitions for shell scripts

MakeLocate("test.inc", "$(HAIKU_OUTPUT_DIR)")
Always("test.inc")

-- Define shell variable with build system value
AddVariableToScript("test.inc", "outputDir", "$(HAIKU_OUTPUT_DIR)")

-- Define variables pointing to build targets
AddTargetVariableToScript("test.inc", "bfs_shell", "bfsShell")
AddTargetVariableToScript("test.inc", "fs_shell_command", "fsShellCommand")

-- Use target name if no variable name given
AddTargetVariableToScript("test.inc", "<build>rc")


-- ============================================================================
-- THIRD PARTY INCLUSION / OPTIMIZATION
-- ============================================================================

-- Skip src directory inclusion (reduces parsing time)
-- Use only with DeferredSubInclude
HAIKU_DONT_INCLUDE_SRC = true

-- Schedule subdirectory for late inclusion
DeferredSubInclude({"HAIKU_TOP", "src", "tests", "add-ons", "kernel", "file_systems", "userlandfs"})

-- Include with alternative Jamfile name
-- "local" = only this directory uses alternative name
-- "global" = subdirectories also use alternative name
DeferredSubInclude({"HAIKU_TOP", "src", "3rdparty", "myproject"}, {
    jamfile = "Jamfile.haiku",
    scope = "local"
})


-- ============================================================================
-- HOOK FUNCTIONS
-- ============================================================================

--[[
    Override these functions to run code at different build stages:

    UserBuildConfigRulePostBuildTargets:
        After all Jamfiles processed, all targets known and located.

    UserBuildConfigRulePreImage:
        After image contents defined, before image generation scripts.

    UserBuildConfigRulePostImage:
        After image target fully defined.
]]

-- Example: Print target location after all targets defined
function UserBuildConfigRulePostBuildTargets()
    local styled_edit_loc = GetTargetLocation("StyledEdit")
    print("StyledEdit will appear at: " .. (styled_edit_loc or "unknown"))
end

function UserBuildConfigRulePreImage()
    print("Image contents defined, ready for image generation")
end

function UserBuildConfigRulePostImage()
    print("Image target fully defined")
end