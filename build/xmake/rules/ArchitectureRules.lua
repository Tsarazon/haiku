--[[
    ArchitectureRules.lua - Haiku architecture setup rules

    xmake equivalent of build/jam/ArchitectureRules

    Rules/Functions defined:
    - ArchitectureSetup       - Initialize userland architecture settings
    - KernelArchitectureSetup - Initialize kernel/boot architecture settings
    - ArchitectureSetupWarnings - Configure -Werror and stack protector
    - MultiArchIfPrimary      - Return value based on primary architecture
    - MultiArchConditionalGristFiles - Set grist based on architecture
    - MultiArchDefaultGristFiles - Convenient grist setting
    - MultiArchSubDirSetup    - Setup subdir for multiple architectures

    This file manages:
    - Compiler flags per architecture
    - Warning flags (C and C++)
    - Debug flags
    - Glue code paths (crti.o, crtn.o, crtbeginS.o, crtendS.o)
    - Library name mappings
    - Boot loader platform selection
    - Private system headers

    Supported architectures:
    - x86_64  - 64-bit x86 (primary target)
    - arm64   - 64-bit ARM (aarch64)
    - arm     - 32-bit ARM (armv7)
    - riscv64 - 64-bit RISC-V

    Usage:
        This file should be included via includes() in the root xmake.lua.
        Functions can also be imported via import() in script scope.
]]

-- ============================================================================
-- Architecture Configuration Storage
-- ============================================================================

-- Per-architecture settings storage
local _arch_settings = {}

