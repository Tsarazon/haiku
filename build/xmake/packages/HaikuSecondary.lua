--[[
    HaikuSecondary.lua - Libraries for secondary architecture

    xmake equivalent of build/jam/packages/HaikuSecondary

    This package provides system libraries for secondary architectures.
]]

return function(context)
    local architecture = context.architecture

    local haikuPackage = "haiku_" .. architecture .. ".hpkg"
    HaikuPackage(haikuPackage)

    -- ========================================================================
    -- Libraries
    -- ========================================================================
    AddLibrariesToPackage("lib/" .. architecture,
        HaikuImageGetSystemLibs(),
        HaikuImageGetPrivateSystemLibs()
    )

    -- ========================================================================
    -- Add-ons
    -- ========================================================================
    local addOnsDir = "add-ons/" .. architecture

    -- Translators
    AddFilesToPackage(addOnsDir .. "/Translators",
        MultiArchDefaultGristFiles and
            MultiArchDefaultGristFiles(SYSTEM_ADD_ONS_TRANSLATORS) or
            SYSTEM_ADD_ONS_TRANSLATORS
    )

    -- Media add-ons
    AddFilesToPackage(addOnsDir .. "/media", SYSTEM_ADD_ONS_MEDIA)

    -- Media plugins
    AddFilesToPackage(addOnsDir .. "/media/plugins",
        MultiArchDefaultGristFiles and
            MultiArchDefaultGristFiles(SYSTEM_ADD_ONS_MEDIA_PLUGINS) or
            SYSTEM_ADD_ONS_MEDIA_PLUGINS
    )

    BuildHaikuPackage(haikuPackage, "haiku_secondary")
end