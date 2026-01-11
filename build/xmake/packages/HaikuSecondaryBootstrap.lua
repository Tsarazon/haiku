--[[
    HaikuSecondaryBootstrap.lua - Bootstrap libraries for secondary architecture

    xmake equivalent of build/jam/packages/HaikuSecondaryBootstrap

    Minimal package for secondary architecture in bootstrap builds.
]]

return function(context)
    local architecture = context.architecture

    local haikuPackage = "haiku_" .. architecture .. ".hpkg"
    HaikuPackage(haikuPackage)

    -- Runtime loader for this architecture
    AddFilesToPackage(architecture, {
        {name = "runtime_loader", grist = architecture},
    })

    -- Libraries
    AddLibrariesToPackage("lib/" .. architecture,
        HaikuImageGetSystemLibs(),
        HaikuImageGetPrivateSystemLibs()
    )

    BuildHaikuPackage(haikuPackage, "haiku_secondary")
end