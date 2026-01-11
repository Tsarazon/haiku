--[[
    LocaleRules.lua - Haiku localization/catalog rules

    xmake equivalent of build/jam/LocaleRules

    Rules defined:
    - ExtractCatalogEntries    - Extract catalog entries from source files
    - LinkApplicationCatalog   - Link catkeys into compiled catalog
    - DoCatalogs               - Main rule for catalog generation
    - AddCatalogEntryAttribute - Add SYS:NAME attribute to target

    This system handles Haiku's localization through:
    1. Extracting translatable strings from source files (collectcatkeys)
    2. Compiling catkeys files into binary catalogs (linkcatkeys)
    3. Managing translations in data/catalogs/
]]

-- import("core.project.config")

-- ============================================================================
-- Configuration
-- ============================================================================

-- Catalogs object directory
local function get_catalogs_object_dir()
    local buildir = config.get("buildir") or "$(buildir)"
    return path.join(buildir, "catalogs")
end

-- Storage for catalog metadata
local _catalog_signatures = {}  -- signature -> subdir mapping
local _localized_targets = {}   -- list of targets with catalogs
local _locale_catalogs = {}     -- list of generated catalog files
local _locale_catkeys = {}      -- list of generated catkeys files

-- ============================================================================
-- ExtractCatalogEntries
-- ============================================================================

--[[
    ExtractCatalogEntries(target, sources, signature, regexp)

    Extract catalog entries from source files using the C preprocessor
    and collectcatkeys tool.

    The process:
    1. Run C preprocessor with -DB_COLLECTING_CATKEYS
    2. Run collectcatkeys to extract translatable strings
    3. Output a .catkeys text file

    Equivalent to Jam:
        rule ExtractCatalogEntries { }
        actions ExtractCatalogEntries1 { }

    Parameters:
        target - Output .catkeys file path
        sources - List of source files to scan
        signature - Application MIME signature
        regexp - Optional regular expression for parsing (default: be_catalog->GetString)
]]
function ExtractCatalogEntries(target, sources, signature, regexp)
    if type(sources) ~= "table" then
        sources = {sources}
    end

    -- Get subdir for this signature
    local subdir = _catalog_signatures[signature] or ""
    local output_dir = path.join(get_catalogs_object_dir(), subdir)
    os.mkdir(output_dir)

    -- Get compiler and flags
    local cc = config.get("cc") or "gcc"
    local defines = {"-DB_COLLECTING_CATKEYS"}
    local includes = {}

    -- Add standard includes
    local haiku_top = os.projectdir()
    table.insert(includes, "-I" .. path.join(haiku_top, "headers"))
    table.insert(includes, "-I" .. path.join(haiku_top, "headers", "os"))
    table.insert(includes, "-I" .. path.join(haiku_top, "headers", "posix"))

    -- Build preprocessor command
    local preprocess_file = target .. ".pre"
    local sources_str = table.concat(sources, " ")
    local defines_str = table.concat(defines, " ")
    local includes_str = table.concat(includes, " ")

    -- Run preprocessor
    local preprocess_cmd = string.format(
        "%s -E %s %s %s > %s 2>/dev/null || true",
        cc, defines_str, includes_str, sources_str, preprocess_file
    )
    os.exec(preprocess_cmd)

    -- Run collectcatkeys
    local collectcatkeys = path.join(haiku_top, "build", "tools", "collectcatkeys")
    local regexp_opt = ""
    if regexp and regexp ~= "" then
        regexp_opt = "-r " .. regexp
    end

    local catkeys_cmd = string.format(
        "%s %s -s %s -w -o %s %s",
        collectcatkeys, regexp_opt, signature, target, preprocess_file
    )
    os.exec(catkeys_cmd)

    -- Cleanup
    os.rm(preprocess_file)

    return target
end

-- ============================================================================
-- LinkApplicationCatalog
-- ============================================================================

