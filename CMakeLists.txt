cmake_minimum_required(VERSION 3.16)

# Haiku OS CMake Migration Project
project(Haiku 
    VERSION 1.0.0 
    DESCRIPTION "Haiku Operating System - CMake Build"
    LANGUAGES C CXX ASM
)

# Set up paths
set(HAIKU_TOP "${CMAKE_SOURCE_DIR}")
set(HAIKU_BUILD_DIR "${CMAKE_SOURCE_DIR}/build")
set(CMAKE_MODULE_PATH "${HAIKU_BUILD_DIR}/cmake/modules")

# Determine target architecture
if(NOT HAIKU_ARCH)
    set(HAIKU_ARCH "x86_64" CACHE STRING "Target architecture")
endif()

# Set up buildtools integration
set(BUILDTOOLS_DIR "${CMAKE_SOURCE_DIR}/buildtools")
if(NOT EXISTS "${BUILDTOOLS_DIR}")
    message(FATAL_ERROR "Buildtools not found at ${BUILDTOOLS_DIR}")
endif()

# Check for built cross-tools
set(CROSS_TOOLS_DIR "${CMAKE_SOURCE_DIR}/generated/cross-tools-${HAIKU_ARCH}")
if(EXISTS "${CROSS_TOOLS_DIR}/bin/${HAIKU_ARCH}-unknown-haiku-gcc")
    message(STATUS "Found cross-compiler at: ${CROSS_TOOLS_DIR}")
    set(HAIKU_CROSS_TOOLS_AVAILABLE TRUE)
else()
    message(STATUS "Cross-tools not found at: ${CROSS_TOOLS_DIR}")
    set(HAIKU_CROSS_TOOLS_AVAILABLE FALSE)
endif()

# Set output directory - mirror Jam build structure in cmake_generated
set(HAIKU_CMAKE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/cmake_generated")

# Mirror Jam's objects/haiku/x86_64/release structure
set(HAIKU_OBJECTS_DIR "${HAIKU_CMAKE_OUTPUT_DIR}/objects")
set(HAIKU_ARCH_OBJECTS_DIR "${HAIKU_OBJECTS_DIR}/haiku/${HAIKU_ARCH}")
set(HAIKU_RELEASE_DIR "${HAIKU_ARCH_OBJECTS_DIR}/release")
/home/ruslan/haiku/MESON_BUILD_INTEGRATION_LOG.md
# Define output directories following Jam structure
set(HAIKU_TOOLS_DIR "${HAIKU_RELEASE_DIR}/tools")
set(HAIKU_LIBS_DIR "${HAIKU_RELEASE_DIR}/lib") 
set(HAIKU_APPS_DIR "${HAIKU_RELEASE_DIR}/apps")
set(HAIKU_ADDONS_DIR "${HAIKU_RELEASE_DIR}/add-ons")
set(HAIKU_SERVERS_DIR "${HAIKU_RELEASE_DIR}/servers")
set(HAIKU_BIN_DIR "${HAIKU_RELEASE_DIR}/bin")
set(HAIKU_PREFS_DIR "${HAIKU_RELEASE_DIR}/preferences")
set(HAIKU_KITS_DIR "${HAIKU_RELEASE_DIR}/kits")

# Create the complete directory structure
file(MAKE_DIRECTORY ${HAIKU_CMAKE_OUTPUT_DIR})
file(MAKE_DIRECTORY ${HAIKU_OBJECTS_DIR})
file(MAKE_DIRECTORY ${HAIKU_ARCH_OBJECTS_DIR}) 
file(MAKE_DIRECTORY ${HAIKU_RELEASE_DIR})
file(MAKE_DIRECTORY ${HAIKU_TOOLS_DIR})
file(MAKE_DIRECTORY ${HAIKU_LIBS_DIR})
file(MAKE_DIRECTORY ${HAIKU_APPS_DIR})
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR})
file(MAKE_DIRECTORY ${HAIKU_SERVERS_DIR})
file(MAKE_DIRECTORY ${HAIKU_BIN_DIR})
file(MAKE_DIRECTORY ${HAIKU_PREFS_DIR})
file(MAKE_DIRECTORY ${HAIKU_KITS_DIR})

# Create complete add-ons structure upfront (mirroring Jam)
# Kernel add-ons - complete list
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/bluetooth)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/bus_managers)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/busses)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/console)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/cpu)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/debugger)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/drivers)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/file_systems)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/generic)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/network)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/partitioning_systems)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/kernel/power)

# Non-kernel add-ons - complete list
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/accelerants)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/control_look)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/decorators)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/disk_systems)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/input_server)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/locale)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/mail_daemon)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/media)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/network_settings)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/print)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/screen_savers)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/tracker)
file(MAKE_DIRECTORY ${HAIKU_ADDONS_DIR}/translators)

message(STATUS "Haiku CMake Migration")
message(STATUS "Target architecture: ${HAIKU_ARCH}")
message(STATUS "Buildtools directory: ${BUILDTOOLS_DIR}")
message(STATUS "CMake output structure:")
message(STATUS "  Base: ${HAIKU_CMAKE_OUTPUT_DIR}")
message(STATUS "  Arch: ${HAIKU_ARCH_OBJECTS_DIR}")
message(STATUS "  Apps: ${HAIKU_APPS_DIR}")
message(STATUS "  Libs: ${HAIKU_LIBS_DIR}")

# Skip BuildtoolsIntegration setup if CMAKE_TOOLCHAIN_FILE is set
if(NOT CMAKE_TOOLCHAIN_FILE)
    # Include CMake modules
    include("${CMAKE_MODULE_PATH}/BuildtoolsIntegration.cmake")
    
    # Set up cross-compilation
    setup_cross_compilation(${HAIKU_ARCH})
endif()

# Verify buildtools after setup
if(CMAKE_C_COMPILER MATCHES "haiku")
    verify_buildtools()
endif()

# Add source directory
add_subdirectory(src)

# DUAL BUILD DEMO: Add host tools built with CMake alongside Jam
message(STATUS "")
message(STATUS "=== Phase 2.5: Dual-Build System Demo ===")
add_subdirectory(src/tools)

# KIT BUILD DEMO: Build Haiku kits with CMake
message(STATUS "")
message(STATUS "=== Kit Building Demo: libbe.so Dual-Build ===")
add_subdirectory(src/kits)