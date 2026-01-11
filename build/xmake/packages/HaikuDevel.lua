--[[
    HaikuDevel.lua - Haiku development package

    xmake equivalent of build/jam/packages/HaikuDevel

    This package contains:
    - Glue code (crti.o, crtn.o, etc.)
    - Kernel interface (kernel.so)
    - Development libraries
    - System headers
    - Debug tools
]]

return function(context)
    local architecture = context.architecture
    local arch = context.TARGET_ARCH

    local haikuDevelPackage = "haiku_devel.hpkg"
    HaikuPackage(haikuDevelPackage)

    -- ========================================================================
    -- Glue Code
    -- ========================================================================
    AddFilesToPackage("develop/lib", {
        {name = "crti.o", grist = "src!system!glue!arch!" .. arch .. "!" .. architecture},
        {name = "crtn.o", grist = "src!system!glue!arch!" .. arch .. "!" .. architecture},
        {name = "init_term_dyn.o", grist = "src!system!glue!" .. architecture},
        {name = "start_dyn.o", grist = "src!system!glue!" .. architecture},
        {name = "haiku_version_glue.o", grist = "src!system!glue!" .. architecture},
    })

    -- ========================================================================
    -- Kernel Interface
    -- ========================================================================
    AddFilesToPackage("develop/lib", {"kernel.so"}, {
        target_name = "_KERNEL_",
    })

    -- ========================================================================
    -- Additional Libraries
    -- ========================================================================
    local developmentLibs = {
        {name = "libroot_debug.so", revisioned = true},
    }
    AddFilesToPackage("lib", developmentLibs)

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
        AddSymlinkToPackage("develop/lib", "../../lib", libName)

        -- ABI versioned symlinks
        local abiVersion = type(lib) == "table" and lib.abi_version
        if abiVersion then
            local abiVersionedLib = libName .. "." .. abiVersion
            AddSymlinkToPackage("develop/lib", "../../lib", abiVersionedLib)
        end
    end

    -- ========================================================================
    -- Static Libraries
    -- ========================================================================
    AddFilesToPackage("develop/lib", {
        "libc.a",
        "libcolumnlistview.a",
        {name = "liblocalestub.a", grist = architecture},
        "libm.a",
        "libnetservices.a",
        "libpthread.a",
        "libprint.a",
        "libprintutils.a",
        {name = "libshared.a", grist = architecture},
    })

    -- Modern C++ network services library
    AddFilesToPackage("develop/lib", {
        {name = "libnetservices2.a", grist = architecture},
    })

    -- POSIX error code mapper
    AddFilesToPackage("develop/lib", {"libposix_error_mapper.a"})

    -- ========================================================================
    -- Directory Attributes
    -- ========================================================================
    AddDirectoryToPackage("develop", {rdef = "system-develop.rdef"})

    -- ========================================================================
    -- Headers
    -- ========================================================================
    AddHeaderDirectoryToPackage("config")
    AddHeaderDirectoryToPackage("glibc")
    AddHeaderDirectoryToPackage("libs/alm")
    AddHeaderDirectoryToPackage("libs/glut", "GL")
    AddHeaderDirectoryToPackage("libs/linprog")
    AddHeaderDirectoryToPackage("libs/thorvg")
    AddHeaderDirectoryToPackage("os")
    AddHeaderDirectoryToPackage("posix")

    -- Private headers
    AddHeaderDirectoryToPackage("private")

    -- FreeBSD compatibility headers
    CopyDirectoryToPackage("develop/headers/private/libs/compat", "freebsd_network", {
        source = "$(HAIKU_TOP)/src/libs/compat/freebsd_network",
        exclude = {"*.c", "*.cpp", "*.awk", "Jamfile", "miidevs", "usbdevs"},
    })
    CopyDirectoryToPackage("develop/headers/private/libs/compat", "freebsd_wlan", {
        source = "$(HAIKU_TOP)/src/libs/compat/freebsd_wlan",
        exclude = {"*.c", "Jamfile"},
    })

    -- be -> os symlink for compatibility
    AddSymlinkToPackage("develop/headers", "os", "be")

    -- BSD and GNU compatibility headers
    AddHeaderDirectoryToPackage("compatibility/bsd", "bsd")
    AddHeaderDirectoryToPackage("compatibility/gnu", "gnu")

    -- Note: C++ headers now come with the gcc package

    -- ========================================================================
    -- Deskbar Menu
    -- ========================================================================
    AddSymlinkToPackage("data/deskbar/menu/Applications",
        "../../../../apps/Debugger", "Debugger")

    -- ========================================================================
    -- Debugging Tools
    -- ========================================================================
    AddFilesToPackage("bin", {"leak_analyser.sh"}, {
        search_path = "$(HAIKU_TOP)/src/bin"
    })

    -- ========================================================================
    -- Build Package
    -- ========================================================================
    BuildHaikuPackage(haikuDevelPackage, "haiku_devel")
end