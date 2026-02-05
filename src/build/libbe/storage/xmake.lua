--[[
    storage_kit_build - Storage Kit for host tools
    Mirrors: src/build/libbe/storage/Jamfile

    Uses local simplified versions of files when they exist,
    falls back to src/kits/storage/ for others.
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

-- Source directories
local local_storage = os.scriptdir()  -- src/build/libbe/storage (local simplified versions)
local kits_storage = path.join(haiku_top, "src", "kits", "storage")
local kits_storage_mime = path.join(kits_storage, "mime")
local kits_storage_sniffer = path.join(kits_storage, "sniffer")

target("storage_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

    -- Use HostBeAPI rule for build headers and -include BeOSBuildCompatibility.h
    add_rules("HostBeAPI")

    -- LOCAL simplified versions from src/build/libbe/storage/
    -- These have MimeType calls commented out with #if 0
    add_files(path.join(local_storage, "AppFileInfo.cpp"))
    add_files(path.join(local_storage, "Directory.cpp"))
    add_files(path.join(local_storage, "Entry.cpp"))
    add_files(path.join(local_storage, "File.cpp"))
    add_files(path.join(local_storage, "MergedDirectory.cpp"))
    add_files(path.join(local_storage, "Mime.cpp"))
    add_files(path.join(local_storage, "MimeType.cpp"))
    add_files(path.join(local_storage, "Node.cpp"))
    add_files(path.join(local_storage, "NodeInfo.cpp"))
    add_files(path.join(local_storage, "Statable.cpp"))
    add_files(path.join(local_storage, "Volume.cpp"))

    -- From src/kits/storage/ (no local version exists)
    add_files(path.join(kits_storage, "DriverSettings.cpp"))
    add_files(path.join(kits_storage, "EntryList.cpp"))
    add_files(path.join(kits_storage, "FdIO.cpp"))
    add_files(path.join(kits_storage, "FileIO.cpp"))
    add_files(path.join(kits_storage, "FindDirectory.cpp"))
    add_files(path.join(kits_storage, "OffsetFile.cpp"))
    add_files(path.join(kits_storage, "Path.cpp"))
    add_files(path.join(kits_storage, "ResourceFile.cpp"))
    add_files(path.join(kits_storage, "ResourceItem.cpp"))
    add_files(path.join(kits_storage, "Resources.cpp"))
    add_files(path.join(kits_storage, "ResourcesContainer.cpp"))
    add_files(path.join(kits_storage, "ResourceStrings.cpp"))
    add_files(path.join(kits_storage, "SymLink.cpp"))
    add_files(path.join(kits_storage, "storage_support.cpp"))

    -- MIME sources (from src/kits/storage/mime/)
    add_files(path.join(kits_storage_mime, "AppMetaMimeCreator.cpp"))
    add_files(path.join(kits_storage_mime, "AssociatedTypes.cpp"))
    add_files(path.join(kits_storage_mime, "Database.cpp"))
    add_files(path.join(kits_storage_mime, "DatabaseDirectory.cpp"))
    add_files(path.join(kits_storage_mime, "DatabaseLocation.cpp"))
    add_files(path.join(kits_storage_mime, "database_support.cpp"))
    add_files(path.join(kits_storage_mime, "InstalledTypes.cpp"))
    add_files(path.join(kits_storage_mime, "MimeEntryProcessor.cpp"))
    add_files(path.join(kits_storage_mime, "MimeInfoUpdater.cpp"))
    add_files(path.join(kits_storage_mime, "MimeSniffer.cpp"))
    add_files(path.join(kits_storage_mime, "MimeSnifferAddon.cpp"))
    add_files(path.join(kits_storage_mime, "MimeSnifferAddonManager.cpp"))
    add_files(path.join(kits_storage_mime, "SnifferRules.cpp"))
    add_files(path.join(kits_storage_mime, "Supertype.cpp"))
    add_files(path.join(kits_storage_mime, "SupportingApps.cpp"))
    add_files(path.join(kits_storage_mime, "TextSnifferAddon.cpp"))

    -- Sniffer sources (from src/kits/storage/sniffer/)
    add_files(path.join(kits_storage_sniffer, "CharStream.cpp"))
    add_files(path.join(kits_storage_sniffer, "Err.cpp"))
    add_files(path.join(kits_storage_sniffer, "DisjList.cpp"))
    add_files(path.join(kits_storage_sniffer, "Pattern.cpp"))
    add_files(path.join(kits_storage_sniffer, "PatternList.cpp"))
    add_files(path.join(kits_storage_sniffer, "Parser.cpp"))
    add_files(path.join(kits_storage_sniffer, "Range.cpp"))
    add_files(path.join(kits_storage_sniffer, "RPattern.cpp"))
    add_files(path.join(kits_storage_sniffer, "RPatternList.cpp"))
    add_files(path.join(kits_storage_sniffer, "Rule.cpp"))

    -- Headers (mirrors Jamfile: UsePrivateBuildHeaders app kernel shared storage)
    on_load(function(target)
        import("core.project.config")
        local top = config.get("haiku_top")
        local build_private = path.join(top, "headers", "build", "private")

        target:add("sysincludedirs",
            path.join(build_private, "app"),
            path.join(build_private, "kernel"),
            path.join(build_private, "shared"),
            path.join(build_private, "storage")
        )
    end)
target_end()
