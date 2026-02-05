--[[
    LocaleRules.lua - Haiku localization rules

    xmake equivalent of build/jam/LocaleRules (1:1 migration)

    Rules defined:
    - ExtractCatalogEntries   - Extract catalog strings from C++ sources via collectcatkeys
    - LinkApplicationCatalog  - Compile .catkeys into .catalog binary via linkcatkeys
    - DoCatalogs              - Full localization pipeline: extract + find translations + compile
    - AddCatalogEntryAttribute - Add SYS:NAME catalog attribute to a target

    External tools used (from build/tools):
    - collectcatkeys  - Extracts translatable strings from preprocessed source
    - linkcatkeys     - Compiles .catkeys text file into binary .catalog
    - addattr         - Adds file attributes

    Usage:
        -- In a target that needs localization:
        add_rules("DoCatalogs")
        set_values("catalog.signature", "application/x-vnd.Haiku-MyApp")
        set_values("catalog.sources", {"src/App.cpp", "src/Window.cpp"})
]]

-- Track localized targets and catalogs
local _localized_targets = {}
local _locale_catalogs = {}
local _locale_output_catkeys = {}

-- GetLocalizedTargets: return list of localized target names
function GetLocalizedTargets()
    return _localized_targets
end

-- GetLocaleCatalogs: return list of compiled catalog files
function GetLocaleCatalogs()
    return _locale_catalogs
end

-- GetLocaleOutputCatkeys: return list of generated catkeys files
function GetLocaleOutputCatkeys()
    return _locale_output_catkeys
end

-- ExtractCatalogEntries: extract translatable strings from source files
-- Runs: CC -E -DB_COLLECTING_CATKEYS sources > pre; collectcatkeys -s signature -o output pre
function ExtractCatalogEntries(target, sources, signature, regexp, output_dir)
    import("core.project.config")
    import("core.project.depend")

    local cc = config.get("target_cc") or config.get("host_cc") or "cc"
    local collectcatkeys = config.get("build_collectcatkeys") or "collectcatkeys"
    local haiku_top = config.get("haiku_top") or os.projectdir()

    local catkeys_dir = output_dir or path.join(config.get("catalogs_object_dir") or
        path.join(haiku_top, "generated", "catalogs"))

    for _, source in ipairs(sources) do
        local basename = path.basename(source)
        local output = path.join(catkeys_dir, basename .. ".catkeys")
        local prefile = output .. ".pre"

        depend.on_changed(function()
            os.mkdir(path.directory(output))

            -- Get include/define flags from target
            local ccdefs = {}
            local defines = target:get("defines") or {}
            for _, d in ipairs(defines) do
                table.insert(ccdefs, "-D" .. d)
            end
            table.insert(ccdefs, "-DB_COLLECTING_CATKEYS")

            local hdrs = {}
            local includedirs = target:get("includedirs") or {}
            for _, dir in ipairs(includedirs) do
                table.insert(hdrs, "-I")
                table.insert(hdrs, dir)
            end

            -- Preprocess source
            local args = {"-E"}
            for _, d in ipairs(ccdefs) do table.insert(args, d) end
            for _, h in ipairs(hdrs) do table.insert(args, h) end
            table.insert(args, source)

            local stdout = os.iorunv(cc, args)
            if stdout then
                io.writefile(prefile, stdout)
            end

            -- Extract catkeys
            local catkeys_args = {}
            if regexp and regexp ~= "" then
                table.insert(catkeys_args, "-r")
                table.insert(catkeys_args, regexp)
            end
            table.insert(catkeys_args, "-s")
            table.insert(catkeys_args, signature)
            table.insert(catkeys_args, "-w")
            table.insert(catkeys_args, "-o")
            table.insert(catkeys_args, output)
            table.insert(catkeys_args, prefile)

            os.vrunv(collectcatkeys, catkeys_args)
            os.rm(prefile)
        end, {
            files = {source},
            dependfile = target:dependfile(output)
        })
    end