-- Global architecture variables (equivalent to Jam's HAIKU_ARCH, HAIKU_ARCHS)
local _haiku_arch = nil  -- Primary architecture CPU
local _haiku_archs = {}  -- List of all architecture CPUs

-- Debug levels (from Haiku build system)
local DEBUG_LEVELS = {0, 1, 2}

-- Werror and stack protector directory configuration
local _werror_dirs = {}
local _stack_protector_dirs = {}
local _glibc_clang_dirs = {}  -- Clang-specific flags for glibc directories
local _werror_arch = nil  -- Current architecture for -Werror setup

-- ============================================================================
-- Helper Functions
-- ============================================================================

-- Split filepath into components (equivalent to Jam's FSplitPath)
local function FSplitPath(filepath)
    if type(filepath) == "table" then
        return filepath
    end
    local components = {}
    for component in string.gmatch(filepath, "[^/\\]+") do
        table.insert(components, component)
    end
    return components
end

-- Get target architecture from config
-- Note: In description scope, get_config() may return nil before 'xmake config'
-- This function is safe to call in both description and script scope
local function get_target_arch()
    -- Try get_config first (works in script scope and after config)
    local arch = get_config("arch")
    if arch then
        return arch
    end
    -- Fallback for description scope before config
    return "x86_64"
end

-- Get CPU name for architecture (maps packaging arch to CPU)
local function get_cpu_for_arch(architecture)
    -- In Haiku, the packaging architecture maps to a CPU name
    -- For most cases they are the same
    return architecture
end

-- Get packaging architectures (primary + secondary)
local function get_packaging_archs()
    local primary = get_target_arch()
    local secondary = get_config("secondary_arch")

    if secondary and secondary ~= "" then
        return {primary, secondary}
    end
    return {primary}
end

-- Get primary packaging architecture
local function get_primary_arch()
    return (get_packaging_archs())[1]
end

-- Create FDefines equivalent - converts defines to -D flags
local function FDefines(defines)
    if type(defines) ~= "table" then
        defines = {defines}
    end

    local flags = {}
    for _, def in ipairs(defines) do
        table.insert(flags, "-D" .. def)
    end
    return flags
end

-- Set include properties based on compiler type
-- Equivalent to Jam's SetIncludePropertiesVariables
local function SetIncludePropertiesVariables(settings)
    if settings.is_legacy_gcc then
        settings.includes_separator = "-I-"
        settings.local_includes_option = "-I"
        settings.system_includes_option = "-I"
    else
        settings.includes_separator = ""
        settings.local_includes_option = "-iquote "
        settings.system_includes_option = "-I "
    end
end

-- Get build directory for architecture
local function get_arch_object_dir(architecture)
    local buildir = get_config("buildir") or "build"
    return path.join(buildir, "objects", "haiku", architecture)
end

-- Get project directory
local function get_haiku_top()
    return os.projectdir() or "."
end

-- ============================================================================
-- ArchitectureSetup - Initialize userland architecture settings
-- ============================================================================

--[[
    ArchitectureSetup(architecture)

    Initializes all global packaging architecture dependent variables.
    Also sets primary architecture if this is the first invocation.

    Equivalent to Jam:
        rule ArchitectureSetup { }

    Parameters:
        architecture - Target architecture (x86_64, arm64, arm, riscv64)

    Sets up:
        - CC/C++ base flags
        - Architecture-specific flags
        - Warning flags
        - Debug flags
        - Include properties
        - Library/executable glue code
        - Library name map
]]
function ArchitectureSetup(architecture)
    if not architecture then
        architecture = get_target_arch()
    end

    local cpu = get_cpu_for_arch(architecture)
    local haiku_top = get_haiku_top()

    -- Initialize architecture settings table
    local settings = {
        cpu = cpu,
        architecture = architecture,
        cc = get_config("cc_" .. architecture),
        cxx = get_config("cxx_" .. architecture),
        link = nil,
        strip = get_config("strip_" .. architecture),
        ccflags = {},
        cxxflags = {},
        asflags = {},
        linkflags = {},
        defines = {},
        warning_ccflags = {},
        warning_cxxflags = {},
        werror_flags = {},
        debug_ccflags = {},
        debug_cxxflags = {},
        private_system_headers = {},
        library_begin_glue = {},
        library_end_glue = {},
        executable_begin_glue = {},
        executable_end_glue = {},
        library_name_map = {},
        is_clang = get_config("cc_is_clang_" .. architecture) == "1",
        is_legacy_gcc = get_config("cc_is_legacy_gcc_" .. architecture) == "1",
        use_gcc_pipe = get_config("use_gcc_pipe") == "1",
        use_graphite = get_config("use_gcc_graphite_" .. architecture) == "1",
        gcc_lib_dir = get_config("gcc_lib_dir_" .. architecture) or "",
        -- Include properties (set below)
        includes_separator = "",
        local_includes_option = "-iquote ",
        system_includes_option = "-I ",
    }

    -- Set default compiler paths if not configured
    settings.cc = settings.cc or (architecture .. "-unknown-haiku-gcc")
    settings.cxx = settings.cxx or (architecture .. "-unknown-haiku-g++")
    settings.strip = settings.strip or (architecture .. "-unknown-haiku-strip")
    settings.link = settings.cc

    -- Set include properties based on compiler
    SetIncludePropertiesVariables(settings)

    -- ========================================
    -- Base compiler flags
    -- ========================================

    local cc_base_flags = {}

    -- Enable GCC -pipe option if requested
    if settings.use_gcc_pipe then
        table.insert(cc_base_flags, "-pipe")
    end

    -- Disable strict aliasing
    table.insert(cc_base_flags, "-fno-strict-aliasing")

    -- Prevent deletion of null-pointer checks
    table.insert(cc_base_flags, "-fno-delete-null-pointer-checks")

    -- Architecture-specific builtins
    if architecture == "x86" then
        table.insert(cc_base_flags, "-fno-builtin-fork")
        table.insert(cc_base_flags, "-fno-builtin-vfork")
    end

    -- ========================================
    -- Architecture tuning flags
    -- ========================================

    local arch_flags = {}

    if cpu == "arm" then
        table.insert(arch_flags, "-march=armv7-a")
        table.insert(arch_flags, "-mfloat-abi=hard")
    elseif cpu == "arm64" then
        table.insert(arch_flags, "-march=armv8-a+fp16")
    elseif cpu == "x86" then
        table.insert(arch_flags, "-march=pentium")
    elseif cpu == "riscv64" then
        table.insert(arch_flags, "-march=rv64gc")
    end

    -- Clang-specific: disable relaxation on RISC-V (lld doesn't support R_RISCV_ALIGN)
    if settings.is_clang and cpu == "riscv64" then
        table.insert(cc_base_flags, "-mno-relax")
    end

    -- Append arch flags to base flags
    for _, flag in ipairs(arch_flags) do
        table.insert(cc_base_flags, flag)
    end

    -- Graphite optimizations
    if settings.use_graphite then
        table.insert(cc_base_flags, "-floop-nest-optimize")
        table.insert(cc_base_flags, "-fgraphite-identity")
    end

    -- ========================================
    -- Compile flags
    -- ========================================

    for _, flag in ipairs(cc_base_flags) do
        table.insert(settings.ccflags, flag)
        table.insert(settings.cxxflags, flag)
        table.insert(settings.linkflags, flag)
    end

    -- Add -nostdinc for target compilation
    table.insert(settings.ccflags, "-nostdinc")
    table.insert(settings.cxxflags, "-nostdinc")

    -- Assembler flags get architecture flags only
    for _, flag in ipairs(arch_flags) do
        table.insert(settings.asflags, flag)
    end
    table.insert(settings.asflags, "-nostdinc")

    -- Set global HAIKU_ARCH (only if not set yet - primary architecture)
    if not _haiku_arch then
        _haiku_arch = cpu
    end

    -- Add to HAIKU_ARCHS if not already present
    local found = false
    for _, arch in ipairs(_haiku_archs) do
        if arch == cpu then
            found = true
            break
        end
    end
    if not found then
        table.insert(_haiku_archs, cpu)
    end

    -- ========================================
    -- Defines
    -- ========================================

    table.insert(settings.defines, "ARCH_" .. cpu)

    -- ========================================
    -- Set include properties based on compiler
    -- ========================================

    SetIncludePropertiesVariables(settings)

    -- ========================================
    -- Warning flags
    -- ========================================

    settings.warning_ccflags = {
        "-Wall",
        "-Wno-multichar",
        "-Wpointer-arith",
        "-Wsign-compare",
        "-Wmissing-prototypes",
    }

    settings.warning_cxxflags = {
        "-Wall",
        "-Wno-multichar",
        "-Wpointer-arith",
        "-Wsign-compare",
        "-Wno-ctor-dtor-privacy",
        "-Woverloaded-virtual",
    }

    -- Clang-specific warning suppressions
    if settings.is_clang then
        local clang_suppress = {
            "-Wno-unused-private-field",
            "-Wno-gnu-designator",
            "-Wno-builtin-requires-header",
        }
        for _, w in ipairs(clang_suppress) do
            table.insert(settings.warning_ccflags, w)
        end

        -- Additional C++ suppressions for Clang
        table.insert(settings.warning_cxxflags, "-Wno-unused-private-field")
        table.insert(settings.warning_cxxflags, "-Wno-gnu-designator")
        table.insert(settings.warning_cxxflags, "-Wno-builtin-requires-header")
        table.insert(settings.warning_cxxflags, "-Wno-non-c-typedef-for-linkage")
        table.insert(settings.warning_cxxflags, "-Wno-non-power-of-two-alignment")
    end

    -- ========================================
    -- Werror exceptions (false positives)
    -- ========================================

    settings.werror_flags = {
        "-Wno-error=unused-but-set-variable",
        "-Wno-error=cpp",
        "-Wno-error=register",
        "-Wno-error=address-of-packed-member",
        "-Wno-error=stringop-overread",
        "-Wno-error=array-bounds",
        "-Wno-error=cast-align",
        "-Wno-error=format-truncation",
    }

    -- ========================================
    -- Debug flags
    -- ========================================

    local debug_flags = "-ggdb"
    local ndebug = get_config("ndebug") or 1

    -- Debug level 0: suppress asserts
    settings.debug_ccflags[0] = FDefines({"NDEBUG=" .. ndebug})
    settings.debug_cxxflags[0] = FDefines({"NDEBUG=" .. ndebug})

    -- Debug levels 1+
    for _, level in ipairs({1, 2}) do
        local flags = {debug_flags}
        for _, d in ipairs(FDefines({"DEBUG=" .. level})) do
            table.insert(flags, d)
        end
        settings.debug_ccflags[level] = flags
        settings.debug_cxxflags[level] = flags
    end

    -- ========================================
    -- Linker flags
    -- ========================================

    table.insert(settings.linkflags, "-Xlinker")
    table.insert(settings.linkflags, "--no-undefined")

    -- ========================================
    -- Private system headers
    -- ========================================

    settings.private_system_headers = {
        path.join(haiku_top, "headers", "private", "system"),
        path.join(haiku_top, "headers", "private", "system", "arch", cpu),
    }

    -- ========================================
    -- Glue code paths
    -- ========================================

    local arch_object_dir = get_arch_object_dir(architecture)
    local glue_dir = path.join(arch_object_dir, "system", "glue")
    local arch_glue_dir = path.join(glue_dir, "arch", cpu)

    -- Library glue code
    settings.library_begin_glue = {
        path.join(arch_glue_dir, "crti.o"),
        path.join(settings.gcc_lib_dir, "crtbeginS.o"),
        path.join(glue_dir, "init_term_dyn.o"),
    }

    settings.library_end_glue = {
        path.join(settings.gcc_lib_dir, "crtendS.o"),
        path.join(arch_glue_dir, "crtn.o"),
    }

    -- Executable glue code
    settings.executable_begin_glue = {
        path.join(arch_glue_dir, "crti.o"),
        path.join(settings.gcc_lib_dir, "crtbeginS.o"),
        path.join(glue_dir, "start_dyn.o"),
        path.join(glue_dir, "init_term_dyn.o"),
    }

    settings.executable_end_glue = settings.library_end_glue

    -- ========================================
    -- Library name map
    -- ========================================

    local packaging_archs = get_packaging_archs()
    local is_primary = (architecture == packaging_archs[1])

    local standard_libs = {
        "be", "bnetapi", "debug", "device", "game", "locale", "mail",
        "media", "midi", "midi2", "network", "package", "root",
        "screensaver", "textencoding", "tracker", "translation", "z"
    }

    for _, lib in ipairs(standard_libs) do
        settings.library_name_map[lib] = "lib" .. lib .. ".so"
    end

    -- Special libraries (static)
    settings.library_name_map["localestub"] = "liblocalestub.a"
    settings.library_name_map["shared"] = "libshared.a"

    -- input_server
    if is_primary then
        settings.library_name_map["input_server"] = "input_server"
    else
        settings.library_name_map["input_server"] = "input_server"
    end

    -- ========================================
    -- Object directories
    -- ========================================

    local arch_object_dir = get_arch_object_dir(architecture)
    settings.arch_object_dir = arch_object_dir
    settings.common_debug_object_dir = path.join(arch_object_dir, "common")
    settings.debug_0_object_dir = path.join(arch_object_dir, "release")

    settings.debug_object_dirs = {}
    settings.debug_object_dirs[0] = settings.debug_0_object_dir
    for _, level in ipairs({1, 2}) do
        settings.debug_object_dirs[level] = path.join(arch_object_dir, "debug_" .. level)
    end

    -- Store settings
    _arch_settings[architecture] = settings

    return settings
end

-- ============================================================================
-- KernelArchitectureSetup - Initialize kernel and boot loader settings
-- ============================================================================

--[[
    KernelArchitectureSetup(architecture)

    Initializes global kernel and boot loader related variables.
    These don't have a packaging architecture suffix since they are
    only set for the primary packaging architecture.

    Equivalent to Jam:
        rule KernelArchitectureSetup { }

    Parameters:
        architecture - Primary packaging architecture

    Sets up:
        - HAIKU_KERNEL_ARCH
        - HAIKU_KERNEL_PLATFORM
        - HAIKU_BOOT_TARGETS
        - Kernel C/C++ flags
        - Boot loader flags for each platform (EFI, BIOS, etc.)
]]
function KernelArchitectureSetup(architecture)
    if not architecture then
        architecture = get_primary_arch()
    end

    local cpu = get_cpu_for_arch(architecture)
    local arch_settings = _arch_settings[architecture]

    if not arch_settings then
        arch_settings = ArchitectureSetup(architecture)
    end

    local kernel_settings = {
        kernel_arch = cpu,
        kernel_arch_dir = cpu,
        kernel_platform = nil,
        boot_targets = {},
        boot_sdimage_begin = nil,
        boot_floppy_image_size = nil,
        boot_archive_image_offset = nil,
        boot_loader_base = nil,
        kernel_ccflags = {},
        kernel_cxxflags = {},
        kernel_pic_ccflags = {},
        kernel_pic_linkflags = {},
        kernel_addon_linkflags = {},
        boot_ccflags = {},
        boot_cxxflags = {},
        boot_optim = "-Os",
        boot_linkflags = {},
        boot_ldflags = {"-Bstatic"},
        kernel_defines = {"_KERNEL_MODE"},
        boot_defines = {"_BOOT_MODE"},
        kernel_addon_begin_glue = {},
        kernel_addon_end_glue = {},
        boot_platform_flags = {},
        private_kernel_headers = {},
    }

    -- ========================================
    -- Platform-specific kernel configuration
    -- ========================================

    if cpu == "arm" then
        kernel_settings.kernel_platform = "efi"
        kernel_settings.boot_targets = {"efi"}
        kernel_settings.boot_sdimage_begin = 20475
        kernel_settings.boot_archive_image_offset = 192
        kernel_settings.boot_loader_base = 0x1000000

    elseif cpu == "arm64" then
        kernel_settings.kernel_platform = "efi"
        kernel_settings.boot_targets = {"efi"}
        kernel_settings.boot_sdimage_begin = 2
        kernel_settings.boot_archive_image_offset = 192
        kernel_settings.boot_loader_base = 0x1000000

    elseif cpu == "x86" then
        kernel_settings.kernel_platform = "bios_ia32"
        kernel_settings.boot_targets = {"bios_ia32", "efi"}
        kernel_settings.boot_archive_image_offset = 384

    elseif cpu == "riscv64" then
        kernel_settings.kernel_platform = "efi"
        kernel_settings.boot_targets = {"efi", "riscv"}
        kernel_settings.boot_sdimage_begin = 2
        kernel_settings.boot_archive_image_offset = 192
        kernel_settings.boot_loader_base = 0x1000000

    elseif cpu == "x86_64" then
        kernel_settings.kernel_platform = "efi"
        kernel_settings.boot_targets = {"efi"}
        kernel_settings.boot_floppy_image_size = 2880
        kernel_settings.boot_archive_image_offset = 384
        kernel_settings.kernel_arch_dir = "x86"

    else
        print("Warning: Unsupported target CPU: " .. cpu)
    end

    -- ========================================
    -- Private kernel headers
    -- ========================================

    local haiku_top = get_haiku_top()
    local arch_object_dir = get_arch_object_dir(architecture)

    kernel_settings.private_kernel_headers = {
        -- PrivateHeaders $(DOT) kernel libroot shared kernel/boot/platform/$(HAIKU_KERNEL_PLATFORM)
        path.join(haiku_top, "headers", "private"),
        path.join(haiku_top, "headers", "private", "kernel"),
        path.join(haiku_top, "headers", "private", "libroot"),
        path.join(haiku_top, "headers", "private", "shared"),
        path.join(haiku_top, "headers", "private", "kernel", "boot", "platform",
            kernel_settings.kernel_platform),
        -- ArchHeaders $(HAIKU_KERNEL_ARCH_DIR)
        path.join(haiku_top, "headers", "private", "kernel", "arch",
            kernel_settings.kernel_arch_dir),
        -- FDirName $(HAIKU_COMMON_DEBUG_OBJECT_DIR_$(architecture)) system kernel
        path.join(arch_settings.common_debug_object_dir, "system", "kernel"),
    }

    -- Add private system headers
    for _, hdr in ipairs(arch_settings.private_system_headers) do
        table.insert(kernel_settings.private_kernel_headers, hdr)
    end

    -- ========================================
    -- Kernel C/C++ flags
    -- ========================================

    local cc_base_flags = {
        "-ffreestanding",
        "-finline",
        "-fno-semantic-interposition",
        "-fno-tree-vectorize",
    }

    local cxx_base_flags = {}
    for _, f in ipairs(cc_base_flags) do
        table.insert(cxx_base_flags, f)
    end
    table.insert(cxx_base_flags, "-fno-exceptions")

    if not arch_settings.is_clang then
        table.insert(cxx_base_flags, "-fno-use-cxa-atexit")
    end

    -- Merge with architecture CCFLAGS
    for _, f in ipairs(arch_settings.ccflags) do
        table.insert(kernel_settings.kernel_ccflags, f)
    end
    for _, f in ipairs(cc_base_flags) do
        table.insert(kernel_settings.kernel_ccflags, f)
    end

    for _, f in ipairs(arch_settings.cxxflags) do
        table.insert(kernel_settings.kernel_cxxflags, f)
    end
    for _, f in ipairs(cxx_base_flags) do
        table.insert(kernel_settings.kernel_cxxflags, f)
    end

    -- Clang-specific kernel link flags
    if arch_settings.is_clang then
        kernel_settings.kernel_pic_linkflags = {
            "-z", "noseparate-code",
            "-z", "norelro",
            "--no-rosegment",
        }
        kernel_settings.kernel_addon_linkflags = {
            "-z", "noseparate-code",
            "-z", "norelro",
            "-Wl,--no-rosegment",
        }
    end

    -- ========================================
    -- Architecture-specific kernel flags
    -- ========================================

    if cpu == "arm" then
        table.insert(kernel_settings.kernel_pic_linkflags, "-z")
        table.insert(kernel_settings.kernel_pic_linkflags, "max-page-size=0x1000")
        table.insert(kernel_settings.kernel_addon_linkflags, "-z")
        table.insert(kernel_settings.kernel_addon_linkflags, "max-page-size=0x1000")
        table.insert(kernel_settings.kernel_pic_ccflags, "-fpic")
        table.insert(kernel_settings.kernel_pic_linkflags, "-shared")

    elseif cpu == "arm64" then
        table.insert(kernel_settings.kernel_pic_linkflags, "-z")
        table.insert(kernel_settings.kernel_pic_linkflags, "max-page-size=0x1000")
        table.insert(kernel_settings.kernel_addon_linkflags, "-z")
        table.insert(kernel_settings.kernel_addon_linkflags, "max-page-size=0x1000")
        table.insert(kernel_settings.kernel_pic_ccflags, "-fpic")
        table.insert(kernel_settings.kernel_pic_linkflags, "-shared")

    elseif cpu == "riscv64" then
        table.insert(kernel_settings.kernel_ccflags, "-mcmodel=medany")
        table.insert(kernel_settings.kernel_ccflags, "-fpic")
        table.insert(kernel_settings.kernel_cxxflags, "-mcmodel=medany")
        table.insert(kernel_settings.kernel_cxxflags, "-fpic")
        table.insert(kernel_settings.kernel_pic_linkflags, "-shared")

    elseif cpu == "x86" then
        table.insert(kernel_settings.kernel_pic_ccflags, "-fno-pic")
        table.insert(kernel_settings.kernel_ccflags, "-march=pentium")
        table.insert(kernel_settings.kernel_cxxflags, "-march=pentium")

    elseif cpu == "x86_64" then
        table.insert(kernel_settings.kernel_pic_ccflags, "-fno-pic")
        table.insert(kernel_settings.kernel_pic_ccflags, "-mcmodel=kernel")

        table.insert(kernel_settings.kernel_ccflags, "-mno-red-zone")
        table.insert(kernel_settings.kernel_ccflags, "-fno-omit-frame-pointer")
        table.insert(kernel_settings.kernel_cxxflags, "-mno-red-zone")
        table.insert(kernel_settings.kernel_cxxflags, "-fno-omit-frame-pointer")

        table.insert(kernel_settings.kernel_pic_linkflags, "-z")
        table.insert(kernel_settings.kernel_pic_linkflags, "max-page-size=0x1000")
        table.insert(kernel_settings.kernel_addon_linkflags, "-z")
        table.insert(kernel_settings.kernel_addon_linkflags, "max-page-size=0x1000")

        -- Check for 32-bit compatibility mode
        local secondary_archs = get_packaging_archs()
        for i = 2, #secondary_archs do
            if secondary_archs[i] == "x86" then
                print("Enable kernel ia32 compatibility")
                table.insert(kernel_settings.kernel_defines, "_COMPAT_MODE")
                kernel_settings.kernel_compat_mode = true
                break
            end
        end
    end

    -- ========================================
    -- Boot loader flags
    -- ========================================

    for _, f in ipairs(arch_settings.ccflags) do
        table.insert(kernel_settings.boot_ccflags, f)
    end
    for _, f in ipairs(cc_base_flags) do
        table.insert(kernel_settings.boot_ccflags, f)
    end

    for _, f in ipairs(arch_settings.cxxflags) do
        table.insert(kernel_settings.boot_cxxflags, f)
    end
    for _, f in ipairs(cxx_base_flags) do
        table.insert(kernel_settings.boot_cxxflags, f)
    end

    -- Boot archive offset define
    if kernel_settings.boot_archive_image_offset then
        table.insert(kernel_settings.boot_ccflags,
            "-DBOOT_ARCHIVE_IMAGE_OFFSET=" .. kernel_settings.boot_archive_image_offset)
        table.insert(kernel_settings.boot_cxxflags,
            "-DBOOT_ARCHIVE_IMAGE_OFFSET=" .. kernel_settings.boot_archive_image_offset)
    end

    -- Boot loader base address
    if kernel_settings.boot_loader_base then
        table.insert(kernel_settings.boot_ldflags, "--defsym")
        table.insert(kernel_settings.boot_ldflags,
            "BOOT_LOADER_BASE=" .. string.format("0x%x", kernel_settings.boot_loader_base))
    end

    -- ========================================
    -- Per-boot-target flags
    -- ========================================

    for _, boot_target in ipairs(kernel_settings.boot_targets) do
        local boot_flags = {
            ccflags = {},
            cxxflags = {},
            ldflags = {},
        }

        if boot_target == "efi" then
            boot_flags.ccflags = {
                "-fpic", "-fno-stack-protector", "-fPIC", "-fshort-wchar",
                "-Wno-error=unused-variable", "-Wno-error=main",
            }
            boot_flags.cxxflags = {
                "-fpic", "-fno-stack-protector", "-fPIC", "-fshort-wchar",
                "-Wno-error=unused-variable", "-Wno-error=main",
            }

            if cpu == "x86" then
                if not arch_settings.is_clang then
                    table.insert(boot_flags.ccflags, "-maccumulate-outgoing-args")
                    table.insert(boot_flags.cxxflags, "-maccumulate-outgoing-args")
                end
            elseif cpu == "x86_64" then
                table.insert(boot_flags.ccflags, "-mno-red-zone")
                table.insert(boot_flags.cxxflags, "-mno-red-zone")
                if not arch_settings.is_clang then
                    table.insert(boot_flags.ccflags, "-maccumulate-outgoing-args")
                    table.insert(boot_flags.cxxflags, "-maccumulate-outgoing-args")
                end
            elseif cpu == "arm" then
                table.insert(boot_flags.ccflags, "-mfloat-abi=soft")
                table.insert(boot_flags.cxxflags, "-mfloat-abi=soft")

                -- Remove any previous -mfloat-abi=hard setting from boot flags
                local function remove_float_abi_hard(flags)
                    local fixed = {}
                    for _, flag in ipairs(flags) do
                        if flag ~= "-mfloat-abi=hard" then
                            table.insert(fixed, flag)
                        end
                    end
                    return fixed
                end
                kernel_settings.boot_ccflags = remove_float_abi_hard(kernel_settings.boot_ccflags)
                kernel_settings.boot_cxxflags = remove_float_abi_hard(kernel_settings.boot_cxxflags)
            end

            boot_flags.ldflags = {
                "-Bstatic", "-Bsymbolic", "-nostdlib",
                "-znocombreloc", "-no-undefined",
            }

        elseif boot_target == "bios_ia32" then
            boot_flags.ccflags = {"-fno-pic", "-fno-stack-protector"}
            boot_flags.cxxflags = {"-fno-pic", "-fno-stack-protector"}
            boot_flags.ldflags = {"-Bstatic", "-nostdlib"}

        elseif boot_target == "pxe_ia32" then
            boot_flags.ccflags = {"-fno-pic", "-fno-stack-protector"}
            boot_flags.cxxflags = {"-fno-pic", "-fno-stack-protector"}
            boot_flags.ldflags = {"-Bstatic", "-nostdlib"}

        elseif boot_target == "riscv" then
            boot_flags.ccflags = {
                "-mcmodel=medany", "-fno-omit-frame-pointer",
                "-fno-plt", "-fno-pic", "-fno-semantic-interposition",
            }
            boot_flags.cxxflags = {
                "-mcmodel=medany", "-fno-omit-frame-pointer",
                "-fno-plt", "-fno-pic", "-fno-semantic-interposition",
            }

        else
            boot_flags.ccflags = {"-fno-pic", "-Wno-error=main"}
            boot_flags.cxxflags = {"-fno-pic", "-Wno-error=main"}
        end

        kernel_settings.boot_platform_flags[boot_target] = boot_flags
    end

    -- ========================================
    -- Kernel warning flags
    -- ========================================

    kernel_settings.kernel_warning_ccflags = arch_settings.warning_ccflags
    kernel_settings.kernel_warning_cxxflags = arch_settings.warning_cxxflags

    -- ========================================
    -- Kernel debug flags
    -- ========================================

    for level, flags in pairs(arch_settings.debug_ccflags) do
        kernel_settings["kernel_debug_" .. level .. "_ccflags"] = flags
    end
    for level, flags in pairs(arch_settings.debug_cxxflags) do
        kernel_settings["kernel_debug_" .. level .. "_cxxflags"] = flags
    end

    -- ========================================
    -- Kernel addon glue code
    -- ========================================

    local arch_object_dir = get_arch_object_dir(architecture)
    local glue_dir = path.join(arch_object_dir, "system", "glue")

    kernel_settings.kernel_addon_begin_glue = {
        path.join(arch_settings.gcc_lib_dir, "crtbeginS.o"),
        path.join(glue_dir, "haiku_version_glue.o"),
    }

    kernel_settings.kernel_addon_end_glue = {
        path.join(arch_settings.gcc_lib_dir, "crtendS.o"),
    }

    -- Store kernel settings in arch settings
    _arch_settings[architecture].kernel = kernel_settings

    return kernel_settings
end

-- ============================================================================
-- ArchitectureSetupWarnings - Configure -Werror and stack protector
-- ============================================================================

--[[
    EnableWerror(dir_tokens)

    Enables -Werror for the specified directory.

    Parameters:
        dir_tokens - Directory path tokens (table) or path string
]]
function EnableWerror(dir_tokens)
    if type(dir_tokens) ~= "table" then
        dir_tokens = FSplitPath(dir_tokens)
    end

    local dir_path = table.concat(dir_tokens, "/")
    _werror_dirs[dir_path] = true
end

--[[
    EnableStackProtector(dir_tokens)

    Enables stack protector for the specified directory.

    Parameters:
        dir_tokens - Directory path tokens (table) or path string
]]
function EnableStackProtector(dir_tokens)
    if type(dir_tokens) ~= "table" then
        dir_tokens = FSplitPath(dir_tokens)
    end

    local dir_path = table.concat(dir_tokens, "/")
    _stack_protector_dirs[dir_path] = true
end

--[[
    IsWerrorEnabled(dir_path)

    Checks if -Werror is enabled for a directory.

    Parameters:
        dir_path - Directory path to check

    Returns:
        true if -Werror is enabled
]]
function IsWerrorEnabled(dir_path)
    if _werror_dirs[dir_path] then
        return true
    end

    for werror_dir, _ in pairs(_werror_dirs) do
        local pattern = "^" .. werror_dir:gsub("([%.%-%+%*%?%^%$%(%)%[%]%%])", "%%%1")
        if dir_path:find(pattern) then
            return true
        end
    end

    return false
end

--[[
    IsStackProtectorEnabled(dir_path)

    Checks if stack protector is enabled for a directory.

    Parameters:
        dir_path - Directory path to check

    Returns:
        true if stack protector is enabled
]]
function IsStackProtectorEnabled(dir_path)
    if _stack_protector_dirs[dir_path] then
        return true
    end

    for sp_dir, _ in pairs(_stack_protector_dirs) do
        local pattern = "^" .. sp_dir:gsub("([%.%-%+%*%?%^%$%(%)%[%]%%])", "%%%1")
        if dir_path:find(pattern) then
            return true
        end
    end

    return false
end

--[[
    ArchitectureSetupWarnings(architecture)

    Sets up compiler warnings and error flags for various subdirectories.

    Parameters:
        architecture - Packaging architecture
]]
function ArchitectureSetupWarnings(architecture)
    if not architecture then
        architecture = get_target_arch()
    end

    local arch_settings = _arch_settings[architecture]
    if not arch_settings then
        arch_settings = ArchitectureSetup(architecture)
    end

    local cpu = arch_settings.cpu

    -- Clang glibc flags
    -- AppendToConfigVar CCFLAGS : HAIKU_TOP src system libroot posix glibc :
    --     -fgnu89-inline -fheinous-gnu-extensions : global ;
    if arch_settings.is_clang then
        local glibc_path = "src/system/libroot/posix/glibc"
        _glibc_clang_dirs[glibc_path] = {
            ccflags = {"-fgnu89-inline", "-fheinous-gnu-extensions"},
        }
    end

    -- ARM architecture skips -Werror setup (uses #warning for placeholders)
    if cpu == "arm" then
        return
    end

    -- Set HAIKU_WERROR_ARCH for this architecture
    _werror_arch = architecture

    -- Enable -Werror for various parts of the source tree
    -- Note: Only enabled for GCC, Clang generates more warnings
    if not arch_settings.is_clang then
        EnableWerror({"src", "add-ons", "accelerants"})
        EnableWerror({"src", "add-ons", "decorators"})
        EnableWerror({"src", "add-ons", "disk_systems"})
        EnableWerror({"src", "add-ons", "input_server"})
        EnableWerror({"src", "add-ons", "kernel", "bus_managers"})
        EnableWerror({"src", "add-ons", "kernel", "busses"})
        EnableWerror({"src", "add-ons", "kernel", "console"})
        EnableWerror({"src", "add-ons", "kernel", "cpu"})
        EnableWerror({"src", "add-ons", "kernel", "debugger"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "audio"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "bluetooth"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "bus"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "common"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "disk"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "graphics", "ati"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "graphics", "framebuffer"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "graphics", "intel_extreme"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "graphics", "radeon_hd"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "graphics", "skeleton"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "graphics", "vesa"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "graphics", "virtio"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "input"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "joystick"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "midi"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "misc"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "ether", "etherpci"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "ether", "pegasus"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "ether", "rdc"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "ether", "sis19x"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "ether", "usb_asix"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "ether", "usb_davicom"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "ether", "usb_ecm"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "ether", "usb_rndis"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "ether", "virtio"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "ether", "wb840"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "network", "wwan"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "ports"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "power"})
        EnableWerror({"src", "add-ons", "kernel", "drivers", "video"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "bfs"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "cdda"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "ext2"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "fat"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "googlefs"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "iso9660"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "layers"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "nfs"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "nfs4"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "packagefs"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "ramfs"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "udf"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "userlandfs"})
        EnableWerror({"src", "add-ons", "kernel", "file_systems", "xfs"})
        EnableWerror({"src", "add-ons", "kernel", "generic"})
        EnableWerror({"src", "add-ons", "kernel", "network"})
        EnableWerror({"src", "add-ons", "kernel", "partitioning_systems"})
        EnableWerror({"src", "add-ons", "kernel", "power"})
        EnableWerror({"src", "add-ons", "locale"})
        EnableWerror({"src", "add-ons", "mail_daemon"})
        EnableWerror({"src", "add-ons", "media", "media-add-ons"})
        EnableWerror({"src", "add-ons", "media", "plugins", "ffmpeg"})
        EnableWerror({"src", "add-ons", "media", "plugins", "raw_decoder"})
        EnableWerror({"src", "add-ons", "print"})
        EnableWerror({"src", "add-ons", "screen_savers"})
        EnableWerror({"src", "add-ons", "tracker"})
        EnableWerror({"src", "add-ons", "translators", "bmp"})
        EnableWerror({"src", "add-ons", "translators", "gif"})
        EnableWerror({"src", "add-ons", "translators", "hvif"})
        EnableWerror({"src", "add-ons", "translators", "ico"})
        EnableWerror({"src", "add-ons", "translators", "jpeg"})
        EnableWerror({"src", "add-ons", "translators", "pcx"})
        EnableWerror({"src", "add-ons", "translators", "png"})
        EnableWerror({"src", "add-ons", "translators", "ppm"})
        EnableWerror({"src", "add-ons", "translators", "raw"})
        EnableWerror({"src", "add-ons", "translators", "rtf"})
        EnableWerror({"src", "add-ons", "translators", "sgi"})
        EnableWerror({"src", "add-ons", "translators", "shared"})
        EnableWerror({"src", "add-ons", "translators", "stxt"})
        EnableWerror({"src", "add-ons", "translators", "tga"})
        EnableWerror({"src", "add-ons", "translators", "tiff"})
        EnableWerror({"src", "add-ons", "translators", "wonderbrush"})
        EnableWerror({"src", "bin", "debug", "strace"})
        EnableWerror({"src", "bin", "desklink"})
        EnableWerror({"src", "bin", "listusb"})
        EnableWerror({"src", "bin", "multiuser"})
        EnableWerror({"src", "bin", "package"})
        EnableWerror({"src", "bin", "package_repo"})
        EnableWerror({"src", "bin", "pkgman"})
        EnableWerror({"src", "bin", "writembr"})
        EnableWerror({"src", "libs", "bsd"})
        EnableWerror({"src", "apps"})
        EnableWerror({"src", "kits"})
        EnableWerror({"src", "preferences"})
        EnableWerror({"src", "servers"})
        EnableWerror({"src", "system", "boot"})
        EnableWerror({"src", "system", "kernel"})
        EnableWerror({"src", "system", "libroot", "add-ons"})
        EnableWerror({"src", "system", "libroot", "os"})
        EnableWerror({"src", "system", "libroot", "posix", "locale"})
        EnableWerror({"src", "system", "libroot", "posix", "wchar"})
        EnableWerror({"src", "system", "runtime_loader"})
    end

    -- Stack protector (enabled regardless of compiler)
    if get_config("use_stack_protector") then
        EnableStackProtector({"src", "add-ons", "input_server"})
        EnableStackProtector({"src", "add-ons", "media"})
        EnableStackProtector({"src", "add-ons", "print"})
        EnableStackProtector({"src", "add-ons", "screen_savers"})
        EnableStackProtector({"src", "add-ons", "translators"})
        EnableStackProtector({"src", "bin"})
        EnableStackProtector({"src", "apps"})
        EnableStackProtector({"src", "kits"})
        EnableStackProtector({"src", "preferences"})
        EnableStackProtector({"src", "servers"})
        EnableStackProtector({"src", "system", "kernel"})
    end
end

-- ============================================================================
-- MultiArch Functions
-- ============================================================================

--[[
    MultiArchIfPrimary(if_value, else_value, architecture)

    Returns one of two values depending on whether architecture
    is the primary packaging architecture.

    Parameters:
        if_value - Value to return if primary
        else_value - Value to return if secondary
        architecture - Architecture to check (default: current target)

    Returns:
        if_value if primary architecture, else_value otherwise
]]
function MultiArchIfPrimary(if_value, else_value, architecture)
    architecture = architecture or get_target_arch()

    local packaging_archs = get_packaging_archs()
    if architecture == packaging_archs[1] then
        return if_value
    end
    return else_value
end

--[[
    MultiArchConditionalGristFiles(files, primary_grist, secondary_grist, architecture)

    Returns files with identifier prefix based on architecture.
    Note: In xmake, grist is not used. This returns files with optional prefix.

    Parameters:
        files - List of file names
        primary_grist - Prefix for primary architecture (or nil)
        secondary_grist - Prefix for secondary architecture
        architecture - Architecture to check (default: current target)

    Returns:
        List of files (prefixes applied as directory components if needed)
]]
function MultiArchConditionalGristFiles(files, primary_grist, secondary_grist, architecture)
    if type(files) ~= "table" then
        files = {files}
    end
    architecture = architecture or get_target_arch()

    local prefix = MultiArchIfPrimary(primary_grist, secondary_grist, architecture)

    -- In xmake we don't use grist, just return files
    -- If prefix logic is needed, implement as directory prefix
    return files
end

--[[
    MultiArchDefaultGristFiles(files, grist_prefix, architecture)

    Convenient shorthand for MultiArchConditionalGristFiles.

    Parameters:
        files - List of file names
        grist_prefix - Prefix
        architecture - Architecture to check (default: current target)

    Returns:
        List of files
]]
function MultiArchDefaultGristFiles(files, grist_prefix, architecture)
    architecture = architecture or get_target_arch()

    local secondary_prefix
    if grist_prefix and grist_prefix ~= "" then
        secondary_prefix = grist_prefix .. "_" .. architecture
    else
        secondary_prefix = architecture
    end

    return MultiArchConditionalGristFiles(files, grist_prefix, secondary_prefix, architecture)
end

--[[
    MultiArchSubDirSetup(architectures)

    Prepares objects for setting up subdirectory variables for
    multiple packaging architectures.

    Parameters:
        architectures - List of architectures (default: all configured)

    Returns:
        List of architecture setup objects
]]
function MultiArchSubDirSetup(architectures)
    if not architectures then
        architectures = get_packaging_archs()
    end
    if type(architectures) ~= "table" then
        architectures = {architectures}
    end

    local packaging_archs = get_packaging_archs()
    local result = {}

    for _, architecture in ipairs(architectures) do
        local is_configured = false
        for _, pkg_arch in ipairs(packaging_archs) do
            if pkg_arch == architecture then
                is_configured = true
                break
            end
        end

        if is_configured then
            local arch_settings = _arch_settings[architecture]
            if not arch_settings then
                arch_settings = ArchitectureSetup(architecture)
            end

            local arch_object = {
                architecture = architecture,
                target_arch = arch_settings.cpu,
                source_grist = architecture,
                settings = arch_settings,
            }

            table.insert(result, arch_object)
        end
    end

    return result
end

-- ============================================================================
-- Exported Getter Functions
-- ============================================================================

--[[
    GetArchSettings(architecture)

    Returns the settings for an architecture.

    Parameters:
        architecture - Architecture name (default: current target)

    Returns:
        Architecture settings table
]]
function GetArchSettings(architecture)
    architecture = architecture or get_target_arch()

    if not _arch_settings[architecture] then
        ArchitectureSetup(architecture)
    end

    return _arch_settings[architecture]
end

--[[
    GetKernelSettings(architecture)

    Returns the kernel settings for an architecture.

    Parameters:
        architecture - Architecture name (default: primary)

    Returns:
        Kernel settings table
]]
function GetKernelSettings(architecture)
    architecture = architecture or get_primary_arch()

    local arch_settings = GetArchSettings(architecture)
    if not arch_settings.kernel then
        KernelArchitectureSetup(architecture)
    end

    return arch_settings.kernel
end

--[[
    GetLibraryName(lib_name, architecture)

    Returns the mapped library name for an architecture.

    Parameters:
        lib_name - Short library name (e.g., "be", "media")
        architecture - Architecture name (default: current target)

    Returns:
        Mapped library name
]]
function GetLibraryName(lib_name, architecture)
    local arch_settings = GetArchSettings(architecture)
    return arch_settings.library_name_map[lib_name] or ("lib" .. lib_name .. ".so")
end

--[[
    GetGlueCode(code_type, position, architecture)

    Returns glue code paths.

    Parameters:
        code_type - "library" or "executable"
        position - "begin" or "end"
        architecture - Architecture name (default: current target)

    Returns:
        List of glue code object paths
]]
function GetGlueCode(code_type, position, architecture)
    local arch_settings = GetArchSettings(architecture)

    local key = code_type .. "_" .. position .. "_glue"
    return arch_settings[key] or {}
end

--[[
    GetPrivateSystemHeaders(architecture)

    Returns private system header directories.

    Parameters:
        architecture - Architecture name (default: current target)

    Returns:
        List of header directories
]]
function GetPrivateSystemHeaders(architecture)
    local arch_settings = GetArchSettings(architecture)
    return arch_settings.private_system_headers or {}
end

--[[
    GetPrivateKernelHeaders(architecture)

    Returns private kernel header directories.

    Parameters:
        architecture - Architecture name (default: primary)

    Returns:
        List of header directories
]]
function GetPrivateKernelHeaders(architecture)
    local kernel_settings = GetKernelSettings(architecture)
    return kernel_settings.private_kernel_headers or {}
end

--[[
    GetWarningFlags(language, architecture)

    Returns warning flags for a language.

    Parameters:
        language - "c" or "c++"
        architecture - Architecture name (default: current target)

    Returns:
        List of warning flags
]]
function GetWarningFlags(language, architecture)
    local arch_settings = GetArchSettings(architecture)

    if language == "c++" or language == "cxx" then
        return arch_settings.warning_cxxflags or {}
    else
        return arch_settings.warning_ccflags or {}
    end
end

--[[
    GetWerrorFlags(architecture)

    Returns -Werror exception flags.

    Parameters:
        architecture - Architecture name (default: current target)

    Returns:
        List of -Wno-error flags
]]
function GetWerrorFlags(architecture)
    local arch_settings = GetArchSettings(architecture)
    return arch_settings.werror_flags or {}
end

--[[
    GetDebugFlags(language, level, architecture)

    Returns debug flags for a level.

    Parameters:
        language - "c" or "c++"
        level - Debug level (0, 1, 2)
        architecture - Architecture name (default: current target)

    Returns:
        List of debug flags
]]
function GetDebugFlags(language, level, architecture)
    local arch_settings = GetArchSettings(architecture)
    level = level or 0

    if language == "c++" or language == "cxx" then
        return arch_settings.debug_cxxflags[level] or {}
    else
        return arch_settings.debug_ccflags[level] or {}
    end
end

--[[
    GetBootPlatformFlags(platform, architecture)

    Returns boot loader flags for a platform.

    Parameters:
        platform - Boot platform ("efi", "bios_ia32", etc.)
        architecture - Architecture name (default: primary)

    Returns:
        Table with ccflags, cxxflags, ldflags
]]
function GetBootPlatformFlags(platform, architecture)
    local kernel_settings = GetKernelSettings(architecture)
    return kernel_settings.boot_platform_flags[platform] or {
        ccflags = {},
        cxxflags = {},
        ldflags = {},
    }
end

--[[
    GetBootTargets(architecture)

    Returns list of boot targets for an architecture.

    Parameters:
        architecture - Architecture name (default: primary)

    Returns:
        List of boot target names
]]
function GetBootTargets(architecture)
    local kernel_settings = GetKernelSettings(architecture)
    return kernel_settings.boot_targets or {}
end

--[[
    GetKernelPlatform(architecture)

    Returns the default kernel platform for an architecture.

    Parameters:
        architecture - Architecture name (default: primary)

    Returns:
        Kernel platform name (e.g., "efi", "bios_ia32")
]]
function GetKernelPlatform(architecture)
    local kernel_settings = GetKernelSettings(architecture)
    return kernel_settings.kernel_platform
end

--[[
    GetHaikuArch()

    Returns the primary architecture CPU name (equivalent to Jam's HAIKU_ARCH).

    Returns:
        Primary architecture CPU name (e.g., "x86_64", "arm64")
]]
function GetHaikuArch()
    return _haiku_arch
end

--[[
    GetHaikuArchs()

    Returns list of all architecture CPUs (equivalent to Jam's HAIKU_ARCHS).

    Returns:
        List of architecture CPU names
]]
function GetHaikuArchs()
    return _haiku_archs
end

--[[
    GetKernelCompatMode(architecture)

    Returns whether kernel compatibility mode is enabled (x86 on x86_64).

    Parameters:
        architecture - Architecture name (default: primary)

    Returns:
        true if compat mode is enabled
]]
function GetKernelCompatMode(architecture)
    local kernel_settings = GetKernelSettings(architecture)
    return kernel_settings.kernel_compat_mode or false
end

--[[
    GetGlibcClangFlags(dir_path)

    Returns Clang-specific flags for glibc directories.

    Parameters:
        dir_path - Directory path to check

    Returns:
        Table with ccflags, or nil if not applicable
]]
function GetGlibcClangFlags(dir_path)
    for glibc_dir, flags in pairs(_glibc_clang_dirs) do
        if dir_path:find(glibc_dir, 1, true) then
            return flags
        end
    end
    return nil
end

--[[
    GetWerrorArch()

    Returns the architecture for which -Werror was set up.

    Returns:
        Architecture name or nil
]]
function GetWerrorArch()
    return _werror_arch
end

-- ============================================================================
-- xmake Rule: ArchitectureAware
-- ============================================================================

--[[
    ArchitectureAware rule

    Applies architecture-specific flags to a target based on its location
    in the source tree.

    Usage in xmake.lua:
        target("myapp")
            add_rules("ArchitectureAware")
            add_files("*.cpp")
]]
rule("ArchitectureAware")
    on_load(function(target)
        -- Get architecture from config
        local architecture = get_config("arch") or "x86_64"

        -- Ensure architecture is set up
        if not _arch_settings[architecture] then
            ArchitectureSetup(architecture)
        end
        local arch_settings = _arch_settings[architecture]

        -- Apply base compiler flags
        for _, flag in ipairs(arch_settings.ccflags) do
            target:add("cflags", flag, {force = true})
        end
        for _, flag in ipairs(arch_settings.cxxflags) do
            target:add("cxxflags", flag, {force = true})
        end
        for _, flag in ipairs(arch_settings.linkflags) do
            target:add("ldflags", flag, {force = true})
        end

        -- Apply defines
        for _, def in ipairs(arch_settings.defines) do
            target:add("defines", def)
        end

        -- Apply warning flags
        for _, flag in ipairs(arch_settings.warning_ccflags) do
            target:add("cflags", flag, {force = true})
        end
        for _, flag in ipairs(arch_settings.warning_cxxflags) do
            target:add("cxxflags", flag, {force = true})
        end

        -- Get source directory relative to project
        local scriptdir = target:scriptdir()
        local projectdir = os.projectdir()
        local rel_dir = path.relative(scriptdir, projectdir)

        -- Check if -Werror is enabled for this directory
        if IsWerrorEnabled(rel_dir) then
            target:add("cflags", "-Werror", {force = true})
            target:add("cxxflags", "-Werror", {force = true})

            -- Add werror exception flags
            for _, flag in ipairs(arch_settings.werror_flags) do
                target:add("cflags", flag, {force = true})
                target:add("cxxflags", flag, {force = true})
            end
        end

        -- Check if stack protector is enabled for this directory
        if IsStackProtectorEnabled(rel_dir) then
            target:add("cflags", "-fstack-protector-strong", {force = true})
            target:add("cflags", "-fstack-clash-protection", {force = true})
            target:add("cxxflags", "-fstack-protector-strong", {force = true})
            target:add("cxxflags", "-fstack-clash-protection", {force = true})
        end

        -- Apply Clang-specific glibc flags if applicable
        local glibc_flags = GetGlibcClangFlags(rel_dir)
        if glibc_flags and glibc_flags.ccflags then
            for _, flag in ipairs(glibc_flags.ccflags) do
                target:add("cflags", flag, {force = true})
            end
        end

        -- Add private system headers
        for _, dir in ipairs(arch_settings.private_system_headers) do
            target:add("includedirs", dir)
        end
    end)

-- ============================================================================
-- xmake Rule: KernelModule
-- ============================================================================

--[[
    KernelModule rule

    Applies kernel-specific flags to a target.

    Usage in xmake.lua:
        target("kernel_module")
            add_rules("KernelModule")
            add_files("*.cpp")
]]
rule("KernelModule")
    on_load(function(target)
        local architecture = get_config("arch") or "x86_64"

        -- Ensure kernel settings are set up
        local kernel_settings = GetKernelSettings(architecture)
        local arch_settings = GetArchSettings(architecture)

        -- Apply kernel compiler flags
        for _, flag in ipairs(kernel_settings.kernel_ccflags) do
            target:add("cflags", flag, {force = true})
        end
        for _, flag in ipairs(kernel_settings.kernel_cxxflags) do
            target:add("cxxflags", flag, {force = true})
        end

        -- Apply kernel PIC flags
        for _, flag in ipairs(kernel_settings.kernel_pic_ccflags) do
            target:add("cflags", flag, {force = true})
            target:add("cxxflags", flag, {force = true})
        end

        -- Apply kernel defines
        for _, def in ipairs(kernel_settings.kernel_defines) do
            target:add("defines", def)
        end

        -- Apply kernel warning flags
        for _, flag in ipairs(kernel_settings.kernel_warning_ccflags) do
            target:add("cflags", flag, {force = true})
        end
        for _, flag in ipairs(kernel_settings.kernel_warning_cxxflags) do
            target:add("cxxflags", flag, {force = true})
        end

        -- Add private kernel headers
        for _, dir in ipairs(kernel_settings.private_kernel_headers) do
            target:add("includedirs", dir)
        end
    end)

-- ============================================================================
-- xmake Rule: BootLoader
-- ============================================================================

--[[
    BootLoader rule

    Applies boot loader-specific flags to a target.
    Set BOOT_PLATFORM on target to specify platform (efi, bios_ia32, etc.)

    Usage in xmake.lua:
        target("haiku_loader")
            add_rules("BootLoader")
            set_values("BOOT_PLATFORM", "efi")
            add_files("*.cpp")
]]
rule("BootLoader")
    on_load(function(target)
        local architecture = get_config("arch") or "x86_64"
        local kernel_settings = GetKernelSettings(architecture)

        -- Get boot platform from target values
        local boot_platform = target:values("BOOT_PLATFORM")
        if not boot_platform then
            boot_platform = kernel_settings.kernel_platform
        end

        -- Apply common boot flags
        for _, flag in ipairs(kernel_settings.boot_ccflags) do
            target:add("cflags", flag, {force = true})
        end
        for _, flag in ipairs(kernel_settings.boot_cxxflags) do
            target:add("cxxflags", flag, {force = true})
        end

        -- Apply boot defines
        for _, def in ipairs(kernel_settings.boot_defines) do
            target:add("defines", def)
        end

        -- Apply platform-specific flags
        local platform_flags = kernel_settings.boot_platform_flags[boot_platform]
        if platform_flags then
            for _, flag in ipairs(platform_flags.ccflags or {}) do
                target:add("cflags", flag, {force = true})
            end
            for _, flag in ipairs(platform_flags.cxxflags or {}) do
                target:add("cxxflags", flag, {force = true})
            end
            for _, flag in ipairs(platform_flags.ldflags or {}) do
                target:add("ldflags", flag, {force = true})
            end
        end

        -- Apply boot ldflags
        for _, flag in ipairs(kernel_settings.boot_ldflags) do
            target:add("ldflags", flag, {force = true})
        end

        -- Set optimization
        target:add("cflags", kernel_settings.boot_optim, {force = true})
        target:add("cxxflags", kernel_settings.boot_optim, {force = true})
    end)
