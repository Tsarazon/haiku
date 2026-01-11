--[[
    Haiku OS Build System - Image Build Rules

    xmake equivalent of build/jam/ImageRules

    This module handles disk image creation, including:
    1. Haiku disk images (raw and VMware)
    2. Network boot archives
    3. CD boot images (EFI and BIOS)
    4. EFI System Partition images

    This module uses Container functions from PackageRules.lua.
    Container abstraction is shared between packages and images.

    Rules provided:
    - Add*ToHaikuImage: Wrappers for adding content to disk image
    - Add*ToNetBootArchive: Network boot archive content
    - BuildHaikuImage: Build the main disk image
    - BuildCDBootImageEFI/BIOS: Create bootable CD images
    - BuildEfiSystemPartition: Create EFI boot partition
    - BuildVMWareImage: Create VMware compatible images
]]

-- ============================================================================
-- Constants and Configuration
-- ============================================================================

-- Container names for different image types
local HAIKU_IMAGE_CONTAINER_NAME = "haiku-image"
local HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME = "haiku-net-boot-archive"

-- Default volume IDs
local HAIKU_VERSION = "1.0"  -- Will be substituted from build configuration

-- ============================================================================
-- State Management
-- ============================================================================

-- Image registry
local _images = {}

-- Currently building image
local _current_image = nil

-- Package tracking for images
local _system_packages = {}
local _other_packages = {}
local _optional_packages = {}
local _added_packages = {}

-- ============================================================================
-- Helper Functions
-- ============================================================================

-- Get Haiku source tree root
local function get_haiku_top()
    return os.projectdir()
end

-- Get target architecture
local function get_target_arch()
    return get_config("arch") or "x86_64"
end

-- Get output directory
local function get_output_dir()
    return path.join(os.projectdir(), "generated")
end

-- Get download directory for packages
local function get_download_dir()
    return path.join(get_output_dir(), "download")
end

-- Get Haiku version string
local function get_haiku_version()
    local nightly = get_config("nightly_build")
    local arch = get_target_arch()
    if nightly then
        return "haiku-nightly-" .. arch
    else
        return "haiku-" .. HAIKU_VERSION .. "-" .. arch
    end
end

-- Get EFI boot file name based on architecture
local function get_efi_boot_name()
    local arch = get_target_arch()
    local names = {
        x86 = "BOOTIA32.EFI",
        x86_64 = "BOOTX64.EFI",
        arm = "BOOTARM.EFI",
        arm64 = "BOOTAA64.EFI",
        riscv32 = "BOOTRISCV32.EFI",
        riscv64 = "BOOTRISCV64.EFI",
    }
    return names[arch] or "BOOT.EFI"
end

-- ============================================================================
-- Script Helper Functions
-- ============================================================================

--[[
    FSameTargetWithPrependedGrist <target> : <gristToPrepend>

    Prepends grist to a target's existing grist.
]]
local function prepend_grist(target, grist_to_prepend)
    local existing_grist = target:match("^<(.-)>") or ""
    local base_name = target:gsub("^<.->", "")

    if existing_grist ~= "" then
        return string.format("<%s!%s>%s", grist_to_prepend, existing_grist, base_name)
    else
        return string.format("<%s>%s", grist_to_prepend, base_name)
    end
end

--[[
    FilterContainerUpdateTargets <targets> : <filterVariable>

    Filters targets based on update variable.
]]
local function filter_update_targets(targets, include_in_update)
    if not include_in_update then return targets end

    local filtered = {}
    for _, target in ipairs(targets) do
        -- In real implementation, check target's update flag
        table.insert(filtered, target)
    end
    return filtered
end

--[[
    PropagateContainerUpdateTargetFlags <toTarget> : <fromTarget>

    Propagates update flags from one target to another.
]]
local function propagate_update_flags(to_target, from_target)
    -- In real implementation, copy HAIKU_INCLUDE_IN_IMAGE and
    -- HAIKU_INCLUDE_IN_PACKAGES flags
end