--[[
    LinkApplicationCatalog(target, sources, signature, language)

    Link catalog entries from catkeys file into compiled catalog.

    Equivalent to Jam:
        rule LinkApplicationCatalog { }
        actions LinkApplicationCatalog1 { }

    Parameters:
        target - Output .catalog file path
        sources - Input .catkeys file(s)
        signature - Application MIME signature
        language - Language code (e.g., "en", "de", "ru")
]]
function LinkApplicationCatalog(target, sources, signature, language)
    if type(sources) ~= "table" then
        sources = {sources}
    end

    -- Get subdir for this signature
    local subdir = _catalog_signatures[signature] or ""
    local output_dir = path.join(get_catalogs_object_dir(), subdir)
    os.mkdir(output_dir)

    -- Get linkcatkeys tool
    local haiku_top = os.projectdir()
    local linkcatkeys = path.join(haiku_top, "build", "tools", "linkcatkeys")

    -- Build command
    local sources_str = table.concat(sources, " ")
    local cmd = string.format(
        "%s %s -l %s -s %s -o %s",
        linkcatkeys, sources_str, language, signature, target
    )
    os.exec(cmd)

    return target
end

-- ============================================================================
-- DoCatalogs
-- ============================================================================

--[[
    DoCatalogs(target, signature, sources, source_language, regexp)

    Main rule for catalog generation. Extracts catkeys from sources,
    finds translations, and generates all catalogs.

    Equivalent to Jam:
        rule DoCatalogs { }

    Parameters:
        target - Target name (application)
        signature - Application MIME signature
        sources - List of source files
        source_language - Language of strings in source (default: "en")
        regexp - Optional regular expression for parsing

    Returns:
        Table with catalog_files and catkeys_file
]]
function DoCatalogs(target, signature, sources, source_language, regexp)
    source_language = source_language or "en"

    if type(sources) ~= "table" then
        sources = {sources}
    end

    -- Determine subdir for catalogs
    local scriptdir = os.scriptdir()
    local projectdir = os.projectdir()
    local rel_path = path.relative(scriptdir, projectdir)
    local subdir_tokens = rel_path:split("/")

    -- Remove first token (usually "src" or similar)
    local subdir = table.concat(subdir_tokens, "/", 2)

    -- Store signature -> subdir mapping
    _catalog_signatures[signature] = subdir

    -- Output directory
    local output_dir = path.join(get_catalogs_object_dir(), subdir)
    os.mkdir(output_dir)

    -- Generate catkeys file from sources
    local catkeys_file = path.join(output_dir, source_language .. ".catkeys")
    ExtractCatalogEntries(catkeys_file, sources, signature, regexp)

    -- Find translations
    local haiku_top = projectdir
    local translations_dir = path.join(haiku_top, "data", "catalogs", subdir)
    local translations = {}

    if os.isdir(translations_dir) then
        local files = os.files(path.join(translations_dir, "*.catkeys"))
        for _, f in ipairs(files) do
            table.insert(translations, f)
        end
    end

    -- Generate catalogs from all catkeys files
    local catalog_files = {}
    local all_catkeys = {catkeys_file}

    -- Add translations to catkeys list
    for _, trans in ipairs(translations) do
        table.insert(all_catkeys, trans)
    end

    -- Generate catalog for each language
    for _, catkeys in ipairs(all_catkeys) do
        local lang = path.basename(catkeys):gsub("%.catkeys$", "")
        local catalog_file = path.join(output_dir, lang .. ".catalog")

        LinkApplicationCatalog(catalog_file, catkeys, signature, lang)
        table.insert(catalog_files, catalog_file)
    end

    -- Track for pseudo-targets
    table.insert(_localized_targets, target)
    for _, cat in ipairs(catalog_files) do
        table.insert(_locale_catalogs, cat)
    end
    table.insert(_locale_catkeys, catkeys_file)

    return {
        catalog_files = catalog_files,
        catkeys_file = catkeys_file,
        signature = signature
    }
end

-- ============================================================================
-- AddCatalogEntryAttribute
-- ============================================================================

--[[
    AddCatalogEntryAttribute(target, attribute_value)

    Add SYS:NAME catalog entry attribute to a target file.

    Equivalent to Jam:
        rule AddCatalogEntryAttribute { }
        actions AddCatalogEntryAttribute1 { }

    Parameters:
        target - Target file path
        attribute_value - Attribute value in format "x-vnd.Haiku-App:context:string"
]]
function AddCatalogEntryAttribute(target, attribute_value)
    local haiku_top = os.projectdir()
    local addattr = path.join(haiku_top, "build", "tools", "addattr")

    local cmd = string.format(
        '%s -t string "SYS:NAME" "%s" "%s"',
        addattr, attribute_value, target
    )
    os.exec(cmd)
