--[[
    app_kit_build - Application Kit for host tools
    Mirrors: src/build/libbe/app/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

-- Source directory
local kits_app = path.join(haiku_top, "src", "kits", "app")

-- Headers
local build_headers = path.join(haiku_top, "headers", "build")
local private_headers = path.join(haiku_top, "headers", "private")

target("app_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

    add_files(path.join(kits_app, "Application.cpp"))
    add_files(path.join(kits_app, "AppMisc.cpp"))
    add_files(path.join(kits_app, "Looper.cpp"))
    add_files(path.join(kits_app, "Message.cpp"))
    add_files(path.join(kits_app, "MessageAdapter.cpp"))
    add_files(path.join(kits_app, "Messenger.cpp"))
    add_files(path.join(kits_app, "MessageUtils.cpp"))
    add_files(path.join(kits_app, "TypeConstants.cpp"))

    add_includedirs(
        build_headers,
        path.join(build_headers, "os"),
        path.join(build_headers, "os", "app"),
        path.join(build_headers, "os", "kernel"),
        path.join(build_headers, "os", "support"),
        path.join(private_headers, "app"),
        path.join(private_headers, "kernel"),
        path.join(private_headers, "shared")
    )

    add_cxxflags("-fPIC")
target_end()