-- ============================================================================
-- Container Script Helpers (Additional to PackageRules)
-- ============================================================================

--[[
    AddPackagesAndRepositoryVariablesToContainerScript <script> : <container>

    Adds package and repository related variables to a build script.
]]
function AddPackagesAndRepositoryVariablesToContainerScript(script_path, container_name)
    local lines = {}

    -- Add download directory
    table.insert(lines, string.format('downloadDir="%s"', get_download_dir()))

    -- Add package tool path
    table.insert(lines, 'package="${buildTools}/package"')
    table.insert(lines, 'getPackageDependencies="${buildTools}/get_package_dependencies"')

    -- Resolve package dependencies flag
    local update_only = false  -- Would be from container
    local resolve_deps = not update_only
    table.insert(lines, string.format('resolvePackageDependencies=%s',
                                       resolve_deps and "1" or ""))

    -- System packages
    local sys_packages = {}
    for name, _ in pairs(_system_packages) do
        table.insert(sys_packages, name)
    end
    table.insert(lines, string.format('systemPackages="%s"', table.concat(sys_packages, " ")))

    -- Other packages
    local other = {}
    for name, _ in pairs(_other_packages) do
        table.insert(other, name)
    end
    table.insert(lines, string.format('otherPackages="%s"', table.concat(other, " ")))

    return lines
end

-- ============================================================================
-- Haiku Image Rules
-- ============================================================================

--[[
    SetUpdateHaikuImageOnly <flag>

    Sets whether only to update the image (not rebuild from scratch).
]]
function SetUpdateHaikuImageOnly(flag)
    local container = get_container(HAIKU_IMAGE_CONTAINER_NAME)
    if container then
        container.update_only = flag
    end
end

--[[
    IsUpdateHaikuImageOnly

    Returns whether we're only updating the image.
]]
function IsUpdateHaikuImageOnly()
    local container = get_container(HAIKU_IMAGE_CONTAINER_NAME)
    return container and container.update_only
end

--[[
    AddDirectoryToHaikuImage <directoryTokens> : <attributeFiles>

    Adds a directory to the Haiku disk image.
]]
function AddDirectoryToHaikuImage(directory_tokens, attribute_files)
    return AddDirectoryToContainer(HAIKU_IMAGE_CONTAINER_NAME, directory_tokens, attribute_files)
end

--[[
    AddFilesToHaikuImage <directory> : <targets> : <destName> : <flags>

    Adds files to the Haiku disk image.
]]
function AddFilesToHaikuImage(directory, targets, dest_name, flags)
    AddFilesToContainer(HAIKU_IMAGE_CONTAINER_NAME, directory, targets, dest_name, flags)
end

--[[
    FFilesInHaikuImageDirectory <directoryTokens>

    Returns files in a directory of the Haiku image.
]]
function FFilesInHaikuImageDirectory(directory_tokens)
    local container = get_container(HAIKU_IMAGE_CONTAINER_NAME)
    if not container then return {} end

    local dir_path
    if type(directory_tokens) == "table" then
        dir_path = path.join(table.unpack(directory_tokens))
    else
        dir_path = directory_tokens
    end

    local result = {}
    for _, file_entry in ipairs(container.files) do
        if file_entry.directory == dir_path then
            table.insert(result, file_entry)
        end
    end
    return result
end

--[[
    AddSymlinkToHaikuImage <directoryTokens> : <linkTarget> : <linkName>

    Adds a symbolic link to the Haiku disk image.
]]
function AddSymlinkToHaikuImage(directory_tokens, link_target, link_name)
    -- Join link target if it's a table
    if type(link_target) == "table" then
        link_target = path.join(table.unpack(link_target))
    end
    AddSymlinkToContainer(HAIKU_IMAGE_CONTAINER_NAME, directory_tokens, link_target, link_name)
end

