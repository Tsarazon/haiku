--[[
    storage_kit_build - Storage Kit for host tools
    Mirrors: src/build/libbe/storage/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

-- Local stubs
local build_storage = os.scriptdir()
-- Fallback
local kits_storage = path.join(haiku_top, "src", "kits", "storage")
local kits_mime = path.join(kits_storage, "mime")
local kits_sniffer = path.join(kits_storage, "sniffer")

target("storage_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

    add_rules("HostBeAPI", {
        private_build_headers = {"app", "kernel", "shared", "storage"},
        public_headers = {"add-ons/registrar"}
    })

    -- Local stub files
    add_files(path.join(build_storage, "AppFileInfo.cpp"))
    add_files(path.join(build_storage, "Directory.cpp"))
    add_files(path.join(build_storage, "Entry.cpp"))
    add_files(path.join(build_storage, "File.cpp"))
    add_files(path.join(build_storage, "MergedDirectory.cpp"))
    add_files(path.join(build_storage, "Mime.cpp"))
    add_files(path.join(build_storage, "MimeType.cpp"))
    add_files(path.join(build_storage, "Node.cpp"))
    add_files(path.join(build_storage, "NodeInfo.cpp"))
    add_files(path.join(build_storage, "Statable.cpp"))
    add_files(path.join(build_storage, "Volume.cpp"))

    -- From src/kits/storage/
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

    -- MIME sources
    add_files(path.join(kits_mime, "AppMetaMimeCreator.cpp"))
    add_files(path.join(kits_mime, "AssociatedTypes.cpp"))
    add_files(path.join(kits_mime, "Database.cpp"))
    add_files(path.join(kits_mime, "DatabaseDirectory.cpp"))
    add_files(path.join(kits_mime, "DatabaseLocation.cpp"))
    add_files(path.join(kits_mime, "database_support.cpp"))
    add_files(path.join(kits_mime, "InstalledTypes.cpp"))
    add_files(path.join(kits_mime, "MimeEntryProcessor.cpp"))
    add_files(path.join(kits_mime, "MimeInfoUpdater.cpp"))
    add_files(path.join(kits_mime, "MimeSniffer.cpp"))
    add_files(path.join(kits_mime, "MimeSnifferAddon.cpp"))
    add_files(path.join(kits_mime, "MimeSnifferAddonManager.cpp"))
    add_files(path.join(kits_mime, "SnifferRules.cpp"))
    add_files(path.join(kits_mime, "Supertype.cpp"))
    add_files(path.join(kits_mime, "SupportingApps.cpp"))
    add_files(path.join(kits_mime, "TextSnifferAddon.cpp"))

    -- Sniffer sources
    add_files(path.join(kits_sniffer, "CharStream.cpp"))
    add_files(path.join(kits_sniffer, "Err.cpp"))
    add_files(path.join(kits_sniffer, "DisjList.cpp"))
    add_files(path.join(kits_sniffer, "Pattern.cpp"))
    add_files(path.join(kits_sniffer, "PatternList.cpp"))
    add_files(path.join(kits_sniffer, "Parser.cpp"))
    add_files(path.join(kits_sniffer, "Range.cpp"))
    add_files(path.join(kits_sniffer, "RPattern.cpp"))
    add_files(path.join(kits_sniffer, "RPatternList.cpp"))
    add_files(path.join(kits_sniffer, "Rule.cpp"))
target_end()
