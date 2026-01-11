--[[
    HaikuLoader.lua - Haiku boot loader package

    xmake equivalent of build/jam/packages/HaikuLoader

    This package contains the boot loader.
    It must be uncompressed so stage one loader can execute it directly.
]]

return function(context)
    local haikuLoaderPackage = "haiku_loader.hpkg"
    HaikuPackage(haikuLoaderPackage)

    -- Boot loader
    AddFilesToPackage("", {
        "haiku_loader." .. (HAIKU_KERNEL_PLATFORM or "efi"),
    })

    -- Force no compression so stage one loader can directly execute
    SetPackageCompressionLevel(haikuLoaderPackage, 0)

    BuildHaikuPackage(haikuLoaderPackage, "haiku_loader")
end