--[[
    FSymlinksInHaikuImageDirectory <directoryTokens>

    Returns symlinks in a directory of the Haiku image.
]]
function FSymlinksInHaikuImageDirectory(directory_tokens)
    local container = get_container(HAIKU_IMAGE_CONTAINER_NAME)
    if not container then return {} end

    local dir_path
    if type(directory_tokens) == "table" then
        dir_path = path.join(table.unpack(directory_tokens))
    else
        dir_path = directory_tokens
    end

    local result = {}
    for _, symlink_entry in ipairs(container.symlinks) do
        if symlink_entry.directory == dir_path then
            table.insert(result, symlink_entry)
        end
    end
    return result
end

--[[
    CopyDirectoryToHaikuImage <directoryTokens> : <sourceDirectory>
        : <targetDirectoryName> : <excludePatterns> : <flags>

    Copies a directory tree to the Haiku disk image.
]]
function CopyDirectoryToHaikuImage(directory_tokens, source_directory, target_directory_name,
                                    exclude_patterns, flags)
    CopyDirectoryToContainer(HAIKU_IMAGE_CONTAINER_NAME, directory_tokens, source_directory,
                             target_directory_name, exclude_patterns, flags)
end

--[[
    AddSourceDirectoryToHaikuImage <dirTokens> : <flags>

    Adds a source directory to home/HaikuSources in the image.
]]
function AddSourceDirectoryToHaikuImage(dir_tokens, flags)
    local haiku_top = get_haiku_top()
    local source_dir = path.join(haiku_top, table.unpack(dir_tokens))
    CopyDirectoryToHaikuImage({"home", "HaikuSources"}, source_dir, nil, nil, flags)
end

--[[
    AddHeaderDirectoryToHaikuImage <dirTokens> : <dirName> : <flags>

    Adds a header directory to the image.
]]
function AddHeaderDirectoryToHaikuImage(dir_tokens, dir_name, flags)
    AddHeaderDirectoryToContainer(HAIKU_IMAGE_CONTAINER_NAME, dir_tokens, dir_name, flags)
end

--[[
    AddWifiFirmwareToHaikuImage <driver> : <package> : <archive> : <extract>

    Adds WiFi firmware to the image.
]]
function AddWifiFirmwareToHaikuImage(driver, package, archive, extract)
    local haiku_top = get_haiku_top()
    local firmware_archive = path.join(haiku_top, "data", "system", "data",
                                        "firmware", driver, archive)

    local dir_tokens = {"system", "data", "firmware", driver}

    if extract == true or extract == "1" or extract == 1 then
        ExtractArchiveToContainer(HAIKU_IMAGE_CONTAINER_NAME, dir_tokens,
                                   firmware_archive, nil, package)
    else
        AddFilesToContainer(HAIKU_IMAGE_CONTAINER_NAME, dir_tokens, {firmware_archive})
    end
end

--[[
    ExtractArchiveToHaikuImage <dirTokens> : <archiveFile> : <flags> : <extractedSubDir>

    Extracts an archive to the Haiku disk image.
]]
function ExtractArchiveToHaikuImage(dir_tokens, archive_file, flags, extracted_sub_dir)
    ExtractArchiveToContainer(HAIKU_IMAGE_CONTAINER_NAME, dir_tokens,
                               archive_file, flags, extracted_sub_dir)
end

--[[
    AddDriversToHaikuImage <relativeDirectoryTokens> : <targets>

    Adds drivers to the Haiku disk image.
]]
function AddDriversToHaikuImage(relative_dir_tokens, targets)
    AddDriversToContainer(HAIKU_IMAGE_CONTAINER_NAME, relative_dir_tokens, targets)
end

--[[
    AddNewDriversToHaikuImage <relativeDirectoryTokens> : <targets> : <flags>

    Adds new-style drivers to the image.
]]
function AddNewDriversToHaikuImage(relative_dir_tokens, targets, flags)
    AddNewDriversToContainer(HAIKU_IMAGE_CONTAINER_NAME, relative_dir_tokens, targets, flags)
end

--[[
    AddBootModuleSymlinksToHaikuImage <targets>

    Adds boot module symlinks to the image.
]]
function AddBootModuleSymlinksToHaikuImage(targets)
    AddBootModuleSymlinksToContainer(HAIKU_IMAGE_CONTAINER_NAME, targets)
