--[[
    TestsRules.lua - Unit test build rules

    xmake equivalent of build/jam/TestsRules

    This module provides rules for building unit tests using the CppUnit
    framework. It defines rules for test executables, test libraries,
    and platform-specific tests.

    Rules defined:
    - UnitTestDependency  - Add dependency to unittests pseudo target
    - UnitTestLib         - Build unit test shared library
    - UnitTest            - Build unit test executable
    - TestObjects         - Compile test objects with test defines
    - SimpleTest          - Simple test wrapper around Application
    - BuildPlatformTest   - Platform-specific test executable

    Key concepts:
    - TEST_HAIKU/TEST_OBOS defines are set for test code
    - TEST_DEBUG enables debug mode for tests
    - CppUnit library (libcppunit.so) is linked automatically
    - libbe_test platform has special handling
]]

-- ============================================================================
-- Module State
-- ============================================================================

-- Track unit test dependencies
local _unittest_deps = {}

-- Unit test directories (set from config or defaults)
local _unit_test_dir = nil
local _unit_test_lib_dir = nil

-- ============================================================================
-- Directory Configuration
-- ============================================================================

--[[
    GetUnitTestDir()

    Get the unit test output directory.

    Returns:
        Path to unit test directory
]]
function GetUnitTestDir()
    if not _unit_test_dir then
        _unit_test_dir = get_config("TARGET_UNIT_TEST_DIR")
            or path.join(get_config("HAIKU_TEST_DIR") or "generated/tests", "unittests")
    end
    return _unit_test_dir
end

--[[
    GetUnitTestLibDir()

    Get the unit test library output directory.

    Returns:
        Path to unit test library directory
]]
function GetUnitTestLibDir()
    if not _unit_test_lib_dir then
        _unit_test_lib_dir = get_config("TARGET_UNIT_TEST_LIB_DIR")
            or path.join(GetUnitTestDir(), "lib")
    end
    return _unit_test_lib_dir
end

--[[
    SetUnitTestDir(dir)

    Set the unit test output directory.

    Parameters:
        dir - Directory path
]]
function SetUnitTestDir(dir)
    _unit_test_dir = dir
end

--[[
    SetUnitTestLibDir(dir)

    Set the unit test library output directory.

    Parameters:
        dir - Directory path
]]
function SetUnitTestLibDir(dir)
    _unit_test_lib_dir = dir
end

-- ============================================================================
-- Test Platform Detection
-- ============================================================================

--[[
    IsLibbeTestPlatform()

    Check if we're building for the libbe_test platform.

    Returns:
        true if TARGET_PLATFORM is libbe_test
]]
function IsLibbeTestPlatform()
    return get_config("TARGET_PLATFORM") == "libbe_test"
end

--[[
    IsTestDebug()

    Check if TEST_DEBUG is enabled.

    Returns:
        true if TEST_DEBUG is set
]]
function IsTestDebug()
    return get_config("TEST_DEBUG") and true or false
end

-- ============================================================================
-- Test Defines
-- ============================================================================

--[[
    GetTestDefines()

    Get the standard defines for test code.

    The original Jam code sets TEST_HAIKU and TEST_OBOS for both
    libbe_test and regular platforms.

    Returns:
        Table of define strings
]]
function GetTestDefines()
    return {"TEST_HAIKU", "TEST_OBOS"}
end

-- ============================================================================
-- Dependency Management
-- ============================================================================

--[[
    UnitTestDependency(target)

    Add a target as a dependency of the "unittests" pseudo target.

    Equivalent to Jam:
        rule UnitTestDependency { Depends unittests : $(1) ; }

    Parameters:
        target - Target name to add as dependency
]]
function UnitTestDependency(target)
    if target then
        table.insert(_unittest_deps, target)
    end
end

--[[
    GetUnitTestDependencies()

    Get all registered unit test dependencies.

    Returns:
        Table of dependency target names
]]
function GetUnitTestDependencies()
    return _unittest_deps
end

--[[
    ClearUnitTestDependencies()

    Clear all registered unit test dependencies.
]]
function ClearUnitTestDependencies()
    _unittest_deps = {}
