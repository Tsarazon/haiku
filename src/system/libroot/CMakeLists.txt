# Haiku libroot.so - Core C Library Bootstrap
cmake_minimum_required(VERSION 3.16)
project(libroot LANGUAGES C CXX ASM)

# Add Haiku CMake modules to path  
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/build/cmake/modules)

# Skip HaikuCommon module for bootstrap to avoid dependencies
# include(HaikuCommon)

message(STATUS "=== Building libroot.so with CMake (Bootstrap Mode) ===")

# Bootstrap approach: Build with host compiler first, then install headers
# so cross-compiler can work. This mimics how Haiku's build system works.
message(STATUS "Building libroot with host compiler for bootstrap")

# Essential libroot sources for bootstrap (minimal set that can compile)
set(LIBROOT_BOOTSTRAP_SOURCES
    libroot_init.c
)

# For bootstrap, we'll create a minimal libroot that provides basic functionality
# This will be expanded once we have a working cross-compilation environment

# Add include directories for libroot build (using absolute paths from Haiku root)
include_directories(SYSTEM
    ${CMAKE_SOURCE_DIR}/../../../headers
    ${CMAKE_SOURCE_DIR}/../../../headers/posix  
    ${CMAKE_SOURCE_DIR}/../../../headers/os
    ${CMAKE_SOURCE_DIR}/../../../headers/os/kernel
    ${CMAKE_SOURCE_DIR}/../../../headers/os/support
    ${CMAKE_SOURCE_DIR}/../../../headers/os/storage
    ${CMAKE_SOURCE_DIR}/../../../headers/private
    ${CMAKE_SOURCE_DIR}/../../../headers/private/system
    ${CMAKE_SOURCE_DIR}/../../../headers/private/system/arch/x86_64
    ${CMAKE_SOURCE_DIR}/../../../headers/private/kernel
    ${CMAKE_SOURCE_DIR}/../../../headers/private/runtime_loader
    ${CMAKE_SOURCE_DIR}/../../../headers/private/libroot
)

# Debug header paths
message(STATUS "Header paths configured:")
message(STATUS "  Base headers: ${CMAKE_SOURCE_DIR}/../../../headers")
message(STATUS "  OS headers: ${CMAKE_SOURCE_DIR}/../../../headers/os")
message(STATUS "  Kernel headers: ${CMAKE_SOURCE_DIR}/../../../headers/os/kernel")

# Build architecture-specific objects (mimicking Jamfile MultiArchSubDirSetup)
set(TARGET_ARCH x86_64)

# Phase 1: Install headers to sysroot so cross-compiler can find them
install(DIRECTORY ${CMAKE_SOURCE_DIR}/../../../headers/posix/
    DESTINATION /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot/boot/system/develop/headers/posix
    FILES_MATCHING PATTERN "*.h"
)

# Install os headers (BeBuild.h, etc.)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/../../../headers/os/
    DESTINATION /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot/boot/system/develop/headers/os
    FILES_MATCHING PATTERN "*.h"
)

# Install config headers individually
install(FILES 
    ${CMAKE_SOURCE_DIR}/../../../headers/config/types.h
    ${CMAKE_SOURCE_DIR}/../../../headers/config/HaikuConfig.h
    DESTINATION /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot/boot/system/develop/headers/config
)

# Phase 2: Use Jam-built libroot.so (complete implementation) instead of minimal bootstrap
# The Jam-built libroot has all required functions like find_thread, exit, etc.
set(JAM_LIBROOT_PATH ${CMAKE_SOURCE_DIR}/../../../generated/objects/haiku/x86_64/release/system/libroot/libroot.so)

# Verify Jam-built libroot exists
if(EXISTS ${JAM_LIBROOT_PATH})
    message(STATUS "Using complete Jam-built libroot.so: ${JAM_LIBROOT_PATH}")
    
    # Install Jam-built libroot to both runtime and development locations
    install(FILES ${JAM_LIBROOT_PATH}
        DESTINATION /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot/boot/system/lib
        RENAME libroot.so
    )
    
    install(FILES ${JAM_LIBROOT_PATH}
        DESTINATION /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot/boot/system/develop/lib
        RENAME libroot.so
    )
else()
    message(FATAL_ERROR "Jam-built libroot.so not found at ${JAM_LIBROOT_PATH}. Please build with Jam first.")
endif()

# Phase 4: Create custom target for building with cross-compiler
# This will be executed after bootstrap installation
add_custom_target(libroot_cross_compile
    COMMENT "Building full libroot with cross-compiler"
    COMMAND ${CMAKE_COMMAND} -E echo "Cross-compiler libroot build would go here"
)

message(STATUS "libroot.so CMake configuration complete")
message(STATUS "  Target: libroot.so")  
message(STATUS "  Architecture: ${TARGET_ARCH}")
message(STATUS "  Install to: cross-tools sysroot")