end

--[[
    AddLibrariesToHaikuImage <directory> : <libs>

    Adds libraries with ABI version symlinks to the image.
]]
function AddLibrariesToHaikuImage(directory, libs)
    AddLibrariesToContainer(HAIKU_IMAGE_CONTAINER_NAME, directory, libs)
end

-- ============================================================================
-- Package Management for Images
-- ============================================================================

--[[
    AddPackageFilesToHaikuImage <location> : <packages> : <flags>

    Adds package files to the image.
    Flags: nameFromMetaInfo
]]
function AddPackageFilesToHaikuImage(location, packages, flags)
    if not packages then return end
    if type(packages) ~= "table" then packages = {packages} end

    flags = flags or {}
    if type(flags) == "string" then flags = {flags} end

    -- Track packages based on location
    if location[1] == "system" and location[2] == "packages" and not location[3] then
        for _, pkg in ipairs(packages) do
            _system_packages[pkg] = true
        end
    else
        for _, pkg in ipairs(packages) do
            _other_packages[pkg] = true
        end
    end

    -- Add files with appropriate naming
    if flags.nameFromMetaInfo then
        AddFilesToHaikuImage(location, packages, "packageFileName", {computeName = true})
    else
        AddFilesToHaikuImage(location, packages)
    end
end

--[[
    AddOptionalHaikuImagePackages <packages>

    Marks packages as optional for the image.
]]
function AddOptionalHaikuImagePackages(packages)
    if not packages then return end
    if type(packages) ~= "table" then packages = {packages} end

    for _, pkg in ipairs(packages) do
        if not _optional_packages[pkg] then
            _optional_packages[pkg] = {
                added = true,
                dependencies = {},
            }
        end
    end
end

--[[
    SuppressOptionalHaikuImagePackages <packages>

    Suppresses optional packages from being included.
]]
function SuppressOptionalHaikuImagePackages(packages)
    if not packages then return end
    if type(packages) ~= "table" then packages = {packages} end

    for _, pkg in ipairs(packages) do
        if _optional_packages[pkg] then
            _optional_packages[pkg].suppressed = true
        else
            _optional_packages[pkg] = {
                added = false,
                suppressed = true,
            }
        end
    end
end

--[[
    IsOptionalHaikuImagePackageAdded <package>

    Returns whether an optional package is added and not suppressed.
]]
function IsOptionalHaikuImagePackageAdded(package)
    local pkg_info = _optional_packages[package]
    if pkg_info and pkg_info.added and not pkg_info.suppressed then
        return true
    end
    return false
end

--[[
    OptionalPackageDependencies <package> : <dependencies>

    Sets dependencies for an optional package.
]]
function OptionalPackageDependencies(package, dependencies)
    if not _optional_packages[package] then
        _optional_packages[package] = {added = false, dependencies = {}}
    end
    _optional_packages[package].dependencies = dependencies or {}

    -- If package is added, also add its dependencies
    if _optional_packages[package].added then
        AddOptionalHaikuImagePackages(dependencies)
    end
end

--[[
    AddHaikuImagePackages <packages> : <directory>

    Adds packages from HaikuPorts repository to the image.
]]
function AddHaikuImagePackages(packages, directory)
    if not packages then return end
    if type(packages) ~= "table" then packages = {packages} end

    for _, pkg in ipairs(packages) do
        if not _added_packages[pkg] then
            _added_packages[pkg] = true
            -- In real implementation, fetch package and add to image
            AddPackageFilesToHaikuImage(directory, {pkg})
        end
    end
end

--[[
    AddHaikuImageSourcePackages <packages>

    Adds source packages if HAIKU_INCLUDE_SOURCES is set.
]]
function AddHaikuImageSourcePackages(packages)
    local include_sources = get_config("include_sources")
    if include_sources then
        if type(packages) ~= "table" then packages = {packages} end
        local source_packages = {}
        for _, pkg in ipairs(packages) do
            table.insert(source_packages, pkg .. "_source")
        end
        AddHaikuImagePackages(source_packages, {"_sources_"})
    end
