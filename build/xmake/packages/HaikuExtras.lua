--[[
    HaikuExtras.lua - Extra Haiku add-ons

    xmake equivalent of build/jam/packages/HaikuExtras

    This package contains:
    - Extra partitioning systems (architecture-specific)
    - Alternative control looks
    - Alternative decorators
    - Misc tools
]]

return function(context)
    local architecture = context.architecture

    local extrasPackage = "haiku_extras.hpkg"
    HaikuPackage(extrasPackage)

    -- ========================================================================
    -- Misc Tools
    -- ========================================================================
    AddFilesToPackage("bin", {"acpi_call"})

    -- ========================================================================
    -- Driver Oddities - Extra Partitioning Systems
    -- ========================================================================
    -- amiga_rdb is normally for m68k, but include for other archs in extras
    AddFilesToPackage("add-ons/kernel/partitioning_systems", {
        {name = "amiga_rdb", archs_exclude = {"m68k"}},
        {name = "apple", archs_exclude = {"ppc"}},
    })

    -- ========================================================================
    -- Visual Oddities
    -- ========================================================================

    -- Control Looks
    AddFilesToPackage("add-ons/control_look", {
        "BeControlLook",
        "FlatControlLook",
    })

    -- Decorators
    AddFilesToPackage("add-ons/decorators", {
        "BeDecorator",
        "FlatDecorator",
    })

    -- ========================================================================
    -- Build Package
    -- ========================================================================
    BuildHaikuPackage(extrasPackage, "haiku_extras")
end