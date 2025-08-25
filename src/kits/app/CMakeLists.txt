# Haiku App Kit - CMake Build System
# This builds app_kit.o with cross-compiler exactly matching Jam build
#
# Usage:
#   cmake -B ../../../cmake_generated .   # Clean build directory in main Haiku folder
#   cmake -B ../../../cmake_generated -DHAIKU_ROOT=/path/to/haiku .
#   cmake -B ../../../cmake_generated -DHAIKU_ARCH=riscv64 .
#
# Configurable variables:
#   HAIKU_ROOT        - Path to Haiku source tree (auto-detected)
#   HAIKU_ARCH        - Target architecture (default: x86_64)  
#   HAIKU_TARGET      - Target triplet (default: ${HAIKU_ARCH}-unknown-haiku)
#   HAIKU_*_PACKAGE   - Package names for zlib, zstd, ICU, GCC

cmake_minimum_required(VERSION 3.16)

# Detect Haiku root directory automatically (can be overridden)
if(NOT DEFINED HAIKU_ROOT)
    get_filename_component(HAIKU_ROOT "${CMAKE_SOURCE_DIR}/../../.." ABSOLUTE)
endif()

# Configure target architecture (can be overridden)
if(NOT DEFINED HAIKU_ARCH)
    set(HAIKU_ARCH "x86_64")
endif()

# Configure target triplet (can be overridden)  
if(NOT DEFINED HAIKU_TARGET)
    set(HAIKU_TARGET "${HAIKU_ARCH}-unknown-haiku")
endif()

message(STATUS "Using Haiku root: ${HAIKU_ROOT}")
message(STATUS "Using Haiku architecture: ${HAIKU_ARCH}")
message(STATUS "Using Haiku target: ${HAIKU_TARGET}")

# Use cross-compiler BEFORE project() call
set(CMAKE_C_COMPILER ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/bin/${HAIKU_TARGET}-gcc CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/bin/${HAIKU_TARGET}-g++ CACHE FILEPATH "" FORCE)

# Set sysroot for cross-compilation
set(CMAKE_SYSROOT ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/sysroot)

# Configure cross-compilation paths
set(CMAKE_FIND_ROOT_PATH ${HAIKU_ROOT}/generated/cross-tools-${HAIKU_ARCH}/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Skip CMake's automatic compiler testing for cross-compilation
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

project(app_kit LANGUAGES C CXX)

message(STATUS "=== Building App Kit with CMake Cross-Compiler ===")

# CMake modules path and HaikuKitBuilderHelper integration
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../build/cmake/modules)
include(HaikuKitBuilderHelper)

# Initialize Haiku kit building environment (replaces manual setup)
haiku_kit_init()

# Now HAIKU_CMAKE_OUTPUT_DIR, HAIKU_KITS_DIR, HAIKU_TEMP_BUILD_DIR are all set
set(HAIKU_APP_KIT_DIR "${HAIKU_KITS_DIR}/app")
file(MAKE_DIRECTORY ${HAIKU_APP_KIT_DIR})

# Build package headers (exactly matching Jam build system approach)
# This matches the exact include paths used by Haiku's Jam build
set(HAIKU_BUILD_PACKAGES_BASE "${HAIKU_ROOT}/generated/build_packages")

# Package versions (can be overridden via -D flags)
# Example: cmake -DHAIKU_ROOT=/path/to/haiku -DHAIKU_ARCH=riscv64 ...
if(NOT DEFINED HAIKU_ZLIB_PACKAGE)
    set(HAIKU_ZLIB_PACKAGE "zlib-1.3.1-3-${HAIKU_ARCH}")
endif()
if(NOT DEFINED HAIKU_ZSTD_PACKAGE)
    set(HAIKU_ZSTD_PACKAGE "zstd-1.5.6-1-${HAIKU_ARCH}")
endif()
if(NOT DEFINED HAIKU_ICU_PACKAGE)
    set(HAIKU_ICU_PACKAGE "icu74-74.1-3-${HAIKU_ARCH}")