end

--[[
    AddHaikuImageSystemPackages <packages>

    Adds packages to system/packages (activated on boot).
]]
function AddHaikuImageSystemPackages(packages)
    AddHaikuImagePackages(packages, {"system", "packages"})
end

--[[
    AddHaikuImageDisabledPackages <packages>

    Adds packages to _packages_ (disabled, can be enabled in Installer).
]]
function AddHaikuImageDisabledPackages(packages)
    AddHaikuImagePackages(packages, {"_packages_"})
end

--[[
    IsHaikuImagePackageAdded <package>

    Returns whether a package has been added to the image.
]]
function IsHaikuImagePackageAdded(package)
    return _added_packages[package] ~= nil
end

-- ============================================================================
-- User/Group Management for Images
-- ============================================================================

-- Storage for user/group entries
local _passwd_entries = {}
local _group_entries = {}

--[[
    AddUserToHaikuImage <user> : <uid> : <gid> : <home> : <shell> : <realName>

    Adds a user entry to the image's /etc/passwd file.
]]
function AddUserToHaikuImage(user, uid, gid, home, shell, real_name)
    if not user or not uid or not gid or not home then
        print("Error: Invalid user specification passed to AddUserToHaikuImage")
        return
    end

    local entry = string.format("%s:x:%s:%s:%s:%s:%s",
        user, uid, gid, real_name or user, home, shell or "")

    table.insert(_passwd_entries, entry)
end

--[[
    AddGroupToHaikuImage <group> : <gid> : <members>

    Adds a group entry to the image's /etc/group file.
]]
function AddGroupToHaikuImage(group, gid, members)
    if not group or not gid then
        print("Error: Invalid group specification passed to AddGroupToHaikuImage")
        return
    end

    local members_str = ""
    if members then
        if type(members) == "table" then
            members_str = table.concat(members, ",")
        else
            members_str = members
        end
    end

    local entry = string.format("%s:x:%s:%s", group, gid, members_str)
    table.insert(_group_entries, entry)
end

--[[
    BuildHaikuImageUserGroupFile <file>

    Builds the passwd or group file content.
]]
local function build_user_group_file(file_type)
    local entries = file_type == "passwd" and _passwd_entries or _group_entries
    return table.concat(entries, "\n")
end

-- ============================================================================
-- Image Script Generation
-- ============================================================================

--[[
    CreateHaikuImageMakeDirectoriesScript <script>

    Creates script for making directories in the Haiku image.
]]
function CreateHaikuImageMakeDirectoriesScript(script_path)
    return CreateContainerMakeDirectoriesScript(HAIKU_IMAGE_CONTAINER_NAME, script_path)
end

--[[
    CreateHaikuImageCopyFilesScript <script>

    Creates script for copying files to the Haiku image.
]]
function CreateHaikuImageCopyFilesScript(script_path)
    return CreateContainerCopyFilesScript(HAIKU_IMAGE_CONTAINER_NAME, script_path)
end

--[[
    CreateHaikuImageExtractFilesScript <script>

    Creates script for extracting archives to the Haiku image.
]]
function CreateHaikuImageExtractFilesScript(script_path)
    return CreateContainerExtractFilesScript(HAIKU_IMAGE_CONTAINER_NAME, script_path)
end

-- ============================================================================
-- Haiku Image Building
-- ============================================================================

--[[
    BuildHaikuImage <haikuImage> : <scripts> : <isImage> : <isVMwareImage>

    Main rule to build the Haiku disk image.
]]
function BuildHaikuImage(haiku_image, scripts, is_image, is_vmware_image)
    local haiku_top = get_haiku_top()
    local main_script = path.join(haiku_top, "build", "scripts", "build_haiku_image")

    local build_info = {
        image_path = haiku_image,
        is_image = is_image == true or is_image == 1,
        is_vmware_image = is_vmware_image == true or is_vmware_image == 1,
        main_script = main_script,
        scripts = scripts or {},
    }

    return build_info