end

-- ============================================================================
-- CppUnit Integration
-- ============================================================================

--[[
    UseCppUnitObjectHeaders(sources)

    Set up CppUnit headers for source files.

    This is a placeholder that integrates with the CppUnit headers
    setup from HeadersRules.

    Parameters:
        sources - Source files that need CppUnit headers
]]
function UseCppUnitObjectHeaders(sources)
    -- In xmake, this would typically be handled by adding
    -- the CppUnit include path to the target
    -- The actual implementation depends on HeadersRules.lua
    -- For now, we just record that these sources use CppUnit
end

-- ============================================================================
-- Test Rules
-- ============================================================================

--[[
    UnitTestLib(config)

    Build a unit test shared library.

    Equivalent to Jam:
        rule UnitTestLib { }

    Parameters:
        config - Table with:
            name      - Library name
            sources   - Source files
            libraries - Additional libraries to link (optional)

    The library will:
    - Be placed in TARGET_UNIT_TEST_LIB_DIR
    - Link against libcppunit.so
    - Have TEST_HAIKU and TEST_OBOS defined
    - Depend on libbe_test.so if building for libbe_test platform
]]
function UnitTestLib(cfg)
    local name = cfg.name
    local sources = cfg.sources or {}
    local libraries = cfg.libraries or {}

    if not name then
        error("UnitTestLib: name is required")
    end

    -- Add libcppunit.so to libraries
    table.insert(libraries, "libcppunit.so")

    return {
        type = "unittest_lib",
        name = name,
        sources = sources,
        libraries = libraries,
        defines = GetTestDefines(),
        debug = IsTestDebug(),
        output_dir = GetUnitTestLibDir(),
        depends_libbe_test = IsLibbeTestPlatform(),
    }
end

--[[
    UnitTest(config)

    Build a unit test executable.

    Equivalent to Jam:
        rule UnitTest { }

    Parameters:
        config - Table with:
            name      - Test executable name
            sources   - Source files
            libraries - Additional libraries to link (optional)
            resources - Resource files (optional)

    The test will:
    - Be placed in TARGET_UNIT_TEST_DIR
    - Link against libcppunit.so
    - Have TEST_HAIKU and TEST_OBOS defined
    - Depend on libbe_test.so if building for libbe_test platform
]]
function UnitTest(cfg)
    local name = cfg.name
    local sources = cfg.sources or {}
    local libraries = cfg.libraries or {}
    local resources = cfg.resources or {}

    if not name then
        error("UnitTest: name is required")
    end

    -- Add libcppunit.so to libraries
    table.insert(libraries, "libcppunit.so")

    return {
        type = "unittest",
        name = name,
        sources = sources,
        libraries = libraries,
        resources = resources,
        defines = GetTestDefines(),
        debug = IsTestDebug(),
        output_dir = GetUnitTestDir(),
        depends_libbe_test = IsLibbeTestPlatform(),
    }
end

--[[
    TestObjects(sources)

    Compile test objects with appropriate defines.

    Equivalent to Jam:
        rule TestObjects { }

    Parameters:
        sources - Source files to compile

    Returns:
        Configuration for test objects
]]
function TestObjects(sources)
    if type(sources) == "string" then
        sources = {sources}
    end

    return {
        type = "test_objects",
        sources = sources,
        defines = GetTestDefines(),
    }
end

--[[
    SimpleTest(config)

    Create a simple test executable.

    Equivalent to Jam:
        rule SimpleTest { }

    This is a wrapper around the Application rule with TEST_DEBUG support.

    Parameters:
        config - Table with:
            name      - Test executable name
            sources   - Source files
            libraries - Libraries to link (optional)
            resources - Resource files (optional)
]]
function SimpleTest(cfg)
    local name = cfg.name
    local sources = cfg.sources or {}
    local libraries = cfg.libraries or {}
    local resources = cfg.resources or {}

    if not name then
        error("SimpleTest: name is required")
    end

    return {
        type = "simple_test",
        name = name,
        sources = sources,
        libraries = libraries,
        resources = resources,
        debug = IsTestDebug(),
    }
