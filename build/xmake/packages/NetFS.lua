--[[
    NetFS.lua - Network filesystem package

    xmake equivalent of build/jam/packages/NetFS

    This package provides network filesystem support.
]]

return function(context)
    local netfsPackage = "netfs.hpkg"
    HaikuPackage(netfsPackage)

    -- UserlandFS module
    AddFilesToPackage("add-ons/userlandfs", {"netfs"})

    -- Servers
    AddFilesToPackage("servers", {"netfs_server", "authentication_server"})

    -- Tools
    AddFilesToPackage("bin", {
        "netfs_config",
        "netfs_server_prefs",
    })
    AddFilesToPackage("bin", {"netfs_mount"}, {
        source = "$(HAIKU_TOP)/data/bin/netfs_mount"
    })

    -- Interface definition
    AddFilesToPackage("data/userlandfs/file_systems", {"netfs"}, {
        source = "$(HAIKU_TOP)/data/userlandfs/file_systems/netfs"
    })

    -- Documentation
    AddFilesToPackage("documentation/add-ons", {"NetFS.html"}, {
        source = "$(HAIKU_TOP)/docs/add-ons/NetFS.html"
    })

    BuildHaikuPackage(netfsPackage, "netfs")
end