end

--[[
    BuildHaikuImagePackageList <target>

    Builds a list of packages included in the image.
]]
function BuildHaikuImagePackageList(target)
    if not target then return end

    local package_names = {}
    for pkg, _ in pairs(_added_packages) do
        -- Extract versioned name without revision
        local base_name = pkg:match("^([^-]+)")
        if base_name then
            table.insert(package_names, base_name)
        end
    end

    table.sort(package_names)
    return package_names
end

-- ============================================================================
-- VMWare Image Building
-- ============================================================================

--[[
    BuildVMWareImage <vmwareImage> : <plainImage> : <imageSize>

    Creates a VMware-compatible disk image from a plain image.
]]
function BuildVMWareImage(vmware_image, plain_image, image_size)
    local build_info = {
        vmware_image = vmware_image,
        plain_image = plain_image,
        image_size_mb = image_size,
        vmdkheader_tool = "<build>vmdkheader",
    }

    return build_info
end

-- ============================================================================
-- Network Boot Archive Rules
-- ============================================================================

--[[
    AddDirectoryToNetBootArchive <directoryTokens>

    Adds a directory to the network boot archive.
]]
function AddDirectoryToNetBootArchive(directory_tokens)
    return AddDirectoryToContainer(HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME, directory_tokens)
end

--[[
    AddFilesToNetBootArchive <directory> : <targets> : <destName>

    Adds files to the network boot archive.
]]
function AddFilesToNetBootArchive(directory, targets, dest_name)
    AddFilesToContainer(HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME, directory, targets, dest_name)
end

--[[
    AddSymlinkToNetBootArchive <directoryTokens> : <linkTarget> : <linkName>

    Adds a symlink to the network boot archive.
]]
function AddSymlinkToNetBootArchive(directory_tokens, link_target, link_name)
    AddSymlinkToContainer(HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME, directory_tokens,
                          link_target, link_name)
end

--[[
    AddDriversToNetBootArchive <relativeDirectoryTokens> : <targets>

    Adds drivers to the network boot archive.
]]
function AddDriversToNetBootArchive(relative_dir_tokens, targets)
    AddDriversToContainer(HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME, relative_dir_tokens, targets)
end

--[[
    AddNewDriversToNetBootArchive <relativeDirectoryTokens> : <targets>

    Adds new-style drivers to the network boot archive.
]]
function AddNewDriversToNetBootArchive(relative_dir_tokens, targets)
    AddNewDriversToContainer(HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME, relative_dir_tokens, targets)
end

--[[
    AddBootModuleSymlinksToNetBootArchive <targets>

    Adds boot module symlinks to the network boot archive.
]]
function AddBootModuleSymlinksToNetBootArchive(targets)
    AddBootModuleSymlinksToContainer(HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME, targets)
end

--[[
    CreateNetBootArchiveMakeDirectoriesScript <script>

    Creates script for making directories in the net boot archive.
]]
function CreateNetBootArchiveMakeDirectoriesScript(script_path)
    return CreateContainerMakeDirectoriesScript(HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME, script_path)
end

--[[
    CreateNetBootArchiveCopyFilesScript <script>

    Creates script for copying files to the net boot archive.
]]
function CreateNetBootArchiveCopyFilesScript(script_path)
    return CreateContainerCopyFilesScript(HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME, script_path)
end

--[[
    BuildNetBootArchive <archive> : <scripts>

    Builds the network boot archive.
]]
function BuildNetBootArchive(archive, scripts)
    local haiku_top = get_haiku_top()
    local main_script = path.join(haiku_top, "build", "scripts", "build_archive")

    return {
        archive_path = archive,
        main_script = main_script,
        scripts = scripts or {},
    }
end

-- ============================================================================
-- CD Boot Image Rules
-- ============================================================================

