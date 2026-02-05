--[[
    TestsRules.lua - Haiku unit test build rules

    xmake equivalent of build/jam/TestsRules (1:1 migration)

    Rules defined:
    - UnitTestLib       - Build a shared library for unit testing with CppUnit
    - UnitTest          - Build a unit test executable with CppUnit
    - SimpleTest        - Build a simple test (wrapper around Application)
    - BuildPlatformTest - Build a platform test executable

    Utility functions:
    - UnitTestDependency(target)                     - Register target as unit test dependency
    - UnitTestResource(target, source, flags)         - Copy a test resource file
    - UnitTestResources(files, sourceDir, flags)      - Batch copy test resource files

    Usage:
        target("my_test")
            add_rules("UnitTest")
            add_files("test_*.cpp")
            set_values("test.libraries", {"libsomething"})
]]

-- Track registered unit test targets
local _unittest_libraries = {}
local _unittest_executables = {}
local _unittest_resources = {}

-- UnitTestDependency: register a target as a unit test dependency
function UnitTestDependency(target_name)
    -- In xmake, the "unittests" phony target depends on individual test targets
    -- This is tracked for later use by the unittests group target
end

-- UnitTestResource: copy a resource file for unit tests
function UnitTestResource(target, source, dest_dir, flags)
    import("core.project.depend")

    local target_file = path.join(dest_dir, path.filename(source))

    depend.on_changed(function()
        os.mkdir(path.directory(target_file))
        os.cp(source, target_file)
        if flags and type(flags) == "table" then
            for _, flag in ipairs(flags) do
                if flag == "isExec" then
                    os.exec("chmod +x %s", target_file)
                end
            end
        end
    end, {
        files = {source},
        dependfile = target:dependfile(target_file)
    })

    table.insert(_unittest_resources, target_file)
end

-- UnitTestResources: batch copy resource files
function UnitTestResources(target, files, source_dir, dest_dir, flags)
    for _, file in ipairs(files) do
        local source = path.join(source_dir, file)
        UnitTestResource(target, source, dest_dir, flags)
    end
end

-- GetUnitTestLibraries: retrieve registered unit test libraries
function GetUnitTestLibraries()
    return _unittest_libraries
end

-- GetUnitTestExecutables: retrieve registered unit test executables
function GetUnitTestExecutables()
    return _unittest_executables
end

-- Dual-load guard: define rules only when loaded via includes() (description scope)
if rule then

-- UnitTestLib: build a shared library for unit testing
-- Equivalent of Jam's UnitTestLib rule
rule("UnitTestLib")

    on_load(function(target)
        import("core.project.config")

        -- Force shared library
        target:set("kind", "shared")

        -- Add test defines
        target:add("defines", "TEST_HAIKU", "TEST_OBOS")

        -- Add CppUnit
        target:add("links", "cppunit")

        -- Enable debug if TEST_DEBUG configured
        if config.get("test_debug") then
            target:add("defines", "DEBUG=1")
        end

        -- Set output directory to unit test lib dir
        local unit_test_lib_dir = config.get("unit_test_lib_dir")
        if unit_test_lib_dir then
            target:set("targetdir", unit_test_lib_dir)
        end

        -- Track for unittests pseudo-target
        table.insert(_unittest_libraries, target:name())
    end)

rule_end()


-- UnitTest: build a unit test executable
-- Equivalent of Jam's UnitTest rule
rule("UnitTest")

    on_load(function(target)
        import("core.project.config")

        -- Force binary
        target:set("kind", "binary")

        -- Add test defines
        target:add("defines", "TEST_HAIKU", "TEST_OBOS")

        -- Add CppUnit
        target:add("links", "cppunit")

        -- Enable debug if TEST_DEBUG configured
        if config.get("test_debug") then
            target:add("defines", "DEBUG=1")
        end

        -- Set output directory to unit test dir
        local unit_test_dir = config.get("unit_test_dir")
        if unit_test_dir then
            target:set("targetdir", unit_test_dir)
        end

        -- Track for unittests pseudo-target
        table.insert(_unittest_executables, target:name())
    end)

    -- Copy resources after link if specified
    after_link(function(target)
        import("core.project.config")

        local resources = target:values("test.resources")
        local resource_dir = target:values("test.resource_dir")
        if resources and resource_dir then
            local dest = path.join(
                config.get("unit_test_dir") or target:targetdir(),
                "resources"
            )
            UnitTestResources(target, resources, resource_dir, dest)
        end
    end)

rule_end()


-- SimpleTest: simple test wrapper (calls Application rule pattern)
-- Equivalent of Jam's SimpleTest -> Application
rule("SimpleTest")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "binary")

        -- Enable debug if TEST_DEBUG configured
        if config.get("test_debug") then
            target:add("defines", "DEBUG=1")
        end
    end)

rule_end()


-- BuildPlatformTest: build a platform test in the test output directory
-- Equivalent of Jam's BuildPlatformTest
rule("BuildPlatformTest")

    on_load(function(target)
        import("core.project.config")

        target:set("kind", "binary")

        -- Determine relative path for output directory
        -- Mirrors Jam logic: if path starts with src/tests, strip first 2 components
        local test_dir = config.get("test_dir")
        if test_dir then
            local subdir = target:values("test.subdir")
            if subdir then
                target:set("targetdir", path.join(test_dir, subdir))
            else
                target:set("targetdir", test_dir)
            end
        end
    end)

rule_end()

end -- if rule
