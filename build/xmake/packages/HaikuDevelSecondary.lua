--[[
    HaikuDevelSecondary.lua - Development package for secondary architecture

    xmake equivalent of build/jam/packages/HaikuDevelSecondary

    This package provides development files for secondary architectures.
]]

return function(context)
    local architecture = context.architecture
    local arch = context.TARGET_ARCH

    local haikuDevelPackage = "haiku_" .. architecture .. "_devel.hpkg"
    HaikuPackage(haikuDevelPackage)

    -- ========================================================================
    -- Glue Code
    -- ========================================================================
    AddFilesToPackage("develop/lib/" .. architecture, {
        {name = "crti.o", grist = "src!system!glue!arch!" .. arch .. "!" .. architecture},
        {name = "crtn.o", grist = "src!system!glue!arch!" .. arch .. "!" .. architecture},
        {name = "init_term_dyn.o", grist = "src!system!glue!" .. architecture},
        {name = "start_dyn.o", grist = "src!system!glue!" .. architecture},
        {name = "haiku_version_glue.o", grist = "src!system!glue!" .. architecture},
    })

    -- ========================================================================
    -- Additional Libraries
    -- ========================================================================
    local developmentLibs = {
        {name = "libroot_debug.so", revisioned = true, grist = architecture},
    }
    AddFilesToPackage("lib/" .. architecture, developmentLibs)

    -- ========================================================================
    -- Library Symlinks
    -- ========================================================================
    local systemLibs = HaikuImageGetSystemLibs()
    local allLibs = {}
    for _, lib in ipairs(systemLibs) do
        table.insert(allLibs, lib)
    end
    for _, lib in ipairs(developmentLibs) do
        table.insert(allLibs, lib)
    end

    for _, lib in ipairs(allLibs) do
        local libName = type(lib) == "table" and lib.name or lib
        AddSymlinkToPackage("develop/lib/" .. architecture,
            "../../../lib/" .. architecture, libName)

        local abiVersion = type(lib) == "table" and lib.abi_version
        if abiVersion then
            local abiVersionedLib = libName .. "." .. abiVersion
            AddSymlinkToPackage("develop/lib/" .. architecture,
                "../../../lib/" .. architecture, abiVersionedLib)
        end
    end

    -- ========================================================================
    -- Static Libraries
    -- ========================================================================
    AddFilesToPackage("develop/lib/" .. architecture, {
        {name = "libc.a", grist = architecture},
        {name = "libcolumnlistview.a", grist = architecture},
        {name = "liblocalestub.a", grist = architecture},
        {name = "libm.a", grist = architecture},
        {name = "libnetservices.a", grist = architecture},
        {name = "libpthread.a", grist = architecture},
        {name = "libshared.a", grist = architecture},
    })

    -- Modern C++ network services library
    AddFilesToPackage("develop/lib/" .. architecture, {
        {name = "libnetservices2.a", grist = architecture},
    })

    -- POSIX error code mapper
    AddFilesToPackage("develop/lib/" .. architecture, {"libposix_error_mapper.a"})

    -- Note: C++ headers come with the gcc package

    BuildHaikuPackage(haikuDevelPackage, "haiku_devel_secondary")
end