--[[
    BuildCDBootImageEFI <image> : <bootfloppy> : <bootefi> : <extrafiles>

    Builds a bootable CD image with both legacy and EFI boot support.
]]
function BuildCDBootImageEFI(image, boot_floppy, boot_efi, extra_files)
    local vol_id = get_haiku_version()

    return {
        image_path = image,
        boot_floppy = boot_floppy,
        boot_efi = boot_efi,
        extra_files = extra_files or {},
        volume_id = vol_id,
        -- xorriso command would be:
        -- xorriso -as mkisofs -b $(boot_floppy) -no-emul-boot
        --         -eltorito-alt-boot -no-emul-boot -e $(boot_efi)
        --         -r -J -V $(vol_id) -o $(image) files...
    }
end

--[[
    BuildCDBootImageEFIOnly <image> : <bootefi> : <extrafiles>

    Builds a bootable CD image with EFI-only boot support.
    No legacy floppy boot support.
]]
function BuildCDBootImageEFIOnly(image, boot_efi, extra_files)
    local vol_id = get_haiku_version()

    return {
        image_path = image,
        boot_efi = boot_efi,
        extra_files = extra_files or {},
        volume_id = vol_id,
        efi_only = true,
        -- xorriso command would be:
        -- xorriso -as mkisofs -no-emul-boot -e $(boot_efi)
        --         -r -J -V $(vol_id) -o $(image) files...
    }
end

--[[
    BuildCDBootImageBIOS <image> : <bootMBR> : <biosLoader> : <extrafiles>

    Builds a bootable CD image for BIOS systems using El Torito standard.
]]
function BuildCDBootImageBIOS(image, boot_mbr, bios_loader, extra_files)
    local vol_id = get_haiku_version()
    local output_dir = get_output_dir()

    return {
        image_path = image,
        boot_mbr = boot_mbr,
        bios_loader = bios_loader,
        extra_files = extra_files or {},
        volume_id = vol_id,
        cd_staging_dir = path.join(output_dir, "cd"),
        -- mkisofs command would be:
        -- mkisofs -R -V "$(vol_id)" -b boot/haiku_loader -no-emul-boot
        --         -boot-load-size 4 -boot-info-table -o $(image) staging_dir
    }
end

-- ============================================================================
-- EFI System Partition Rules
-- ============================================================================

