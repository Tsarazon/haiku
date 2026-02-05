--[[
    storage_kit_build - Storage Kit for host tools
    Mirrors: src/build/libbe/storage/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

-- Source directories
local kits_storage = path.join(haiku_top, "src", "kits", "storage")
local kits_storage_mime = path.join(kits_storage, "mime")
local kits_storage_sniffer = path.join(kits_storage, "sniffer")

-- Headers
local build_headers = path.join(haiku_top, "headers", "build")
local private_headers = path.join(haiku_top, "headers", "private")

target("storage_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

    -- Main storage sources
    add_files(path.join(kits_storage, "AppFileInfo.cpp"))
    add_files(path.join(kits_storage, "Directory.cpp"))
    add_files(path.join(kits_storage, "DriverSettings.cpp"))
    add_files(path.join(kits_storage, "Entry.cpp"))
    add_files(path.join(kits_storage, "EntryList.cpp"))
    add_files(path.join(kits_storage, "FdIO.cpp"))
    add_files(path.join(kits_storage, "File.cpp"))
    add_files(path.join(kits_storage, "FileIO.cpp"))
    add_files(path.join(kits_storage, "FindDirectory.cpp"))
    add_files(path.join(kits_storage, "MergedDirectory.cpp"))
    add_files(path.join(kits_storage, "Mime.cpp"))
    add_files(path.join(kits_storage, "MimeType.cpp"))
    add_files(path.join(kits_storage, "Node.cpp"))
    add_files(path.join(kits_storage, "NodeInfo.cpp"))
    add_files(path.join(kits_storage, "OffsetFile.cpp"))
    add_files(path.join(kits_storage, "Path.cpp"))
    add_files(path.join(kits_storage, "ResourceFile.cpp"))
    add_files(path.join(kits_storage, "ResourceItem.cpp"))
    add_files(path.join(kits_storage, "Resources.cpp"))
    add_files(path.join(kits_storage, "ResourcesContainer.cpp"))
    add_files(path.join(kits_storage, "ResourceStrings.cpp"))
    add_files(path.join(kits_storage, "Statable.cpp"))
    add_files(path.join(kits_storage, "SymLink.cpp"))
    add_files(path.join(kits_storage, "Volume.cpp"))
    add_files(path.join(kits_storage, "storage_support.cpp"))

    -- MIME sources
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

    -- Sniffer sources
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

    add_includedirs(
        build_headers,
        path.join(build_headers, "os"),
        path.join(build_headers, "os", "app"),
        path.join(build_headers, "os", "kernel"),
        path.join(build_headers, "os", "storage"),
        path.join(build_headers, "os", "support"),
        path.join(private_headers, "app"),
        path.join(private_headers, "kernel"),
        path.join(private_headers, "shared"),
        path.join(private_headers, "storage")
    )

    add_cxxflags("-fPIC")
target_end()
