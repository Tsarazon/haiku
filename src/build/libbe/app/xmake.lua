--[[
    app_kit_build - Application Kit for host tools
    Mirrors: src/build/libbe/app/Jamfile
]]

local haiku_top = HAIKU_TOP or path.directory(path.directory(path.directory(path.directory(os.scriptdir()))))
local output_dir = HAIKU_OUTPUT_DIR or path.join(haiku_top, "spawned")
local obj_output = path.join(output_dir, "objects", "libbe_build")

-- Source directory
local kits_app = path.join(haiku_top, "src", "kits", "app")

target("app_kit_build")
    set_kind("object")
    set_targetdir(obj_output)

    -- Use HostBeAPI rule for build headers and -include BeOSBuildCompatibility.h
    add_rules("HostBeAPI")

    add_files(path.join(kits_app, "Application.cpp"))
    add_files(path.join(kits_app, "AppMisc.cpp"))
    add_files(path.join(kits_app, "Looper.cpp"))
    add_files(path.join(kits_app, "Message.cpp"))
    add_files(path.join(kits_app, "MessageAdapter.cpp"))
    add_files(path.join(kits_app, "Messenger.cpp"))
    add_files(path.join(kits_app, "MessageUtils.cpp"))
    add_files(path.join(kits_app, "TypeConstants.cpp"))

    -- Use HeadersRules for include paths (mirrors Jamfile)
    on_load(function(target)
        import("rules.HeadersRules")

        -- UsePrivateBuildHeaders app kernel shared (from Jamfile line 3)
        HeadersRules.UsePrivateBuildHeaders(target, {"app", "kernel", "shared"})
    end)
target_end()