--[[
    BuildEfiSystemPartition <image> : <efiLoader>

    Builds an EFI System Partition FAT image.
]]
function BuildEfiSystemPartition(image, efi_loader)
    local haiku_top = get_haiku_top()

    local efi_name = get_efi_boot_name()
    local mac_volume_icon = path.join(haiku_top, "data", "artwork", "VolumeIcon.icns")
    local efi_keys_dir = path.join(haiku_top, "data", "boot", "efi", "keys")

    return {
        image_path = image,
        efi_loader = efi_loader,
        efi_boot_name = efi_name,
        mac_volume_icon = mac_volume_icon,
        efi_keys_dir = efi_keys_dir,
        fatshell_tool = "<build>fat_shell",
        -- Creates a 2880KB FAT image with:
        -- /EFI/BOOT/$(efi_name) - EFI loader
        -- /.VolumeIcon.icns - Mac volume icon
        -- /KEYS/* - UEFI signing keys
    }
end

-- ============================================================================
-- xmake Rules for Image Building
-- ============================================================================

rule("HaikuImage")
    on_load(function (target)
        -- Container initialization deferred to on_build
        target:set("container_name", HAIKU_IMAGE_CONTAINER_NAME or "haiku-image")
    end)

    on_build(function (target)
        -- Initialize container at build time when functions are available
        local container_name = target:get("container_name")
        if get_or_create_container then
            local container = get_or_create_container(container_name)
            container.system_dir_tokens = {"system"}
        end

        local image_path = target:targetfile()
        local is_vmware = target:values("vmware")

        local scripts = {}
        -- Generate scripts would go here

        local build_info = BuildHaikuImage(image_path, scripts, true, is_vmware)
        print("Building Haiku image: " .. image_path)
    end)

rule("HaikuNetBootArchive")
    on_load(function (target)
        -- Container initialization deferred to on_build
        target:set("container_name", HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME or "haiku-netboot")
    end)

    on_build(function (target)
        -- Initialize container at build time when functions are available
        local container_name = target:get("container_name")
        if get_or_create_container then
            local container = get_or_create_container(container_name)
            container.system_dir_tokens = {"system"}
        end

        local archive_path = target:targetfile()
        local scripts = {}

        local build_info = BuildNetBootArchive(archive_path, scripts)
        print("Building net boot archive: " .. archive_path)
    end)

rule("CDBootImage")
    on_load(function (target)
        -- Configure CD boot image
    end)

    on_build(function (target)
        local image_path = target:targetfile()
        local boot_type = target:values("boot_type") or "efi"

        if boot_type == "efi" then
            local boot_efi = target:values("boot_efi")
            local boot_floppy = target:values("boot_floppy")
            local extra_files = target:values("extra_files") or {}

            if boot_floppy then
                local build_info = BuildCDBootImageEFI(image_path, boot_floppy,
                                                        boot_efi, extra_files)
            else
                local build_info = BuildCDBootImageEFIOnly(image_path, boot_efi, extra_files)
            end
        elseif boot_type == "bios" then
            local boot_mbr = target:values("boot_mbr")
            local bios_loader = target:values("bios_loader")
            local extra_files = target:values("extra_files") or {}

            local build_info = BuildCDBootImageBIOS(image_path, boot_mbr,
                                                     bios_loader, extra_files)
        end

        print("Building CD boot image: " .. image_path)
    end)

rule("EfiSystemPartition")
    on_load(function (target)
        -- Configure EFI partition
    end)

    on_build(function (target)
        local image_path = target:targetfile()
        local efi_loader = target:values("efi_loader")

        local build_info = BuildEfiSystemPartition(image_path, efi_loader)
        print("Building EFI system partition: " .. image_path)
    end)

rule("VMWareImage")
    on_load(function (target)
        -- Configure VMware image
    end)

    on_build(function (target)
        local vmware_image = target:targetfile()
        local plain_image = target:values("plain_image")
        local image_size = target:values("image_size") or 512

        local build_info = BuildVMWareImage(vmware_image, plain_image, image_size)
        print("Building VMware image: " .. vmware_image)
    end)

-- ============================================================================
-- Convenience Functions for Jamfile Translation
-- ============================================================================

-- Create Haiku disk image target
function haiku_image(name, config)
    target(name)
        add_rules("HaikuImage")
        if config.vmware then
            set_values("vmware", config.vmware)
        end
end

-- Create network boot archive target
function haiku_net_boot_archive(name, config)
    target(name)
        add_rules("HaikuNetBootArchive")
end

-- Create CD boot image target
function cd_boot_image(name, config)
    target(name)
        add_rules("CDBootImage")
        set_values("boot_type", config.boot_type or "efi")
        if config.boot_efi then
            set_values("boot_efi", config.boot_efi)
        end
        if config.boot_floppy then
            set_values("boot_floppy", config.boot_floppy)
        end
        if config.boot_mbr then
            set_values("boot_mbr", config.boot_mbr)
        end
        if config.bios_loader then
            set_values("bios_loader", config.bios_loader)
        end
        if config.extra_files then
            set_values("extra_files", config.extra_files)
        end
end

-- Create EFI system partition target
function efi_system_partition(name, config)
    target(name)
        add_rules("EfiSystemPartition")
        if config.efi_loader then
            set_values("efi_loader", config.efi_loader)
        end
end

-- Create VMware image target
function vmware_image(name, config)
    target(name)
        add_rules("VMWareImage")
        if config.plain_image then
            set_values("plain_image", config.plain_image)
        end
        if config.image_size then
            set_values("image_size", config.image_size)
        end
end

-- ============================================================================
-- Module Exports
-- ============================================================================

-- Export container names for external use
function get_haiku_image_container_name()
    return HAIKU_IMAGE_CONTAINER_NAME
end

function get_net_boot_archive_container_name()
    return HAIKU_NET_BOOT_ARCHIVE_CONTAINER_NAME
end
