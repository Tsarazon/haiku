--[[
    HaikuDataTranslators.lua - Data translators and media plugins

    xmake equivalent of build/jam/packages/HaikuDataTranslators
]]

return function(context)
    local architecture = context.architecture

    local dataTranslatorsPackage = "haiku_datatranslators.hpkg"
    HaikuPackage(dataTranslatorsPackage)

    -- Translators
    AddFilesToPackage("add-ons/Translators", SYSTEM_ADD_ONS_TRANSLATORS)

    -- Media plugins
    AddFilesToPackage("add-ons/media/plugins", SYSTEM_ADD_ONS_MEDIA_PLUGINS)

    -- Print add-ons
    AddFilesToPackage("add-ons/Print", SYSTEM_ADD_ONS_PRINT)
    AddFilesToPackage("add-ons/Print/transport", SYSTEM_ADD_ONS_PRINT_TRANSPORT)

    BuildHaikuPackage(dataTranslatorsPackage, "haiku_datatranslators")
end