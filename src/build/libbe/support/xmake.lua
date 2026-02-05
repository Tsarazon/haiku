--[[
    support_kit_build - Support Kit for host tools
    Mirrors: src/build/libbe/support/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

local kits_support = path.join(haiku_top, "src", "kits", "support")

target("support_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

    add_rules("HostBeAPI", {
        private_build_headers = {"app", "interface", "shared", "support", "locale"},
        private_headers = {""}  -- for binary_compatibility/Support.h
    })

    add_files(path.join(kits_support, "Archivable.cpp"))
    add_files(path.join(kits_support, "BlockCache.cpp"))
    add_files(path.join(kits_support, "BufferIO.cpp"))
    add_files(path.join(kits_support, "ByteOrder.cpp"))
    add_files(path.join(kits_support, "CompressionAlgorithm.cpp"))
    add_files(path.join(kits_support, "DataIO.cpp"))
    add_files(path.join(kits_support, "DataPositionIOWrapper.cpp"))
    add_files(path.join(kits_support, "Flattenable.cpp"))
    add_files(path.join(kits_support, "Job.cpp"))
    add_files(path.join(kits_support, "JobQueue.cpp"))
    add_files(path.join(kits_support, "List.cpp"))
    add_files(path.join(kits_support, "Locker.cpp"))
    add_files(path.join(kits_support, "PointerList.cpp"))
    add_files(path.join(kits_support, "Referenceable.cpp"))
    add_files(path.join(kits_support, "String.cpp"))
    add_files(path.join(kits_support, "StringList.cpp"))
    add_files(path.join(kits_support, "Url.cpp"))
    add_files(path.join(kits_support, "ZlibCompressionAlgorithm.cpp"))
    add_files(path.join(kits_support, "ZstdCompressionAlgorithm.cpp"))

    add_defines("ZSTD_ENABLED")
target_end()