--[[
    support_kit_build - Support Kit for host tools
    Mirrors: src/build/libbe/support/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

-- Source directory
local kits_support = path.join(haiku_top, "src", "kits", "support")

-- Headers
local build_headers = path.join(haiku_top, "headers", "build")
local private_headers = path.join(haiku_top, "headers", "private")

target("support_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

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

    add_includedirs(
        build_headers,
        path.join(build_headers, "os"),
        path.join(build_headers, "os", "app"),
        path.join(build_headers, "os", "interface"),
        path.join(build_headers, "os", "support"),
        path.join(private_headers, "app"),
        path.join(private_headers, "interface"),
        path.join(private_headers, "shared"),
        path.join(private_headers, "support"),
        path.join(private_headers, "locale")  -- for Url.cpp
    )

    add_defines("ZSTD_ENABLED")
    add_cxxflags("-fPIC")
target_end()