end

--[[
    BuildPlatformTest(config)

    Build a platform-specific test executable.

    Equivalent to Jam:
        rule BuildPlatformTest { }

    Parameters:
        config - Table with:
            name    - Test executable name
            sources - Source files
            subdir_tokens - Current subdirectory tokens (optional)

    The test will be placed in HAIKU_TEST_DIR/<relative_path>
    where relative_path is derived from the subdirectory tokens.
]]
function BuildPlatformTest(cfg)
    local name = cfg.name
    local sources = cfg.sources or {}
    local subdir_tokens = cfg.subdir_tokens or {}

    if not name then
        error("BuildPlatformTest: name is required")
    end

    -- Calculate relative path
    -- Original Jam logic:
    -- if [ FIsPrefix src tests : $(SUBDIR_TOKENS) ] {
    --     relPath = $(SUBDIR_TOKENS[3-]) ;
    -- } else {
    --     relPath = $(SUBDIR_TOKENS[2-]) ;
    -- }
    local rel_path = ""
    if #subdir_tokens >= 2 then
        if subdir_tokens[1] == "src" and subdir_tokens[2] == "tests" then
            -- Skip first two tokens (src, tests)
            for i = 3, #subdir_tokens do
                if rel_path ~= "" then
                    rel_path = rel_path .. "/"
                end
                rel_path = rel_path .. subdir_tokens[i]
            end
        else
            -- Skip first token
            for i = 2, #subdir_tokens do
                if rel_path ~= "" then
                    rel_path = rel_path .. "/"
                end
                rel_path = rel_path .. subdir_tokens[i]
            end
        end
    end

    local test_dir = get_config("HAIKU_TEST_DIR") or "generated/tests"
    local output_dir = rel_path ~= "" and path.join(test_dir, rel_path) or test_dir

    return {
        type = "platform_test",
        name = name,
        sources = sources,
        output_dir = output_dir,
    }
end

-- ============================================================================
-- xmake Rule Definitions
-- ============================================================================

--[[
    Rule: unittest_lib

    xmake rule for building unit test shared libraries.
]]
rule("unittest_lib")
    set_extensions(".cpp", ".c")

    on_load(function (target)
        -- Set output directory
        local output_dir = GetUnitTestLibDir()
        target:set("targetdir", output_dir)

        -- Add test defines
        for _, def in ipairs(GetTestDefines()) do
            target:add("defines", def)
        end

        -- Add CppUnit dependency
        target:add("links", "cppunit")

        -- Enable debug if TEST_DEBUG is set
        if IsTestDebug() then
            target:set("symbols", "debug")
            target:set("optimize", "none")
        end

        -- Add libbe_test dependency if needed
        if IsLibbeTestPlatform() then
            target:add("deps", "libbe_test")
        end
    end)

    after_build(function (target)
        -- Register as unit test dependency
        UnitTestDependency(target:name())
    end)
rule_end()

--[[
    Rule: unittest

    xmake rule for building unit test executables.
]]
rule("unittest")
    set_extensions(".cpp", ".c")

    on_load(function (target)
        -- Set output directory
        local output_dir = GetUnitTestDir()
        target:set("targetdir", output_dir)

        -- Add test defines
        for _, def in ipairs(GetTestDefines()) do
            target:add("defines", def)
        end

        -- Add CppUnit dependency
        target:add("links", "cppunit")

        -- Enable debug if TEST_DEBUG is set
        if IsTestDebug() then
            target:set("symbols", "debug")
            target:set("optimize", "none")
        end

        -- Add libbe_test dependency if needed
        if IsLibbeTestPlatform() then
            target:add("deps", "libbe_test")
        end
    end)

    after_build(function (target)
        -- Register as unit test dependency
        UnitTestDependency(target:name())
    end)
rule_end()

--[[
    Rule: simple_test

    xmake rule for building simple test executables.
]]
rule("simple_test")
    set_extensions(".cpp", ".c")

    on_load(function (target)
        -- Enable debug if TEST_DEBUG is set
        if IsTestDebug() then
            target:set("symbols", "debug")
            target:set("optimize", "none")
        end
    end)
