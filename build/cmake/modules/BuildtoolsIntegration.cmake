# BuildtoolsIntegration.cmake
# Integration with Haiku's custom gcc, binutils, and build tools
# Handles cross-compilation setup and buildtools verification

cmake_minimum_required(VERSION 3.16)

# Global variables for buildtools
if(NOT DEFINED HAIKU_ARCH)
    set(HAIKU_ARCH "x86_64")
endif()

if(NOT DEFINED HAIKU_TARGET)
    set(HAIKU_TARGET "${HAIKU_ARCH}-unknown-haiku")
endif()

if(NOT DEFINED HAIKU_TOP)
    get_filename_component(HAIKU_TOP "${CMAKE_SOURCE_DIR}" ABSOLUTE)
endif()

# Buildtools paths
set(BUILDTOOLS_DIR "${HAIKU_TOP}/buildtools")
set(CROSS_TOOLS_PREFIX "${HAIKU_TOP}/generated/cross-tools-${HAIKU_ARCH}")
set(CROSS_TOOLS_BIN "${CROSS_TOOLS_PREFIX}/bin")
set(CROSS_TOOLS_SYSROOT "${CROSS_TOOLS_PREFIX}/${HAIKU_TARGET}")

# Function: setup_cross_compilation
# Sets up cross-compilation environment for specified architecture
# Usage: setup_cross_compilation(ARCH)
function(setup_cross_compilation ARCH)
    message(STATUS "Setting up cross-compilation for: ${ARCH}")
    
    # Update global variables with specified architecture
    set(HAIKU_ARCH ${ARCH} PARENT_SCOPE)
    set(HAIKU_TARGET "${ARCH}-unknown-haiku" PARENT_SCOPE)
    
    # Update paths for the specified architecture
    set(CROSS_TOOLS_PREFIX "${HAIKU_TOP}/generated/cross-tools-${ARCH}")
    set(CROSS_TOOLS_BIN "${CROSS_TOOLS_PREFIX}/bin")
    set(CROSS_TOOLS_SYSROOT "${CROSS_TOOLS_PREFIX}/${ARCH}-unknown-haiku")
    
    # Check if cross-tools exist
    set(TARGET_TRIPLET "${ARCH}-unknown-haiku")
    set(CROSS_GCC "${CROSS_TOOLS_BIN}/${TARGET_TRIPLET}-gcc")
    set(CROSS_GPP "${CROSS_TOOLS_BIN}/${TARGET_TRIPLET}-g++")
    
    if(NOT EXISTS ${CROSS_GCC})
        message(WARNING "Cross-compiler not found at: ${CROSS_GCC}")
        message(STATUS "You may need to build cross-tools first with: build_cross_tools.sh ${ARCH}")
        return()
    endif()
    
    # Set CMake variables for cross-compilation
    set(CMAKE_SYSTEM_NAME "Haiku" PARENT_SCOPE)
    set(CMAKE_SYSTEM_VERSION "1" PARENT_SCOPE)
    set(CMAKE_SYSTEM_PROCESSOR ${ARCH} PARENT_SCOPE)
    
    # Set compilers
    set(CMAKE_C_COMPILER ${CROSS_GCC} PARENT_SCOPE)
    set(CMAKE_CXX_COMPILER ${CROSS_GPP} PARENT_SCOPE)
    set(CMAKE_ASM_COMPILER ${CROSS_GCC} PARENT_SCOPE)
    
    # Set other tools
    set(CMAKE_AR "${CROSS_TOOLS_BIN}/${TARGET_TRIPLET}-ar" PARENT_SCOPE)
    set(CMAKE_RANLIB "${CROSS_TOOLS_BIN}/${TARGET_TRIPLET}-ranlib" PARENT_SCOPE)
    set(CMAKE_LINKER "${CROSS_TOOLS_BIN}/${TARGET_TRIPLET}-ld" PARENT_SCOPE)
    set(CMAKE_STRIP "${CROSS_TOOLS_BIN}/${TARGET_TRIPLET}-strip" PARENT_SCOPE)
    set(CMAKE_OBJCOPY "${CROSS_TOOLS_BIN}/${TARGET_TRIPLET}-objcopy" PARENT_SCOPE)
    set(CMAKE_OBJDUMP "${CROSS_TOOLS_BIN}/${TARGET_TRIPLET}-objdump" PARENT_SCOPE)
    
    # Set sysroot
    if(EXISTS ${CROSS_TOOLS_SYSROOT})
        set(CMAKE_SYSROOT ${CROSS_TOOLS_SYSROOT} PARENT_SCOPE)
        set(CMAKE_FIND_ROOT_PATH ${CROSS_TOOLS_SYSROOT} PARENT_SCOPE)
    else()
        message(WARNING "Sysroot not found at: ${CROSS_TOOLS_SYSROOT}")
    endif()
    
    # Configure search modes for cross-compilation
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER PARENT_SCOPE)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY PARENT_SCOPE)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY PARENT_SCOPE)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY PARENT_SCOPE)
    
    # Skip compiler testing (trust that our cross-compiler works)
    set(CMAKE_C_COMPILER_WORKS TRUE PARENT_SCOPE)
    set(CMAKE_CXX_COMPILER_WORKS TRUE PARENT_SCOPE)
    set(CMAKE_ASM_COMPILER_WORKS TRUE PARENT_SCOPE)
    
    # Set Haiku-specific compiler flags
    set(HAIKU_C_FLAGS "-fno-strict-aliasing -fno-delete-null-pointer-checks")
    set(HAIKU_CXX_FLAGS "-fno-strict-aliasing -fno-delete-null-pointer-checks -std=c++17")
    
    # Architecture-specific flags
    if(ARCH STREQUAL "x86_64")
        set(HAIKU_C_FLAGS "${HAIKU_C_FLAGS} -m64 -march=x86-64")
        set(HAIKU_CXX_FLAGS "${HAIKU_CXX_FLAGS} -m64 -march=x86-64")
    elseif(ARCH STREQUAL "x86")
        set(HAIKU_C_FLAGS "${HAIKU_C_FLAGS} -m32 -march=i586") 
    elseif(ARCH STREQUAL "arm64")
        set(HAIKU_C_FLAGS "${HAIKU_C_FLAGS} -march=armv8-a")
        set(HAIKU_CXX_FLAGS "${HAIKU_CXX_FLAGS} -march=armv8-a")
    elseif(ARCH STREQUAL "riscv64")
        set(HAIKU_C_FLAGS "${HAIKU_C_FLAGS} -march=rv64imafdc")
        set(HAIKU_CXX_FLAGS "${HAIKU_CXX_FLAGS} -march=rv64imafdc")
    endif()
    
    # Set flags in parent scope
    set(CMAKE_C_FLAGS_INIT ${HAIKU_C_FLAGS} PARENT_SCOPE)
    set(CMAKE_CXX_FLAGS_INIT ${HAIKU_CXX_FLAGS} PARENT_SCOPE)
    
    # Set runtime library paths for Haiku
    set(CMAKE_INSTALL_RPATH "/boot/system/lib" PARENT_SCOPE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE PARENT_SCOPE)
    
    message(STATUS "Cross-compilation configured for: ${ARCH}")
    message(STATUS "  Compiler: ${CROSS_GCC}")
    message(STATUS "  Sysroot: ${CMAKE_SYSROOT}")
