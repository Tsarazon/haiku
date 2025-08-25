# HaikuCommon.cmake
# Core CMake functions for Haiku OS build system
# Provides functions equivalent to common Jam rules

cmake_minimum_required(VERSION 3.16)

# Global variables for Haiku build
if(NOT DEFINED HAIKU_TOP)
    set(HAIKU_TOP "${CMAKE_SOURCE_DIR}")
endif()

if(NOT DEFINED HAIKU_ARCH)
    set(HAIKU_ARCH "x86_64")
endif()

if(NOT DEFINED HAIKU_TARGET)
    set(HAIKU_TARGET "${HAIKU_ARCH}-unknown-haiku")
endif()

# Common compiler and linker flags for Haiku
set(HAIKU_COMMON_C_FLAGS "-fno-strict-aliasing -fno-delete-null-pointer-checks")
set(HAIKU_COMMON_CXX_FLAGS "-fno-strict-aliasing -fno-delete-null-pointer-checks -std=c++17")
set(HAIKU_COMMON_LINK_FLAGS "")

# Architecture-specific flags
if(HAIKU_ARCH STREQUAL "x86_64")
    list(APPEND HAIKU_COMMON_C_FLAGS "-m64" "-march=x86-64")
    list(APPEND HAIKU_COMMON_CXX_FLAGS "-m64" "-march=x86-64")
elseif(HAIKU_ARCH STREQUAL "x86")
    list(APPEND HAIKU_COMMON_C_FLAGS "-m32" "-march=i586")
    list(APPEND HAIKU_COMMON_CXX_FLAGS "-m32" "-march=i586")
elseif(HAIKU_ARCH STREQUAL "arm64")
    list(APPEND HAIKU_COMMON_C_FLAGS "-march=armv8-a")
    list(APPEND HAIKU_COMMON_CXX_FLAGS "-march=armv8-a")
elseif(HAIKU_ARCH STREQUAL "riscv64")
    list(APPEND HAIKU_COMMON_C_FLAGS "-march=rv64imafdc")
    list(APPEND HAIKU_COMMON_CXX_FLAGS "-march=rv64imafdc")
endif()

# Function: haiku_application
# Creates a Haiku application (equivalent to Jam's Application rule)
# Usage: haiku_application(TARGET_NAME SOURCES source1.cpp source2.cpp LIBRARIES lib1 lib2 [RESOURCES resources.rdef])
function(haiku_application TARGET_NAME)
    cmake_parse_arguments(HAIKU_APP 
        "" # Options
        "" # Single value args
        "SOURCES;LIBRARIES;RESOURCES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_APP_SOURCES)
        message(FATAL_ERROR "haiku_application: SOURCES required for ${TARGET_NAME}")
    endif()
    
    # Create the executable
    add_executable(${TARGET_NAME} ${HAIKU_APP_SOURCES})
    
    # Set Haiku-specific properties  
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${HAIKU_APPS_DIR}"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    
    # Apply common flags
    target_compile_options(${TARGET_NAME} PRIVATE 
        $<$<COMPILE_LANGUAGE:C>:${HAIKU_COMMON_C_FLAGS}>
        $<$<COMPILE_LANGUAGE:CXX>:${HAIKU_COMMON_CXX_FLAGS}>
    )
    
    # Link with specified libraries
    if(HAIKU_APP_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_APP_LIBRARIES})
    endif()
    
    # Add Haiku-specific defines
    target_compile_definitions(${TARGET_NAME} PRIVATE 
        __HAIKU__
        _GNU_SOURCE
    )
    
    message(STATUS "Created Haiku application: ${TARGET_NAME}")
endfunction()

