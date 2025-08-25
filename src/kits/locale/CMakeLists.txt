# Haiku Locale Kit - CMake Build System
# This builds locale_kit.o with cross-compiler exactly matching Jam build
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

project(locale_kit LANGUAGES C CXX)

message(STATUS "=== Building Locale Kit with CMake Cross-Compiler ===")

# CMake modules path (simplified for testing)
# list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../build/cmake/modules)

# Set up architecture-aware directory structure (matching Jam exactly)
set(HAIKU_CMAKE_OUTPUT_DIR "${HAIKU_ROOT}/cmake_generated")
set(HAIKU_ARCH_OBJECTS_DIR "${HAIKU_CMAKE_OUTPUT_DIR}/objects/haiku/${HAIKU_ARCH}/release")
set(HAIKU_KITS_DIR "${HAIKU_ARCH_OBJECTS_DIR}/kits")
set(HAIKU_LOCALE_KIT_DIR "${HAIKU_KITS_DIR}/locale")

# Create directories
file(MAKE_DIRECTORY ${HAIKU_LOCALE_KIT_DIR})

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


# Locale Kit sources (exactly from locale/Jamfile)
set(LOCALE_KIT_SOURCES
    Catalog.cpp
    CatalogData.cpp
    Collator.cpp
    Country.cpp
    DateFormat.cpp
    DateTimeFormat.cpp
    DefaultCatalog.cpp
    DurationFormat.cpp
    EditableCatalog.cpp
    Format.cpp
    FormattingConventions.cpp
    HashMapCatalog.cpp
    InitLocaleKit.cpp
    Language.cpp
    Locale.cpp
    LocaleRoster.cpp
    LocaleRosterData.cpp
    MutableLocaleRoster.cpp
    NumberFormat.cpp
    RelativeDateTimeFormat.cpp
    StringFormat.cpp
    TextEncoding.cpp
    TimeFormat.cpp
    TimeUnitFormat.cpp
    TimeZone.cpp
    UnicodeChar.cpp
)

# Create locale_kit.o as object library (matches Jam MergeObject)
add_library(locale_kit_obj OBJECT ${LOCALE_KIT_SOURCES})

# Add compiler flags matching Jam build (with symbol visibility control)
target_compile_definitions(locale_kit_obj PRIVATE 
    ZSTD_ENABLED
    HAIKU_TARGET_PLATFORM_HAIKU
)
target_compile_options(locale_kit_obj PRIVATE 
    -O2
    -fno-strict-aliasing
)

# Headers are now available globally from build packages, no target-specific linking needed

# Set cross-compiler properties
set_target_properties(locale_kit_obj PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME locale_kit
)

# Create locale_kit.o exactly like Jam build (MergeObject)
add_custom_command(
    OUTPUT ${HAIKU_LOCALE_KIT_DIR}/locale_kit.o
    COMMAND ${CMAKE_LINKER} -r $<TARGET_OBJECTS:locale_kit_obj> -o ${HAIKU_LOCALE_KIT_DIR}/locale_kit.o
    DEPENDS locale_kit_obj
    COMMENT "Merging objects into locale_kit.o (matching Jam MergeObject)"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

add_custom_target(locale_kit ALL DEPENDS ${HAIKU_LOCALE_KIT_DIR}/locale_kit.o)

message(STATUS "Locale Kit CMake cross-compilation configured:")
message(STATUS "  Sources: ${LOCALE_KIT_SOURCES}")
message(STATUS "  Cross-compiler: ${HAIKU_TARGET}-gcc")
message(STATUS "  Output: locale_kit.o object file")
message(STATUS "  Dependencies: zlib, zstd, ICU")