endfunction()

# Function: verify_buildtools
# Verifies that buildtools have Haiku patches and work correctly
# Usage: verify_buildtools()
function(verify_buildtools)
    message(STATUS "Verifying buildtools integration...")
    
    if(NOT CMAKE_C_COMPILER OR NOT CMAKE_CXX_COMPILER)
        message(FATAL_ERROR "Compilers not configured. Call setup_cross_compilation() first.")
    endif()
    
    # Test 1: Verify gcc has Haiku patches
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -dumpspecs
        OUTPUT_VARIABLE GCC_SPECS
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    if(NOT GCC_SPECS MATCHES "haiku")
        message(WARNING "GCC does not appear to have Haiku patches!")
        message(STATUS "GCC specs: ${GCC_SPECS}")
    else()
        message(STATUS "✓ GCC has Haiku patches")
    endif()
    
    # Test 2: Verify linker has Haiku support  
    execute_process(
        COMMAND ${CMAKE_LINKER} --help
        OUTPUT_VARIABLE LD_HELP
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    if(LD_HELP MATCHES "elf.*haiku")
        message(STATUS "✓ Linker has Haiku support")
    else()
        message(WARNING "Linker may not have Haiku support")
    endif()
    
    # Test 3: Verify compiler can compile simple Haiku code
    set(TEST_SOURCE "${HAIKU_CMAKE_OUTPUT_DIR}/buildtools_test.c")
    file(WRITE ${TEST_SOURCE} "
#ifdef __HAIKU__
#include <OS.h>
int main() { 
    thread_id tid = find_thread(NULL);
    return 0; 
}
#else
#error \"Not compiling for Haiku\"
#endif
")
    
    try_compile(COMPILE_RESULT 
        ${HAIKU_CMAKE_OUTPUT_DIR}/buildtools_test
        SOURCES ${TEST_SOURCE}
        CMAKE_FLAGS 
            "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DCMAKE_SYSROOT=${CMAKE_SYSROOT}"
        OUTPUT_VARIABLE COMPILE_OUTPUT
    )
    
    if(COMPILE_RESULT)
        message(STATUS "✓ Cross-compiler can build Haiku code")
    else()
        message(WARNING "Cross-compiler test failed:")
        message(STATUS "Output: ${COMPILE_OUTPUT}")
    endif()
    
    # Test 4: Check for required Haiku defines
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -dM -E -
        INPUT_STRING ""
        OUTPUT_VARIABLE DEFINES
        ERROR_QUIET
    )
    
    if(DEFINES MATCHES "__HAIKU__")
        message(STATUS "✓ __HAIKU__ is defined")
    else()
        message(WARNING "__HAIKU__ is not defined by compiler")
    endif()
    
    message(STATUS "Buildtools verification completed")
endfunction()

# Function: build_cross_tools
# Builds cross-compilation tools for specified architecture
# Usage: build_cross_tools(ARCH [JOBS])
function(build_cross_tools ARCH)
    cmake_parse_arguments(BUILD_TOOLS
        "" # Options
        "JOBS" # Single value args
        "" # Multi-value args
        ${ARGN}
    )
    
    # Set default job count
    if(NOT BUILD_TOOLS_JOBS)
        include(ProcessorCount)
        ProcessorCount(BUILD_TOOLS_JOBS)
        if(BUILD_TOOLS_JOBS EQUAL 0)
            set(BUILD_TOOLS_JOBS 4)
        endif()
    endif()
    
    message(STATUS "Building cross-tools for ${ARCH} with ${BUILD_TOOLS_JOBS} jobs")
    
    # Check if buildtools directory exists
    if(NOT EXISTS ${BUILDTOOLS_DIR})
        message(FATAL_ERROR "Buildtools directory not found: ${BUILDTOOLS_DIR}")
    endif()
    
    # Set up build directory
    set(BUILD_DIR "${HAIKU_CMAKE_OUTPUT_DIR}/cross-tools-build-${ARCH}")
    file(MAKE_DIRECTORY ${BUILD_DIR})
    
    # Run the build script
    set(BUILD_SCRIPT "${HAIKU_TOP}/build_cross_tools.sh")
    if(NOT EXISTS ${BUILD_SCRIPT})
        message(FATAL_ERROR "Build script not found: ${BUILD_SCRIPT}")
    endif()
    
    message(STATUS "Executing: ${BUILD_SCRIPT} ${ARCH}")
    
    execute_process(
        COMMAND ${BUILD_SCRIPT} ${ARCH}
        WORKING_DIRECTORY ${HAIKU_TOP}
        RESULT_VARIABLE BUILD_RESULT
        OUTPUT_VARIABLE BUILD_OUTPUT
        ERROR_VARIABLE BUILD_ERROR
    )
    
    if(BUILD_RESULT EQUAL 0)
        message(STATUS "✓ Cross-tools built successfully for ${ARCH}")
    else()
        message(FATAL_ERROR "Cross-tools build failed for ${ARCH}: ${BUILD_ERROR}")
    endif()
endfunction()

# Function: find_haiku_headers
# Locates Haiku system headers 
# Usage: find_haiku_headers()
function(find_haiku_headers)
    # Standard header locations
    set(HEADER_PATHS
        ${HAIKU_TOP}/headers/os
        ${HAIKU_TOP}/headers/posix
        ${HAIKU_TOP}/headers/private
        ${HAIKU_TOP}/headers/build
    )
    
    # Check if headers exist
    foreach(HEADER_PATH ${HEADER_PATHS})
        if(EXISTS ${HEADER_PATH})
            message(STATUS "Found headers: ${HEADER_PATH}")
        else()
            message(WARNING "Headers not found: ${HEADER_PATH}")
        endif()
    endforeach()
    
    # Set header paths for use by other functions
    set(HAIKU_HEADER_PATHS ${HEADER_PATHS} PARENT_SCOPE)
endfunction()

# Function: configure_haiku_packages
# Sets up package integration paths
# Usage: configure_haiku_packages()
function(configure_haiku_packages)
    # Package directories
    set(PACKAGE_BASE "${HAIKU_TOP}/generated/build_packages")
    
    if(NOT EXISTS ${PACKAGE_BASE})
        message(WARNING "Package directory not found: ${PACKAGE_BASE}")
        return()
    endif()
    
    # Common packages
    set(PACKAGES
        "zlib"
        "zstd"
        "icu74"
        "gcc_syslibs"
    )
    
    # Find packages for current architecture
    foreach(PACKAGE ${PACKAGES})
        file(GLOB PACKAGE_DIRS "${PACKAGE_BASE}/${PACKAGE}*${HAIKU_ARCH}*")
        if(PACKAGE_DIRS)
            list(GET PACKAGE_DIRS 0 PACKAGE_DIR)
            message(STATUS "Found package: ${PACKAGE} at ${PACKAGE_DIR}")
            
            # Add to CMake prefix path
            if(EXISTS "${PACKAGE_DIR}/develop")
                list(APPEND CMAKE_PREFIX_PATH "${PACKAGE_DIR}/develop")
            endif()
        endif()
    endforeach()
    
    # Update parent scope
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} PARENT_SCOPE)
endfunction()

# Function: setup_haiku_build_environment
# Complete setup of Haiku build environment
# Usage: setup_haiku_build_environment(ARCH)
function(setup_haiku_build_environment ARCH)
    message(STATUS "=== Setting up Haiku build environment ===")
    
    # Set up cross-compilation
    setup_cross_compilation(${ARCH})
    
    # Configure packages
    configure_haiku_packages()
    
    # Find headers
    find_haiku_headers()
    
    message(STATUS "=== Haiku build environment ready ===")
endfunction()

# Automatic setup on module load
if(NOT CMAKE_CROSSCOMPILING AND DEFINED HAIKU_ARCH)
    setup_haiku_build_environment(${HAIKU_ARCH})
endif()

message(STATUS "Loaded BuildtoolsIntegration.cmake module")