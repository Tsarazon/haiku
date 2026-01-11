--[[
    HaikuCrossDevel.lua - Cross-development sysroot package

    xmake equivalent of build/jam/packages/HaikuCrossDevel

    This package is for cross-development, providing a sysroot
    for building software with a cross-compiler on the target Haiku system.
    Main use: HaikuPorter cross-build environment.

    Creates both:
    - haiku_cross_devel_sysroot_*.hpkg (actual sysroot)
    - haiku_cross_devel_*.hpkg (wrapper containing sysroot package)
]]

return function(context)
    local architecture = context.architecture
    local primaryArchitecture = context.primary_architecture or architecture
    local isPrimaryArchitecture = context.is_primary
    local TARGET_ARCH = context.TARGET_ARCH

    -- Package name suffix
    local packageNameSuffix = primaryArchitecture
    local architectureSubDir = nil
    if not isPrimaryArchitecture then
        packageNameSuffix = primaryArchitecture .. "_" .. architecture
        architectureSubDir = architecture
    end

    local developCrossLibDirTokens = {"develop", "lib"}
    if architectureSubDir then
        table.insert(developCrossLibDirTokens, architectureSubDir)
    end
    local developCrossLibDir = table.concat(developCrossLibDirTokens, "/")

    -- Build for each stage: stage0, stage1, and full
    local stages = {"_stage0", "_stage1", ""}

    for _, stage in ipairs(stages) do
        -- ====================================================================
        -- Create the actual cross development sysroot package
        -- ====================================================================
        local haikuCrossDevelSysrootPackage =
            "haiku_cross_devel_sysroot" .. stage .. "_" .. packageNameSuffix .. ".hpkg"
        HaikuPackage(haikuCrossDevelSysrootPackage)

        if stage == "_stage0" then
            -- Stage 0: Bootstrap glue and stubbed libroot
            AddFilesToPackage(developCrossLibDir, {
                {name = "crti.o", grist = "bootstrap!src!system!glue!arch!" .. TARGET_ARCH .. "!" .. architecture},
                {name = "crtn.o", grist = "bootstrap!src!system!glue!arch!" .. TARGET_ARCH .. "!" .. architecture},
                {name = "init_term_dyn.o", grist = "bootstrap!src!system!glue!" .. architecture},
                {name = "start_dyn.o", grist = "bootstrap!src!system!glue!" .. architecture},
                {name = "haiku_version_glue.o", grist = "bootstrap!src!system!glue!" .. architecture},
            })

            AddLibrariesToPackage(developCrossLibDir, {
                {name = "libroot.so", grist = MultiArchDefaultGristFiles and "stubbed" or nil},
            })
        else
            -- Stage 1 and full: Regular glue code
            AddFilesToPackage(developCrossLibDir, {
                {name = "crti.o", grist = "src!system!glue!arch!" .. TARGET_ARCH .. "!" .. architecture},
                {name = "crtn.o", grist = "src!system!glue!arch!" .. TARGET_ARCH .. "!" .. architecture},
                {name = "init_term_dyn.o", grist = "src!system!glue!" .. architecture},
                {name = "start_dyn.o", grist = "src!system!glue!" .. architecture},
                {name = "haiku_version_glue.o", grist = "src!system!glue!" .. architecture},
            })

            -- Kernel interface (primary arch only)
            if isPrimaryArchitecture then
                AddFilesToPackage(developCrossLibDir, {"kernel.so"}, {
                    target_name = "_KERNEL_",
                })
            end

            -- Libraries
            local additionalLibraries = {}
            if stage ~= "_stage1" then
                additionalLibraries = MultiArchDefaultGristFiles and MultiArchDefaultGristFiles({
                    "libbe.so", "libnetwork.so", "libpackage.so"
                }) or {"libbe.so", "libnetwork.so", "libpackage.so"}
            end

            AddLibrariesToPackage(developCrossLibDir, {
                {name = "libbsd.so", grist = MultiArchDefaultGristFiles and architecture or nil},
                {name = "libroot.so", revisioned = true, grist = MultiArchDefaultGristFiles and architecture or nil},
                {name = "libnetwork.so", grist = MultiArchDefaultGristFiles and architecture or nil},
            })

            -- C++ runtime libraries
            if TargetLibstdc then
                AddLibrariesToPackage(developCrossLibDir, {TargetLibstdc()})
            end
            if TargetLibsupc then
                AddLibrariesToPackage(developCrossLibDir, {TargetLibsupc()})
            end

            AddLibrariesToPackage(developCrossLibDir, additionalLibraries)

            -- Static libraries
            AddFilesToPackage(developCrossLibDir, {
                {name = "liblocalestub.a", grist = architecture},
            })

            -- POSIX error code mapper
            AddFilesToPackage(developCrossLibDir, {
                {name = "libposix_error_mapper.a", grist = MultiArchDefaultGristFiles and architecture or nil},
            })
        end

        -- Headers (all stages)
        local developCrossHeadersDir = "develop/headers"
        for _, headerDir in ipairs({"config", "glibc", "os", "posix"}) do
            CopyDirectoryToPackage(developCrossHeadersDir, headerDir, {
                source = "$(HAIKU_TOP)/headers/" .. headerDir
            })
        end

        -- BSD and GNU compatibility headers
        for _, headerDir in ipairs({"bsd", "gnu"}) do
            CopyDirectoryToPackage(developCrossHeadersDir, headerDir, {
                source = "$(HAIKU_TOP)/headers/compatibility/" .. headerDir
            })
        end

        -- Note: C++ headers come with the DevelopmentBase package (not stage0)

        BuildHaikuPackage(haikuCrossDevelSysrootPackage, "haiku_cross_devel_sysroot")

        -- ====================================================================
        -- Create wrapper package containing the sysroot package
        -- ====================================================================
        local haikuCrossDevelPackage =
            "haiku_cross_devel" .. stage .. "_" .. packageNameSuffix .. ".hpkg"
        HaikuPackage(haikuCrossDevelPackage)

        -- Add the wrapped sysroot package
        AddFilesToPackage("develop/cross", {haikuCrossDevelSysrootPackage})

        BuildHaikuPackage(haikuCrossDevelPackage, "haiku_cross_devel")
    end
end