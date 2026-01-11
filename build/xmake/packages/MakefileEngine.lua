--[[
    MakefileEngine.lua - Makefile build system for Haiku apps

    xmake equivalent of build/jam/packages/MakefileEngine

    This package provides the traditional Makefile-based build system
    for Haiku applications.
]]

return function(context)
    local makefileEnginePackage = "makefile_engine.hpkg"
    HaikuPackage(makefileEnginePackage)

    -- Skeleton makefile and makefile-engine
    AddFilesToPackage("develop/etc", {
        {name = "Makefile", grist = "makefile-engine"},
        {name = "makefile-engine", grist = "makefile-engine"},
    }, {
        search_path = "$(HAIKU_TOP)/data/develop"
    })

    -- Documentation
    AddFilesToPackage("develop/documentation", {"makefile-engine.html"}, {
        source = "$(HAIKU_TOP)/docs/misc/makefile-engine.html"
    })

    BuildHaikuPackage(makefileEnginePackage, "makefile_engine")
end