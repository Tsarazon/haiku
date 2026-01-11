--[[
    UserBuildConfig.sample.lua - Example user customization for Haiku build

    Copy the parts you need to UserBuildConfig.lua
    See UserBuildConfig.ReadMe.lua for full documentation.
]]

-- ============================================================================
-- Adjusting Build Variables
-- ============================================================================

-- The following variables can be configured per subdirectory or per file:
--   CCFLAGS, CXXFLAGS, DEBUG, DEFINES, HDRS, LINKFLAGS, OPTIM, OPTIMIZE,
--   SYSHDRS, WARNINGS, HOST_WARNING_CCFLAGS, HOST_WARNING_CXXFLAGS,
--   TARGET_WARNING_CCFLAGS, TARGET_WARNING_CXXFLAGS, PLATFORM, SUPPORTED_PLATFORMS

-- Turn off warnings in directory src/system/kernel (local scope)
-- SetConfigVar("WARNINGS", {"HAIKU_TOP", "src", "system", "kernel"}, 0, "local")

-- Set debug level for src/system/boot/loader recursively (global scope)
-- SetConfigVar("DEBUG", {"HAIKU_TOP", "src", "system", "boot", "loader"}, 1, "global")

-- Add define for src/kits and all subdirectories
-- AppendToConfigVar("DEFINES", {"HAIKU_TOP", "src", "kits"}, "RUN_WITHOUT_REGISTRAR", "global")

-- Set debug level for a specific source file
-- SetDebugOnTarget("<src!bin!gdb!gdb!>haiku-nat.o", 1)

-- Enable SSL build feature
-- HAIKU_BUILD_FEATURE_SSL = true


-- ============================================================================
-- Haiku Image Settings
-- ============================================================================

-- Image name and location
-- HAIKU_IMAGE_NAME = "walter.image"
-- HAIKU_IMAGE_DIR = "/tmp"
-- HAIKU_IMAGE_SIZE = 100  -- MB

-- Volume label (default is "Haiku")
-- HAIKU_IMAGE_LABEL = "Walter"

-- VMware image name
-- HAIKU_VMWARE_IMAGE_NAME = "walter.vmdk"

-- Installation directory (for install target)
-- HAIKU_INSTALL_DIR = "/Haiku"

-- Don't zero out existing image (useful for partitions)
-- HAIKU_DONT_CLEAR_IMAGE = true

-- Package compression level (0-9, default 9)
-- 0 = no compression, 1 = fastest, 9 = best
-- For development, use 1 for faster builds
-- HAIKU_PACKAGE_COMPRESSION_LEVEL = 1

-- Override the Haiku revision string
-- HAIKU_REVISION = "hrev66666"

-- Only update specific targets on image updates
-- SetUpdateHaikuImageOnly(true)

-- Mark targets for image inclusion
-- SetHaikuIncludeInImage({"libbe.so", "kernel"})


-- ============================================================================
-- Adding Files to Image
-- ============================================================================

-- Add file to system/bin directory
-- AddFilesToHaikuImage("system/bin", {"crashing_app"})

-- Create symlink
-- AddSymlinkToHaikuImage("home/config/bin", {
--     target = "/bin/crashing_app",
--     name = "crash"
-- })

-- Add keymap settings
-- AddFilesToHaikuImage("home/config/settings", {
--     {source = "<keymap>US-International", dest = "Key_map"}
-- })

-- Add source directory to image
-- AddSourceDirectoryToHaikuImage({"src", "kits", "storage"})
-- AddSourceDirectoryToHaikuImage({"src", "tests", "servers", "debug"}, {alwaysUpdate = true})

-- Copy external directory to image
-- CopyDirectoryToHaikuImage("home/Desktop", {
--     source = "$(HAIKU_TOP)/../buildtools/jam",
--     dest = "jam-src",
--     exclude = {".git"},
--     excludePatterns = {"*/jam/./bin.*"}
-- })

-- Extract archive to image
-- ExtractArchiveToHaikuImage("develop/tools", {
--     archive = "/path/to/archive.zip"
-- })


-- ============================================================================
-- Optional Packages
-- ============================================================================