endif()
if(NOT DEFINED HAIKU_GCC_PACKAGE)
    set(HAIKU_GCC_PACKAGE "gcc_syslibs_devel-13.3.0_2023_08_10-1-${HAIKU_ARCH}")
endif()

# Add the same include directories that Jam uses (from debug output analysis)
include_directories(SYSTEM
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_ZLIB_PACKAGE}/develop/headers
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_ZSTD_PACKAGE}/develop/headers  
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_ICU_PACKAGE}/develop/headers
)

set(ICU_FOUND TRUE)
set(ZLIB_FOUND TRUE) 
set(ZSTD_FOUND TRUE)

# Include headers exactly like Jam build (complete mapping from haiku-headers-mapping.md)
# Order matters: build packages first, then private, then public
include_directories(SYSTEM
    # Private headers (highest precedence for compression algorithm headers)
    ${HAIKU_ROOT}/headers/private/app
    ${HAIKU_ROOT}/headers/private/interface
    ${HAIKU_ROOT}/headers/private/input
    ${HAIKU_ROOT}/headers/private/kernel
    ${HAIKU_ROOT}/headers/private/libroot
    ${HAIKU_ROOT}/headers/private/locale
    ${HAIKU_ROOT}/headers/private/media
    ${HAIKU_ROOT}/headers/private/print
    ${HAIKU_ROOT}/headers/private/shared
    ${HAIKU_ROOT}/headers/private/support    # ZlibCompressionAlgorithm.h here
    ${HAIKU_ROOT}/headers/private/storage
    ${HAIKU_ROOT}/headers/private/tracker
    ${HAIKU_ROOT}/headers/private
    ${HAIKU_ROOT}/headers/private/system
    ${HAIKU_ROOT}/headers/private/system/arch/${HAIKU_ARCH}
    
    # Build headers (include redirectors)
    ${HAIKU_ROOT}/headers/build/private/support
    
    # GCC C++ headers from build packages
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/c++
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/c++/${HAIKU_TARGET}
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/c++/backward
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/c++/ext
    
    # Compatibility headers
    ${HAIKU_ROOT}/headers/glibc
    ${HAIKU_ROOT}/headers/posix
    
    # GCC built-in headers  
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/gcc/include
    ${HAIKU_BUILD_PACKAGES_BASE}/${HAIKU_GCC_PACKAGE}/develop/headers/gcc/include-fixed
    
    # Public OS headers (last to avoid conflicts)
    ${HAIKU_ROOT}/headers
    ${HAIKU_ROOT}/headers/os
    ${HAIKU_ROOT}/headers/os/add-ons
    ${HAIKU_ROOT}/headers/os/add-ons/file_system
    ${HAIKU_ROOT}/headers/os/add-ons/graphics
    ${HAIKU_ROOT}/headers/os/add-ons/input_server
    ${HAIKU_ROOT}/headers/os/add-ons/registrar
    ${HAIKU_ROOT}/headers/os/add-ons/screen_saver
    ${HAIKU_ROOT}/headers/os/add-ons/tracker
    ${HAIKU_ROOT}/headers/os/app
    ${HAIKU_ROOT}/headers/os/device
    ${HAIKU_ROOT}/headers/os/drivers
    ${HAIKU_ROOT}/headers/os/game
    ${HAIKU_ROOT}/headers/os/interface
    ${HAIKU_ROOT}/headers/os/kernel
    ${HAIKU_ROOT}/headers/os/locale
    ${HAIKU_ROOT}/headers/os/media
    ${HAIKU_ROOT}/headers/os/mail
    ${HAIKU_ROOT}/headers/os/midi
    ${HAIKU_ROOT}/headers/os/midi2
    ${HAIKU_ROOT}/headers/os/net
    ${HAIKU_ROOT}/headers/os/storage
    ${HAIKU_ROOT}/headers/os/support
    ${HAIKU_ROOT}/headers/os/translation
    
    # Library headers
    ${HAIKU_ROOT}/headers/libs/agg
    ${HAIKU_ROOT}/headers/libs/icon
    
    # Build configuration
    ${HAIKU_ROOT}/build/config_headers
    ${HAIKU_ROOT}/src/kits/tracker
)

