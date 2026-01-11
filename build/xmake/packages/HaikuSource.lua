--[[
    HaikuSource.lua - Haiku source code package

    xmake equivalent of build/jam/packages/HaikuSource

    This package contains:
    - Haiku source code (src/, headers/, build/)
    - Build configuration files
]]

return function(context)
    local haikuSourcePackage = "haiku_source.hpkg"
    HaikuPackage(haikuSourcePackage)

    -- Source directories
    CopyDirectoryToPackage("develop/sources/haiku", "src", {
        source = "$(HAIKU_TOP)/src"
    })
    CopyDirectoryToPackage("develop/sources/haiku", "headers", {
        source = "$(HAIKU_TOP)/headers"
    })
    CopyDirectoryToPackage("develop/sources/haiku", "build", {
        source = "$(HAIKU_TOP)/build"
    })

    -- Build configuration files
    AddFilesToPackage("develop/sources/haiku", {
        "configure", "Jamfile", "Jamrules"
    }, {
        search_path = "$(HAIKU_TOP)"
    })

    BuildHaikuPackage(haikuSourcePackage, "haiku_source")
end