-- Add optional packages to image
-- AddHaikuImageSystemPackages({"WonderBrush"})

-- Add optional package without dependencies
-- AddOptionalHaikuImagePackages({"WebPositive"})
-- SuppressHaikuImagePackages({"SQLite"})


-- ============================================================================
-- Users and Groups
-- ============================================================================

-- Root user settings
-- HAIKU_ROOT_USER_NAME = "bond"
-- HAIKU_ROOT_USER_REAL_NAME = "James Bond"

-- Host name
-- HAIKU_IMAGE_HOST_NAME = "mybox"

-- Add user
-- AddUserToHaikuImage({
--     name = "walter",
--     uid = 1000,
--     gid = 100,
--     home = "/boot/home",
--     shell = "/bin/bash",
--     realname = "Just Walter"
-- })

-- Add group
-- AddGroupToHaikuImage({
--     name = "party",
--     gid = 101,
--     members = {"baron", "walter"}
-- })


-- ============================================================================
-- Build Profiles
-- ============================================================================

-- Define custom build profiles
-- DefineBuildProfile("disk", "disk", "/dev/sda57")
-- DefineBuildProfile("qemu", "image", "haiku-qemu.image")
-- DefineBuildProfile("vmware", "vmware-image")
-- DefineBuildProfile("anyboot", "anyboot-image", "haiku-anyboot.iso")
-- DefineBuildProfile("crash", "vmware-image", "/path/to/CrashOMatic.vmdk")
-- DefineBuildProfile("install", "install", "/Haiku2")

-- Profile-specific settings
-- local profile = get_config("build_profile")
-- if profile == "disk" then
--     AddOptionalHaikuImagePackages({"BeBook", "Development", "OpenSSH", "Welcome"})
--     AddHaikuImageSystemPackages({"bepdf", "git", "pe", "vision"})
-- elseif profile == "qemu" then
--     HAIKU_IMAGE_SIZE = 200
-- elseif profile == "vmware" then
--     HAIKU_IMAGE_SIZE = 400
--     HAIKU_DONT_CLEAR_IMAGE = true
--     AddOptionalHaikuImagePackages({"Development"})
--     AddHaikuImageSystemPackages({"pe"})
-- end


-- ============================================================================
-- Custom Scripts
-- ============================================================================

-- Scripts to run when populating image
-- HAIKU_IMAGE_EARLY_USER_SCRIPTS = {"$(HAIKU_TOP)/../early-image-script.sh"}
-- HAIKU_IMAGE_LATE_USER_SCRIPTS = {"$(HAIKU_TOP)/../late-image-script.sh"}


-- ============================================================================
-- Shell Script Generation
-- ============================================================================

-- Generate sourceable shell scripts for testing
-- CreateShellScript("test.inc", {
--     variables = {
--         outputDir = "$(HAIKU_OUTPUT_DIR)"
--     },
--     targets = {
--         bfsShell = "bfs_shell",
--         fsShellCommand = "fs_shell_command",
--         rc = "<build>rc"
--     }
-- })


-- ============================================================================
-- Third Party Inclusion / Optimization
-- ============================================================================

-- Skip src directory for faster parsing (use with DeferredSubInclude)
-- HAIKU_DONT_INCLUDE_SRC = true

-- Deferred include for subprojects
-- DeferredSubInclude({"HAIKU_TOP", "src", "tests", "add-ons", "kernel", "file_systems", "userlandfs"})
-- DeferredSubInclude({"HAIKU_TOP", "src", "3rdparty", "myproject"}, {jamfile = "Jamfile.haiku", scope = "local"})


-- ============================================================================
-- Hook Functions
-- ============================================================================

-- Override these functions for custom behavior at different build stages

-- Called after all build targets are defined
-- function UserBuildConfigRulePostBuildTargets()
--     print("All targets defined!")
-- end

-- Called after image contents defined, before image scripts
-- function UserBuildConfigRulePreImage()
--     print("Image contents ready")
-- end

-- Called after image target fully defined
-- function UserBuildConfigRulePostImage()
--     print("Image target complete")
-- end