# Find optional dependencies using CMake (for cross-compilation these may not be found)
find_package(ZLIB QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ICU QUIET icu-uc icu-io)
endif()

# App Kit sources (exactly from app/Jamfile)
set(APP_KIT_SOURCES
    AppDefs.cpp
    AppMisc.cpp
    AppServerLink.cpp
    Application.cpp
    Clipboard.cpp
    Cursor.cpp
    DesktopLink.cpp
    DirectMessageTarget.cpp
    Handler.cpp
    InitTerminateLibBe.cpp
    Invoker.cpp
    Key.cpp
    KeyStore.cpp
    LaunchRoster.cpp
    LinkReceiver.cpp
    LinkSender.cpp
    Looper.cpp
    LooperList.cpp
    Message.cpp
    MessageAdapter.cpp
    MessageFilter.cpp
    MessageQueue.cpp
    MessageRunner.cpp
    MessageUtils.cpp
    Messenger.cpp
    Notification.cpp
    PortLink.cpp
    PropertyInfo.cpp
    RegistrarDefs.cpp
    Roster.cpp
    RosterPrivate.cpp
    Server.cpp
    ServerLink.cpp
    ServerMemoryAllocator.cpp
    TokenSpace.cpp
    TypeConstants.cpp
)


# Create app_kit.o as object library (matches Jam MergeObject)
add_library(app_kit_obj OBJECT ${APP_KIT_SOURCES})

# Add compiler flags exactly matching Jam build
target_compile_definitions(app_kit_obj PRIVATE ZSTD_ENABLED)
target_compile_options(app_kit_obj PRIVATE 
    -O2
    -fno-strict-aliasing
)

# Link dependencies if found
if(ZLIB_FOUND)
    target_link_libraries(app_kit_obj PRIVATE ZLIB::ZLIB)
endif()

if(ICU_FOUND)
    target_include_directories(app_kit_obj PRIVATE ${ICU_INCLUDE_DIRS})
    target_compile_options(app_kit_obj PRIVATE ${ICU_CFLAGS_OTHER})
endif()

# Set cross-compiler properties
set_target_properties(app_kit_obj PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME app_kit
)

# Create app_kit.o exactly like Jam build (MergeObject)
add_custom_command(
    OUTPUT ${HAIKU_APP_KIT_DIR}/app_kit.o
    COMMAND ${CMAKE_LINKER} -r $<TARGET_OBJECTS:app_kit_obj> -o ${HAIKU_APP_KIT_DIR}/app_kit.o
    DEPENDS app_kit_obj
    COMMENT "Merging objects into app_kit.o (matching Jam MergeObject)"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

add_custom_target(app_kit ALL DEPENDS ${HAIKU_APP_KIT_DIR}/app_kit.o)

message(STATUS "App Kit CMake cross-compilation configured:")
message(STATUS "  Sources: ${APP_KIT_SOURCES}")
message(STATUS "  Cross-compiler: ${HAIKU_TARGET}-gcc")
message(STATUS "  Output: app_kit.o object file")
message(STATUS "  Dependencies: zlib, zstd, ICU")

# HaikuKitBuilderHelper is now integrated! Additional helper functions available:
# haiku_kit_help()                              # Show usage help
# haiku_configure_kit(app SUCCESS ERROR)        # Configure with error checking  
# haiku_build_kit(app SUCCESS ERROR)            # Build with error checking
# haiku_quick_build_kit(app SUCCESS ERROR)      # Configure + build
# haiku_clean_temp()                            # Clean temp directory
#
# Standard workflow remains the same:
# cmake -B /path/to/temp_build .
# make -C /path/to/temp_build