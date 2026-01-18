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
]]

-- ============================================================================
-- Architecture Configuration Storage
-- ============================================================================

-- Per-architecture settings storage
local _arch_settings = {}

-- Debug levels (from Haiku build system)
local DEBUG_LEVELS = {0, 1, 2}

-- ============================================================================
-- Helper Functions
-- ============================================================================

-- Get target architecture
local function get_target_arch()
    return get_config("arch") or "x86_64"
end

-- Get CPU name for architecture (maps packaging arch to CPU)
local function get_cpu_for_arch(architecture)
    -- In Haiku, the packaging architecture maps to a CPU name
    -- For most cases they are the same, but not always
    return architecture
end

-- Get packaging architectures (primary + secondary)
local function get_packaging_archs()
    local primary = get_config("arch") or "x86_64"
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
        if def:find("=") then
            table.insert(flags, "-D" .. def)
        else
            table.insert(flags, "-D" .. def)
        end
    end
    return flags
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

    -- Initialize architecture settings table
    local settings = {
        cpu = cpu,
        architecture = architecture,
        cc = get_config("cc_" .. architecture) or (architecture .. "-unknown-haiku-gcc"),
        cxx = get_config("cxx_" .. architecture) or (architecture .. "-unknown-haiku-g++"),
        link = nil,  -- Set below
        strip = get_config("strip_" .. architecture) or (architecture .. "-unknown-haiku-strip"),
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
        use_gcc_pipe = get_config("use_gcc_pipe") == "1",
        use_graphite = get_config("use_gcc_graphite_" .. architecture) == "1",
        gcc_lib_dir = get_config("gcc_lib_dir_" .. architecture) or "",
    }

    settings.link = settings.cc

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

    -- ========================================
    -- Defines
    -- ========================================

    table.insert(settings.defines, "ARCH_" .. cpu)

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

        -- Additional C++ suppressions
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
        -- Suppress warnings from legacy code
        "-Wno-error=unused-but-set-variable",
        "-Wno-error=cpp",
        "-Wno-error=register",
        -- These generate too many false positives
        "-Wno-error=address-of-packed-member",
        "-Wno-error=stringop-overread",
        "-Wno-error=array-bounds",
        -- These can stay
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

    local haiku_top = get_config("haiku_top") or "$(projectdir)"
    settings.private_system_headers = {
        path.join(haiku_top, "headers", "private", "system"),
        path.join(haiku_top, "headers", "private", "system", "arch", cpu),
    }

    -- ========================================
    -- Glue code paths
    -- ========================================

    local glue_dir = path.join("$(buildir)", "objects", "haiku", architecture,
        "system", "glue")
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

    local library_grist = ""
    local packaging_archs = get_packaging_archs()
    if architecture ~= packaging_archs[1] then
        library_grist = architecture
    end

    local standard_libs = {
        "be", "bnetapi", "debug", "device", "game", "locale", "mail",
        "media", "midi", "midi2", "network", "package", "root",
        "screensaver", "textencoding", "tracker", "translation", "z"
    }

    for _, lib in ipairs(standard_libs) do
        local lib_name = "lib" .. lib .. ".so"
        if library_grist ~= "" then
            lib_name = "<" .. library_grist .. ">" .. lib_name
        end
        settings.library_name_map[lib] = lib_name
    end

    -- Special libraries
    settings.library_name_map["localestub"] = "<" .. architecture .. ">liblocalestub.a"
    settings.library_name_map["shared"] = "<" .. architecture .. ">libshared.a"

    -- input_server depends on primary vs secondary
    if architecture == packaging_archs[1] then
        settings.library_name_map["input_server"] = "<nogrist>input_server"
    else
        settings.library_name_map["input_server"] = "<" .. architecture .. ">input_server"
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
        boot_platform_flags = {},  -- Per-platform boot flags
    }

    -- ========================================
    -- Platform-specific kernel configuration
    -- ========================================

    if cpu == "arm" then
        kernel_settings.kernel_platform = "efi"
        kernel_settings.boot_targets = {"efi"}
        kernel_settings.boot_sdimage_begin = 20475  -- KiB
        kernel_settings.boot_archive_image_offset = 192  -- KiB
        kernel_settings.boot_loader_base = 0x1000000

    elseif cpu == "arm64" then
        kernel_settings.kernel_platform = "efi"
        kernel_settings.boot_targets = {"efi"}
        kernel_settings.boot_sdimage_begin = 2  -- KiB
        kernel_settings.boot_archive_image_offset = 192  -- KiB
        kernel_settings.boot_loader_base = 0x1000000

    elseif cpu == "x86" then
        kernel_settings.kernel_platform = "bios_ia32"
        kernel_settings.boot_targets = {"bios_ia32", "efi"}
        kernel_settings.boot_archive_image_offset = 384  -- KiB

        -- Verify nasm is available
        if not get_config("nasm") then
            print("Warning: HAIKU_NASM not set. x86 target may not build correctly.")
        end

    elseif cpu == "riscv64" then
        kernel_settings.kernel_platform = "efi"
        kernel_settings.boot_targets = {"efi", "riscv"}
        kernel_settings.boot_sdimage_begin = 2  -- KiB
        kernel_settings.boot_archive_image_offset = 192  -- KiB
        kernel_settings.boot_loader_base = 0x1000000

    elseif cpu == "x86_64" then
        kernel_settings.kernel_platform = "efi"
        kernel_settings.boot_targets = {"efi"}
        kernel_settings.boot_floppy_image_size = 2880  -- KiB
        kernel_settings.boot_archive_image_offset = 384  -- KiB
        kernel_settings.kernel_arch_dir = "x86"  -- x86_64 kernel source is under arch/x86

        -- Verify nasm is available
        if not get_config("nasm") then
            print("Warning: HAIKU_NASM not set. x86_64 target may not build correctly.")
        end

    else
        error("Currently unsupported target CPU: " .. cpu)
    end

    -- ========================================
    -- Kernel C/C++ flags
    -- ========================================

    local cc_base_flags = {
        "-ffreestanding",
        "-finline",
        "-fno-semantic-interposition",
        "-fno-tree-vectorize",  -- Disable autovectorization (causes VM issues)
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
        -- Kernel lives within any single 2 GiB address space
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
        -- Kernel lives in top 2GB, use kernel code model
        table.insert(kernel_settings.kernel_pic_ccflags, "-fno-pic")
        table.insert(kernel_settings.kernel_pic_ccflags, "-mcmodel=kernel")

        -- Disable red zone and always use frame pointer
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
                break
            end
        end
    end

    -- ========================================
    -- Boot loader flags
    -- ========================================

    -- Common boot flags (from arch settings)
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
        table.insert(kernel_settings.boot_ldflags,
            "--defsym")
        table.insert(kernel_settings.boot_ldflags,
            "BOOT_LOADER_BASE=" .. string.format("0x%x", kernel_settings.boot_loader_base))
    end

    -- ========================================
    -- Per-boot-target flags
    -- ========================================

    for _, boot_target in ipairs(kernel_settings.boot_targets) do
        local target_upper = boot_target:upper()
        local boot_flags = {
            ccflags = {},
            cxxflags = {},
            ldflags = {},
        }

        if boot_target == "efi" then
            -- EFI bootloader is PIC
            boot_flags.ccflags = {
                "-fpic", "-fno-stack-protector", "-fPIC", "-fshort-wchar",
                "-Wno-error=unused-variable", "-Wno-error=main",
            }
            boot_flags.cxxflags = {
                "-fpic", "-fno-stack-protector", "-fPIC", "-fshort-wchar",
                "-Wno-error=unused-variable", "-Wno-error=main",
            }

            -- CPU-specific EFI flags
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
            -- Other bootloaders are non-PIC
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

    local glue_dir = path.join("$(buildir)", "objects", "haiku", architecture,
        "system", "glue")

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

-- Storage for directory warning configuration
local _werror_dirs = {}
local _stack_protector_dirs = {}

--[[
    EnableWerror(dir_tokens)

    Enables -Werror for the specified directory.

    Equivalent to Jam:
        rule EnableWerror { }

    Parameters:
        dir_tokens - Directory path tokens (e.g., {"src", "apps"})
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

    Equivalent to Jam:
        rule EnableStackProtector { }

    Parameters:
        dir_tokens - Directory path tokens
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
    -- Check exact match
    if _werror_dirs[dir_path] then
        return true
    end

    -- Check parent directories
    for werror_dir, _ in pairs(_werror_dirs) do
        if dir_path:find("^" .. werror_dir:gsub("([%.%-%+%*%?%^%$%(%)%[%]%%])", "%%%1")) then
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
    -- Check exact match
    if _stack_protector_dirs[dir_path] then
        return true
    end

    -- Check parent directories
    for sp_dir, _ in pairs(_stack_protector_dirs) do
        if dir_path:find("^" .. sp_dir:gsub("([%.%-%+%*%?%^%$%(%)%[%]%%])", "%%%1")) then
            return true
        end
    end

    return false
end

--[[
    ArchitectureSetupWarnings(architecture)

    Sets up compiler warnings and error flags for various subdirectories.

    Equivalent to Jam:
        rule ArchitectureSetupWarnings { }

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

    -- Clang-specific glibc flags
    if arch_settings.is_clang then
        -- These would be added to src/system/libroot/posix/glibc
        -- In xmake we handle this via per-directory config
    end

    -- ARM architecture skips -Werror setup (uses #warning for placeholders)
    if cpu == "arm" then
        return
    end

    -- Enable -Werror for various parts of the source tree
    -- Note: Only enabled for GCC, Clang generates more warnings
    if not arch_settings.is_clang then
        -- Add-ons
        EnableWerror({"src", "add-ons", "accelerants"})
        EnableWerror({"src", "add-ons", "bluetooth"})
        EnableWerror({"src", "add-ons", "decorators"})
        EnableWerror({"src", "add-ons", "disk_systems"})
        EnableWerror({"src", "add-ons", "input_server"})
        EnableWerror({"src", "add-ons", "kernel", "bluetooth"})
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

        -- Binaries
        EnableWerror({"src", "bin", "debug", "strace"})
        EnableWerror({"src", "bin", "desklink"})
        EnableWerror({"src", "bin", "listusb"})
        EnableWerror({"src", "bin", "multiuser"})
        EnableWerror({"src", "bin", "package"})
        EnableWerror({"src", "bin", "package_repo"})
        EnableWerror({"src", "bin", "pkgman"})
        EnableWerror({"src", "bin", "writembr"})

        -- Libraries
        EnableWerror({"src", "libs", "bsd"})

        -- Major components
        EnableWerror({"src", "apps"})
        EnableWerror({"src", "kits"})
        EnableWerror({"src", "preferences"})
        EnableWerror({"src", "servers"})

        -- System
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

    Equivalent to Jam:
        rule MultiArchIfPrimary { }

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

    Returns files with their grist set based on architecture.

    Equivalent to Jam:
        rule MultiArchConditionalGristFiles { }

    Parameters:
        files - List of file names
        primary_grist - Grist to use for primary architecture
        secondary_grist - Grist to use for secondary architecture
        architecture - Architecture to check (default: current target)

    Returns:
        List of files with appropriate grist
]]
function MultiArchConditionalGristFiles(files, primary_grist, secondary_grist, architecture)
    if type(files) ~= "table" then files = {files} end
    architecture = architecture or get_target_arch()

    local grist = MultiArchIfPrimary(primary_grist, secondary_grist, architecture)

    local result = {}
    for _, file in ipairs(files) do
        if grist and grist ~= "" then
            table.insert(result, "<" .. grist .. ">" .. file)
        else
            table.insert(result, file)
        end
    end

    return result
end

--[[
    MultiArchDefaultGristFiles(files, grist_prefix, architecture)

    Convenient shorthand for MultiArchConditionalGristFiles for the common case
    where secondary architecture name is appended to grist.

    Equivalent to Jam:
        rule MultiArchDefaultGristFiles { }

    Parameters:
        files - List of file names
        grist_prefix - Grist prefix
        architecture - Architecture to check (default: current target)

    Returns:
        List of files with appropriate grist
]]
function MultiArchDefaultGristFiles(files, grist_prefix, architecture)
    architecture = architecture or get_target_arch()

    local secondary_grist
    if grist_prefix and grist_prefix ~= "" then
        secondary_grist = grist_prefix .. "!" .. architecture
    else
        secondary_grist = architecture
    end

    return MultiArchConditionalGristFiles(files, grist_prefix, secondary_grist, architecture)
end

--[[
    MultiArchSubDirSetup(architectures)

    Prepares objects for setting up subdirectory variables for
    multiple packaging architectures.

    Equivalent to Jam:
        rule MultiArchSubDirSetup { }

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
        -- Check if architecture is configured
        local is_configured = false
        for _, pkg_arch in ipairs(packaging_archs) do
            if pkg_arch == architecture then
                is_configured = true
                break
            end
        end

        if is_configured then
            -- Get or create architecture settings
            local arch_settings = _arch_settings[architecture]
            if not arch_settings then
                arch_settings = ArchitectureSetup(architecture)
            end

            local arch_object = {
                architecture = architecture,
                target_arch = arch_settings.cpu,
                source_grist = "!" .. architecture,
                settings = arch_settings,
            }

            table.insert(result, arch_object)
        end
    end

    return result
end

-- ============================================================================
-- Exported Helper Functions
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
        Mapped library name with appropriate grist
]]
function GetLibraryName(lib_name, architecture)
    local arch_settings = GetArchSettings(architecture)
    return arch_settings.library_name_map[lib_name] or ("lib" .. lib_name .. ".so")
end

--[[
    GetGlueCode(type, position, architecture)

    Returns glue code paths.

    Parameters:
        type - "library" or "executable"
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

-- ============================================================================
-- xmake Rule for Architecture-Aware Targets
-- ============================================================================

--[[
    ArchitectureAware rule

    Applies architecture-specific flags to a target based on its location
    in the source tree.

    Usage:
        target("myapp")
            add_rules("ArchitectureAware")
            add_files("*.cpp")
]]
rule("ArchitectureAware")
    on_load(function (target)
        local architecture = get_config("arch") or "x86_64"
        local arch_settings = GetArchSettings(architecture)

        -- Apply base flags
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

        -- Get source directory relative path
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

        -- Add private system headers
        for _, dir in ipairs(arch_settings.private_system_headers) do
            target:add("includedirs", dir)
        end
    end)

-- ============================================================================
-- Initialize default architecture on load
-- ============================================================================

-- NOTE: Auto-initialization is disabled because import() cannot be called
-- at top-level in files loaded via includes(). Architecture setup will be
-- called lazily when functions like GetArchSettings() are invoked.
--[[
local primary_arch = get_primary_arch()
if primary_arch then
    ArchitectureSetup(primary_arch)
    KernelArchitectureSetup(primary_arch)
    ArchitectureSetupWarnings(primary_arch)
end
]]