end

-- ============================================================================
-- Accessor Functions
-- ============================================================================

--[[
    GetLocalizedTargets()

    Returns list of all targets that have catalogs.
]]
function GetLocalizedTargets()
    return _localized_targets
end

--[[
    GetLocaleCatalogs()

    Returns list of all generated catalog files.
]]
function GetLocaleCatalogs()
    return _locale_catalogs
end

--[[
    GetLocaleCatkeys()

    Returns list of all generated catkeys files.
]]
function GetLocaleCatkeys()
    return _locale_catkeys
end

--[[
    ClearLocaleData()

    Clears all locale tracking data (for testing).
]]
function ClearLocaleData()
    _catalog_signatures = {}
    _localized_targets = {}
    _locale_catalogs = {}
    _locale_catkeys = {}
end

-- ============================================================================
-- xmake Rule for Localized Targets
-- ============================================================================

--[[
    Localized rule

    Automatically handles catalog generation for targets.

    Usage:
        target("MyApp")
            add_rules("Localized")
            set_values("catalog_signature", "x-vnd.Haiku-MyApp")
            set_values("catalog_sources", {"App.cpp", "Window.cpp"})
            -- Optional:
            set_values("catalog_language", "en")
            set_values("catalog_regexp", "")
]]
rule("Localized")
    on_load(function (target)
        -- Mark as localized
        target:set("localized", true)
    end)

    after_build(function (target)
        local signature = target:values("catalog_signature")
        local sources = target:values("catalog_sources")

        if not signature then
            return -- No localization configured
        end

        if not sources then
            -- Auto-detect C++ sources
            sources = {}
            for _, sourcefile in ipairs(target:sourcefiles()) do
                if sourcefile:match("%.cpp$") or sourcefile:match("%.cxx$") then
                    table.insert(sources, sourcefile)
                end
            end
        end

        if #sources == 0 then
            return
        end

        local language = target:values("catalog_language") or "en"
        local regexp = target:values("catalog_regexp")

        local result = DoCatalogs(target:name(), signature, sources, language, regexp)

        -- Store result for installation
        target:set("catalog_files", result.catalog_files)
        target:set("catalog_signature", signature)
    end)

-- ============================================================================
-- Convenience Functions
-- ============================================================================

--[[
    localized_app(name, config)

    Convenience function for defining a localized application.

    Usage:
        localized_app("StyledEdit", {
            signature = "x-vnd.Haiku-StyledEdit",
            sources = {"StyledEditApp.cpp", "StyledEditWindow.cpp"},
            language = "en"  -- optional
        })
]]
function localized_app(name, cfg)
    target(name)
        add_rules("Localized")
        set_kind("binary")

        if cfg.signature then
            set_values("catalog_signature", cfg.signature)
        end

        if cfg.sources then
            set_values("catalog_sources", cfg.sources)
        end

        if cfg.language then
            set_values("catalog_language", cfg.language)
        end

        if cfg.regexp then
            set_values("catalog_regexp", cfg.regexp)
        end
end

-- ============================================================================
-- Catalog Installation Helper
-- ============================================================================

--[[
    InstallCatalogs(target, dest_base)

    Install catalog files to destination.

    Parameters:
        target - Target object with catalog_files and catalog_signature
        dest_base - Base destination directory (e.g., "/boot/system/data/catalogs")
]]
function InstallCatalogs(target, dest_base)
    local catalog_files = target:values("catalog_files")
    local signature = target:values("catalog_signature")

    if not catalog_files or not signature then
        return
    end

    -- Create destination directory
    local dest_dir = path.join(dest_base, signature)
    os.mkdir(dest_dir)

    -- Copy each catalog
    for _, catalog in ipairs(catalog_files) do
        local lang = path.basename(catalog)
        local dest = path.join(dest_dir, lang)
        os.cp(catalog, dest)
    end
end