end

-- LinkApplicationCatalog: compile a .catkeys file into a binary .catalog file
function LinkApplicationCatalog(target, catkeys_file, signature, language, output_dir)
    import("core.project.config")
    import("core.project.depend")

    local linkcatkeys = config.get("build_linkcatkeys") or "linkcatkeys"
    local catalog_file = path.join(output_dir, path.basename(catkeys_file) .. ".catalog")

    depend.on_changed(function()
        os.mkdir(path.directory(catalog_file))
        os.vrunv(linkcatkeys, {
            catkeys_file,
            "-l", language,
            "-s", signature,
            "-o", catalog_file
        })
    end, {
        files = {catkeys_file},
        dependfile = target:dependfile(catalog_file)
    })

    return catalog_file
end

-- Dual-load guard
if rule then

-- DoCatalogs: full localization pipeline rule
-- Extracts catkeys from sources, finds translations, compiles all to .catalog
rule("DoCatalogs")

    after_build(function(target)
        import("core.project.config")
        import("core.project.depend")

        local signature = target:values("catalog.signature")
        if not signature then return end

        local sources = target:values("catalog.sources") or {}
        local source_language = target:values("catalog.source_language") or "en"
        local regexp = target:values("catalog.regexp")

        local haiku_top = config.get("haiku_top") or os.projectdir()
        local catalogs_object_dir = config.get("catalogs_object_dir") or
            path.join(haiku_top, "generated", "catalogs")

        -- Determine subdir for catalogs
        local subdir = target:values("catalog.subdir") or target:name()
        local output_dir = path.join(catalogs_object_dir, subdir)

        -- Step 1: Extract catkeys from sources
        ExtractCatalogEntries(target, sources, signature, regexp, output_dir)

        -- Step 2: Find translations in data/catalogs/<subdir>/
        local translations_dir = path.join(haiku_top, "data", "catalogs", subdir)
        local translations = {}
        if os.isdir(translations_dir) then
            translations = os.files(path.join(translations_dir, "*.catkeys")) or {}
        end

        -- Step 3: Compile all catkeys to .catalog files
        local catalog_files = {}

        -- Source language catkeys
        for _, source in ipairs(sources) do
            local catkeys_file = path.join(output_dir, path.basename(source) .. ".catkeys")
            if os.isfile(catkeys_file) then
                local catalog = LinkApplicationCatalog(
                    target, catkeys_file, signature, source_language, output_dir)
                table.insert(catalog_files, catalog)
                table.insert(_locale_output_catkeys, catkeys_file)
            end
        end

        -- Translation catkeys
        for _, catkeys_file in ipairs(translations) do
            local language = path.basename(catkeys_file)
            local catalog = LinkApplicationCatalog(
                target, catkeys_file, signature, language, output_dir)
            table.insert(catalog_files, catalog)
        end

        -- Store catalog files on target for later packaging
        target:data_set("haiku.catalog_files", catalog_files)
        target:data_set("haiku.catalog_signature", signature)

        -- Track globally
        table.insert(_localized_targets, target:name())
        for _, c in ipairs(catalog_files) do
            table.insert(_locale_catalogs, c)
        end
    end)

rule_end()


-- AddCatalogEntryAttribute: add a catalog entry attribute to a file
rule("AddCatalogEntryAttribute")

    after_link(function(target)
        import("core.project.config")
        import("core.project.depend")

        local entry = target:values("catalog.entry")
        if not entry then return end

        local addattr = config.get("build_addattr") or "addattr"
        local targetfile = target:targetfile()

        depend.on_changed(function()
            os.vrunv(addattr, {
                "-t", "string",
                "SYS:NAME",
                entry,
                targetfile
            })
        end, {
            files = {targetfile},
            dependfile = target:dependfile(targetfile .. ".catattr")
        })
    end)

rule_end()

end -- if rule
