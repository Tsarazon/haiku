--[[
    UserlandFS.lua - Userland filesystem support package

    xmake equivalent of build/jam/packages/UserlandFS

    This package provides:
    - Userland filesystem kernel module
    - UserlandFS server
    - FUSE compatibility layer
]]

return function(context)
    local architecture = context.architecture

    local userlandFSPackage = "userland_fs.hpkg"
    HaikuPackage(userlandFSPackage)

    -- Kernel module
    AddFilesToPackage("add-ons/kernel/file_systems", {"userlandfs"})

    -- Server
    AddFilesToPackage("servers", {"userlandfs_server"})

    -- Libraries
    local userlandfsLibs = FFilterByBuildFeatures({
        "libuserlandfs_beos_kernel.so",
        "libuserlandfs_haiku_kernel.so",
        "libuserlandfs_fuse.so",
    })
    AddLibrariesToPackage("lib", userlandfsLibs)

    -- Library symlinks for development
    for _, lib in ipairs(userlandfsLibs) do
        local libName = type(lib) == "table" and lib.name or lib
        AddSymlinkToPackage("develop/lib", "/system/lib/" .. libName)
    end

    -- FUSE headers
    local fuseHeaders = {
        "fuse_common_compat.h",
        "fuse_common.h",
        "fuse_compat.h",
        "fuse.h",
        "fuse_lowlevel_compat.h",
        "fuse_lowlevel.h",
        "fuse_opt.h",
    }
    AddFilesToPackage("develop/headers/userlandfs/fuse", fuseHeaders, {
        grist = "userlandfs!fuse",
        search_path = "$(HAIKU_TOP)/headers/private/userlandfs/fuse"
    })

    -- Pkg-config file for FUSE compatibility
    AddFilesToPackage("develop/lib/pkgconfig", {"fuse.pc"}, {
        grist = "userlandfs!fuse",
        search_path = "$(HAIKU_TOP)/data/develop"
    })

    BuildHaikuPackage(userlandFSPackage, "userland_fs")
end