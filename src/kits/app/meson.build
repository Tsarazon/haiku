# Haiku App Kit - Meson Build Configuration
# Converts from Jamfile MergeObject to Meson equivalent
# This creates app_kit.o as component for libbe.so

# Sources from Jamfile MergeObject (exact match)
app_kit_sources = files([
    'AppDefs.cpp',
    'Application.cpp', 
    'AppMisc.cpp',
    'AppServerLink.cpp',
    'Cursor.cpp',
    'Clipboard.cpp',
    'DesktopLink.cpp',
    'DirectMessageTarget.cpp',
    'Handler.cpp',
    'InitTerminateLibBe.cpp',
    'Invoker.cpp',
    'LaunchRoster.cpp',
    'LinkReceiver.cpp',
    'LinkSender.cpp',
    'Looper.cpp',
    'LooperList.cpp',
    'Message.cpp',
    'MessageAdapter.cpp',
    'MessageFilter.cpp',
    'MessageQueue.cpp',
    'MessageRunner.cpp',
    'Messenger.cpp',
    'MessageUtils.cpp',
    'Notification.cpp',
    'PropertyInfo.cpp',
    'PortLink.cpp',
    'RegistrarDefs.cpp',
    'Roster.cpp',
    'RosterPrivate.cpp',
    'Server.cpp',
    'ServerLink.cpp',
    'ServerMemoryAllocator.cpp',
    'TokenSpace.cpp',
    'TypeConstants.cpp',
    # KeyStore implementation
    'Key.cpp',
    'KeyStore.cpp',
])

# Compile definitions based on Jamfile conditionals + CMakeLists.txt analysis
app_kit_defines = []

# Critical: Add ZSTD_ENABLED (missing from our build, present in CMakeLists.txt)
app_kit_defines += ['-DZSTD_ENABLED']

# RUN_WITHOUT_REGISTRAR conditional (from Jamfile)
run_without_registrar = get_variable('run_without_registrar', false)
if run_without_registrar
    app_kit_defines += ['-DRUN_WITHOUT_REGISTRAR']
endif

# RUN_WITHOUT_APP_SERVER conditional (from Jamfile)  
run_without_app_server = get_variable('run_without_app_server', 0)
if run_without_app_server != 0
    app_kit_defines += ['-DRUN_WITHOUT_APP_SERVER']
endif

# Build packages headers (matching CMakeLists.txt exactly)
# Package versions from CMakeLists.txt analysis
target_arch = get_option('target_arch')
build_packages_base = '../generated/build_packages'
zlib_package = 'zlib-1.3.1-3-' + target_arch
zstd_package = 'zstd-1.5.6-1-' + target_arch
icu_package = 'icu74-74.1-3-' + target_arch
gcc_package = 'gcc_syslibs_devel-13.3.0_2023_08_10-1-' + target_arch

# Critical missing headers from CMakeLists.txt
build_packages_includes = []
build_pkg_paths = [
    build_packages_base + '/' + zlib_package + '/develop/headers',
    build_packages_base + '/' + zstd_package + '/develop/headers', 
    build_packages_base + '/' + icu_package + '/develop/headers',
    # GCC C++ headers
    build_packages_base + '/' + gcc_package + '/develop/headers/c++',
    build_packages_base + '/' + gcc_package + '/develop/headers/c++/' + target_arch + '-unknown-haiku',
    build_packages_base + '/' + gcc_package + '/develop/headers/c++/backward',
    build_packages_base + '/' + gcc_package + '/develop/headers/c++/ext',
    # GCC built-in headers
    build_packages_base + '/' + gcc_package + '/develop/headers/gcc/include',
    build_packages_base + '/' + gcc_package + '/develop/headers/gcc/include-fixed',
]

foreach pkg_path : build_pkg_paths
    if fs.is_dir(pkg_path)
        build_packages_includes += include_directories(pkg_path)
    endif
endforeach

# Use CORRECT ORDER: build packages headers FIRST (highest precedence), then others
# This matches CMakeLists.txt lines 88-92 then 100-168
app_kit_includes = build_packages_includes + haiku_config.get('get_all_include_dirs')

# Create app_kit.o equivalent to Jamfile MergeObject using proper Meson approach
# Use static_library to compile, then extract and merge object files
app_kit_lib = static_library(
    'app_kit_temp',
    app_kit_sources,
    include_directories: app_kit_includes,
    cpp_args: [
        '-O2',                    # Optimization (matching Jam)
        '-fno-strict-aliasing'    # Standard Haiku flag
    ] + app_kit_defines,
    pic: true,                   # Use Meson's pic instead of manual -fPIC
    build_by_default: false,
    install: false
)

# Create merged object file app_kit.o (equivalent to Jamfile MergeObject)
# Output to generated_meson mirroring Jam structure: /generated_meson/objects/haiku/x86_64/release/kits/app/app_kit.o
app_kit_output_dir = haiku_config['kits_output_dir'] + '/app'
app_kit_merged = custom_target(
    'app_kit.o',
    input: app_kit_lib,
    output: 'app_kit.o',
    command: [
        'sh', '-c',
        'mkdir -p ' + app_kit_output_dir + ' && /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-ld -r -o ' + app_kit_output_dir + '/app_kit.o @INPUT@.p/*.o'
    ],
    build_by_default: true,
    install: false
)

# Export the merged object for libbe.so integration
app_kit_dep = declare_dependency(
    sources: app_kit_merged,
    include_directories: app_kit_includes,
    compile_args: app_kit_defines
)