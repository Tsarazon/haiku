--[[
    WebPositive.lua - WebPositive web browser package

    xmake equivalent of build/jam/packages/WebPositive

    This package is only built if the webpositive build feature is enabled.
]]

return function(context)
    local architecture = context.architecture

    -- Only build if webpositive feature is enabled
    if not FIsBuildFeatureEnabled("webpositive") then
        return
    end

    local webpositivePackage = "webpositive.hpkg"
    HaikuPackage(webpositivePackage)

    -- Application
    AddFilesToPackage("apps", {"WebPositive"})

    -- Deskbar menu entry
    AddSymlinkToPackage("data/deskbar/menu/Applications",
        "../../../../apps/WebPositive", "WebPositive")

    BuildHaikuPackage(webpositivePackage, "webpositive")
end