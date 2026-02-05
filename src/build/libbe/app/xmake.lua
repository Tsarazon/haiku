--[[
    app_kit_build - Application Kit for host tools
    Mirrors: src/build/libbe/app/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

-- Local stubs in src/build/libbe/app/
local build_app = os.scriptdir()
-- Fallback for files not in build_app
local kits_app = path.join(haiku_top, "src", "kits", "app")

target("app_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

    add_rules("HostBeAPI", {
        private_build_headers = {"app", "kernel", "shared"}
    })

    -- Local stub files (simplified for host build)
    add_files(path.join(build_app, "Application.cpp"))
    add_files(path.join(build_app, "AppMisc.cpp"))
    add_files(path.join(build_app, "Looper.cpp"))
    add_files(path.join(build_app, "Message.cpp"))
    add_files(path.join(build_app, "Messenger.cpp"))

    -- From src/kits/app/ (not present locally)
    add_files(path.join(kits_app, "MessageAdapter.cpp"))
    add_files(path.join(kits_app, "MessageUtils.cpp"))
    add_files(path.join(kits_app, "TypeConstants.cpp"))
target_end()