rule_end()

--[[
    Rule: platform_test

    xmake rule for building platform-specific test executables.
]]
rule("platform_test")
    set_extensions(".cpp", ".c")

    on_load(function (target)
        -- Output directory is set per-target based on subdir_tokens
        -- This is handled in BuildPlatformTest()
    end)
rule_end()

-- ============================================================================
-- Helper Functions for xmake Targets
-- ============================================================================

--[[
    unittest_lib_target(name, config)

    Helper to create a unit test library target in xmake.

    Parameters:
        name   - Target name
        config - Configuration table with sources, libraries
]]
function unittest_lib_target(name, cfg)
    target(name)
        set_kind("shared")
        add_rules("unittest_lib")

        if cfg.sources then
            add_files(cfg.sources)
        end

        if cfg.libraries then
            for _, lib in ipairs(cfg.libraries) do
                add_deps(lib)
            end
        end
    target_end()
end

--[[
    unittest_target(name, config)

    Helper to create a unit test executable target in xmake.

    Parameters:
        name   - Target name
        config - Configuration table with sources, libraries, resources
]]
function unittest_target(name, cfg)
    target(name)
        set_kind("binary")
        add_rules("unittest")

        if cfg.sources then
            add_files(cfg.sources)
        end

        if cfg.libraries then
            for _, lib in ipairs(cfg.libraries) do
                add_deps(lib)
            end
        end

        -- Handle resources via HaikuResources rule from BeOSRules
        if cfg.resources and #cfg.resources > 0 then
            add_rules("HaikuResources")
            for _, res in ipairs(cfg.resources) do
                if res:match("%.rdef$") then
                    -- .rdef files are compiled via ResComp
                    add_files(res)
                elseif res:match("%.rsrc$") then
                    -- .rsrc files are added directly
                    add_values("resources", res)
                end
            end
        end
    target_end()
end

--[[
    simple_test_target(name, config)

    Helper to create a simple test executable target in xmake.

    Parameters:
        name   - Target name
        config - Configuration table with sources, libraries, resources
]]
function simple_test_target(name, cfg)
    target(name)
        set_kind("binary")
        add_rules("simple_test")

        if cfg.sources then
            add_files(cfg.sources)
        end

        if cfg.libraries then
            for _, lib in ipairs(cfg.libraries) do
                add_deps(lib)
            end
        end

        -- Handle resources via HaikuResources rule from BeOSRules
        if cfg.resources and #cfg.resources > 0 then
            add_rules("HaikuResources")
            for _, res in ipairs(cfg.resources) do
                if res:match("%.rdef$") then
                    -- .rdef files are compiled via ResComp
                    add_files(res)
                elseif res:match("%.rsrc$") then
                    -- .rsrc files are added directly
                    add_values("resources", res)
                end
            end
        end
    target_end()
end

--[[
    platform_test_target(name, config)

    Helper to create a platform test executable target in xmake.

    Parameters:
        name   - Target name
        config - Configuration table with sources, output_dir
]]
function platform_test_target(name, cfg)
    target(name)
        set_kind("binary")
        add_rules("platform_test")

        if cfg.output_dir then
            set_targetdir(cfg.output_dir)
        end

        if cfg.sources then
            add_files(cfg.sources)
        end
    target_end()
end

-- ============================================================================
-- Pseudo Target: unittests
-- ============================================================================

--[[
    Create the "unittests" pseudo target that depends on all unit tests.

    This is equivalent to Jam's:
        NotFile unittests ;
]]

-- Note: In xmake, we can create a phony target to collect all unit test
-- dependencies. This should be called after all unit tests are defined.

--[[
    CreateUnitTestsTarget()

    Create the "unittests" phony target that depends on all registered
    unit test targets.

    Call this after all unit tests have been defined.
]]
function CreateUnitTestsTarget()
    target("unittests")
        set_kind("phony")

        for _, dep in ipairs(_unittest_deps) do
            add_deps(dep)
        end
    target_end()
end