# Function: haiku_library  
# Creates a Haiku shared or static library (equivalent to Jam's SharedLibrary/StaticLibrary)
# Usage: haiku_library(TARGET_NAME TYPE SHARED/STATIC SOURCES source1.cpp LIBRARIES lib1)
function(haiku_library TARGET_NAME TYPE)
    cmake_parse_arguments(HAIKU_LIB 
        "" # Options
        "" # Single value args  
        "SOURCES;LIBRARIES;VERSION;SOVERSION" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_LIB_SOURCES)
        message(FATAL_ERROR "haiku_library: SOURCES required for ${TARGET_NAME}")
    endif()
    
    if(NOT TYPE STREQUAL "SHARED" AND NOT TYPE STREQUAL "STATIC")
        message(FATAL_ERROR "haiku_library: TYPE must be SHARED or STATIC")
    endif()
    
    # Create the library
    add_library(${TARGET_NAME} ${TYPE} ${HAIKU_LIB_SOURCES})
    
    # Set output directory based on type - use standardized paths
    if(TYPE STREQUAL "SHARED")
        set_target_properties(${TARGET_NAME} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${HAIKU_LIBS_DIR}"
        )
    else()
        set_target_properties(${TARGET_NAME} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${HAIKU_LIBS_DIR}"
        )
    endif()
    
    # Set version information for shared libraries
    if(TYPE STREQUAL "SHARED" AND HAIKU_LIB_VERSION)
        set_target_properties(${TARGET_NAME} PROPERTIES
            VERSION ${HAIKU_LIB_VERSION}
            SOVERSION ${HAIKU_LIB_SOVERSION}
        )
    endif()
    
    # Apply common flags
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${HAIKU_COMMON_C_FLAGS}>
        $<$<COMPILE_LANGUAGE:CXX>:${HAIKU_COMMON_CXX_FLAGS}>
    )
    
    # Link with specified libraries
    if(HAIKU_LIB_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_LIB_LIBRARIES})
    endif()
    
    # Add Haiku-specific defines
    target_compile_definitions(${TARGET_NAME} PRIVATE 
        __HAIKU__
        _GNU_SOURCE
    )
    
    # For shared libraries, add -fPIC
    if(TYPE STREQUAL "SHARED")
        set_target_properties(${TARGET_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    
    message(STATUS "Created Haiku ${TYPE} library: ${TARGET_NAME}")
endfunction()

# Function: haiku_server
# Creates a Haiku system server (equivalent to Jam's Server rule) 
# Usage: haiku_server(TARGET_NAME SOURCES source1.cpp LIBRARIES lib1)
function(haiku_server TARGET_NAME)
    cmake_parse_arguments(HAIKU_SERVER
        "" # Options
        "" # Single value args
        "SOURCES;LIBRARIES;RESOURCES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_SERVER_SOURCES)
        message(FATAL_ERROR "haiku_server: SOURCES required for ${TARGET_NAME}")
    endif()
    
    # Create the executable
    add_executable(${TARGET_NAME} ${HAIKU_SERVER_SOURCES})
    
    # Set properties for servers - use Jam-compatible structure
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${HAIKU_SERVERS_DIR}"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    
    # Apply common flags
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${HAIKU_COMMON_C_FLAGS}>
        $<$<COMPILE_LANGUAGE:CXX>:${HAIKU_COMMON_CXX_FLAGS}>
    )
    
    # Link with specified libraries
    if(HAIKU_SERVER_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_SERVER_LIBRARIES})
    endif()
    
    # Add Haiku-specific defines
    target_compile_definitions(${TARGET_NAME} PRIVATE 
        __HAIKU__
        _GNU_SOURCE
    )
    
    message(STATUS "Created Haiku server: ${TARGET_NAME}")
endfunction()

# Function: haiku_addon
# Creates a Haiku add-on (equivalent to Jam's Addon rule)
# Usage: haiku_addon(TARGET_NAME SOURCES source1.cpp LIBRARIES lib1)
function(haiku_addon TARGET_NAME)
    cmake_parse_arguments(HAIKU_ADDON
        "" # Options
        "" # Single value args
        "SOURCES;LIBRARIES;SUBDIR" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_ADDON_SOURCES)
        message(FATAL_ERROR "haiku_addon: SOURCES required for ${TARGET_NAME}")
    endif()
    
    # Create the shared library (add-ons are shared libraries)
    add_library(${TARGET_NAME} SHARED ${HAIKU_ADDON_SOURCES})
    
    # Set output directory - use Jam-compatible structure
    set(ADDON_OUTPUT_DIR "${HAIKU_ADDONS_DIR}")
    if(HAIKU_ADDON_SUBDIR)
        set(ADDON_OUTPUT_DIR "${ADDON_OUTPUT_DIR}/${HAIKU_ADDON_SUBDIR}")
        file(MAKE_DIRECTORY ${ADDON_OUTPUT_DIR})
    endif()
    
    set_target_properties(${TARGET_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${ADDON_OUTPUT_DIR}"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Apply common flags
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${HAIKU_COMMON_C_FLAGS}>
        $<$<COMPILE_LANGUAGE:CXX>:${HAIKU_COMMON_CXX_FLAGS}>
    )
    
    # Link with specified libraries
    if(HAIKU_ADDON_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_ADDON_LIBRARIES})
    endif()
    
    # Add Haiku-specific defines
    target_compile_definitions(${TARGET_NAME} PRIVATE 
        __HAIKU__
        _GNU_SOURCE
    )
    
    message(STATUS "Created Haiku add-on: ${TARGET_NAME}")
endfunction()

# Function: haiku_kernel_module
# Creates a Haiku kernel add-on (equivalent to Jam's KernelAddon rule)
# Usage: haiku_kernel_module(TARGET_NAME SOURCES source1.cpp LIBRARIES kernel_libs)
function(haiku_kernel_module TARGET_NAME)
    cmake_parse_arguments(HAIKU_KERNEL
        "" # Options
        "" # Single value args
        "SOURCES;LIBRARIES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_KERNEL_SOURCES)
        message(FATAL_ERROR "haiku_kernel_module: SOURCES required for ${TARGET_NAME}")
    endif()
    
    # Create the shared library
    add_library(${TARGET_NAME} SHARED ${HAIKU_KERNEL_SOURCES})
    
    # Set properties for kernel modules
    set_target_properties(${TARGET_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${HAIKU_ADDONS_DIR}/kernel"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Kernel-specific flags
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${HAIKU_COMMON_C_FLAGS} -fno-stack-protector>
        $<$<COMPILE_LANGUAGE:CXX>:${HAIKU_COMMON_CXX_FLAGS} -fno-stack-protector>
    )
    
    # Link with kernel libraries
    if(HAIKU_KERNEL_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_KERNEL_LIBRARIES})
    endif()
    
    # Kernel-specific defines
    target_compile_definitions(${TARGET_NAME} PRIVATE 
        __HAIKU__
        _GNU_SOURCE
        _KERNEL_MODE
    )
    
    message(STATUS "Created Haiku kernel module: ${TARGET_NAME}")
endfunction()

# Function: haiku_driver
# Creates a Haiku device driver (equivalent to Jam's Driver rule)
# Usage: haiku_driver(TARGET_NAME SOURCES source1.cpp)
function(haiku_driver TARGET_NAME)
    cmake_parse_arguments(HAIKU_DRIVER
        "" # Options
        "" # Single value args
        "SOURCES;LIBRARIES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_DRIVER_SOURCES)
        message(FATAL_ERROR "haiku_driver: SOURCES required for ${TARGET_NAME}")
    endif()
    
    # Drivers are kernel modules in a specific directory
    add_library(${TARGET_NAME} SHARED ${HAIKU_DRIVER_SOURCES})
    
    set_target_properties(${TARGET_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${HAIKU_ADDONS_DIR}/kernel/drivers"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Driver-specific flags (similar to kernel modules)
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${HAIKU_COMMON_C_FLAGS} -fno-stack-protector>
        $<$<COMPILE_LANGUAGE:CXX>:${HAIKU_COMMON_CXX_FLAGS} -fno-stack-protector>
    )
    
    # Link with libraries if specified
    if(HAIKU_DRIVER_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_DRIVER_LIBRARIES})
    endif()
    
    # Driver-specific defines
    target_compile_definitions(${TARGET_NAME} PRIVATE 
        __HAIKU__
        _GNU_SOURCE
        _KERNEL_MODE
    )
    
    message(STATUS "Created Haiku driver: ${TARGET_NAME}")
endfunction()

# Function: haiku_static_library
# Creates a Haiku static library (equivalent to Jam's StaticLibrary rule)
# Usage: haiku_static_library(TARGET_NAME SOURCES source1.cpp)
function(haiku_static_library TARGET_NAME)
    cmake_parse_arguments(HAIKU_STATIC
        "" # Options
        "" # Single value args
        "SOURCES;LIBRARIES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_STATIC_SOURCES)
        message(FATAL_ERROR "haiku_static_library: SOURCES required for ${TARGET_NAME}")
    endif()
    
    # Create static library
    add_library(${TARGET_NAME} STATIC ${HAIKU_STATIC_SOURCES})
    
    set_target_properties(${TARGET_NAME} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${HAIKU_LIBS_DIR}"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    
    # Apply common flags
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${HAIKU_COMMON_C_FLAGS}>
        $<$<COMPILE_LANGUAGE:CXX>:${HAIKU_COMMON_CXX_FLAGS}>
    )
    
    # Link with specified libraries
    if(HAIKU_STATIC_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_STATIC_LIBRARIES})
    endif()
    
    # Add Haiku-specific defines
    target_compile_definitions(${TARGET_NAME} PRIVATE 
        __HAIKU__
        _GNU_SOURCE
    )
    
    message(STATUS "Created Haiku static library: ${TARGET_NAME}")
endfunction()

# Function: haiku_object_library
# Creates object files that can be reused in multiple targets (like Jam's Objects rule)
# Usage: haiku_object_library(TARGET_NAME SOURCES source1.cpp)
function(haiku_object_library TARGET_NAME)
    cmake_parse_arguments(HAIKU_OBJ
        "" # Options
        "" # Single value args
        "SOURCES;LIBRARIES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_OBJ_SOURCES)
        message(FATAL_ERROR "haiku_object_library: SOURCES required for ${TARGET_NAME}")
    endif()
    
    # Create object library
    add_library(${TARGET_NAME} OBJECT ${HAIKU_OBJ_SOURCES})
    
    set_target_properties(${TARGET_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Apply common flags
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:${HAIKU_COMMON_C_FLAGS}>
        $<$<COMPILE_LANGUAGE:CXX>:${HAIKU_COMMON_CXX_FLAGS}>
    )
    
    # Link with specified libraries (for interface requirements)
    if(HAIKU_OBJ_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_OBJ_LIBRARIES})
    endif()
    
    # Add Haiku-specific defines
    target_compile_definitions(${TARGET_NAME} PRIVATE 
        __HAIKU__
        _GNU_SOURCE
    )
    
    message(STATUS "Created Haiku object library: ${TARGET_NAME}")
endfunction()

message(STATUS "Loaded HaikuCommon.cmake module")