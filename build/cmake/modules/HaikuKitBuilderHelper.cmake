# HaikuKitBuilderHelper.cmake - Modern convenience functions for building Haiku kits
#
# This module provides MODERN wrapper functions using CMake 3.25+ return(PROPAGATE)
# with fallback to PARENT_SCOPE for older CMake versions.
# 
# Features:
# - Proper variable scope management (return(PROPAGATE) vs PARENT_SCOPE)
# - Error handling with detailed output
# - Result variables for success/failure checking
# - Modern CMake best practices 2025
#
# Usage in your main CMakeLists.txt:
#   include(HaikuKitBuilderHelper)
#   haiku_kit_init()
#   
#   # Functions with result variables:
#   haiku_configure_kit(app CONFIG_SUCCESS)
#   if(CONFIG_SUCCESS)
#       haiku_build_kit(app BUILD_SUCCESS BUILD_ERROR)
#   endif()
#   
#   # Or combined with error handling:
#   haiku_quick_build_kit(interface SUCCESS ERROR_MSG)
#
# Functions:
#   haiku_kit_init() - setup paths and variables  
#   haiku_configure_kit(NAME SUCCESS_VAR [ERROR_VAR]) - configure with result
#   haiku_build_kit(NAME SUCCESS_VAR [ERROR_VAR]) - build with result
#   haiku_quick_build_kit(NAME SUCCESS_VAR [ERROR_VAR]) - configure + build
#   haiku_clean_temp() - clean temp directory

include_guard(GLOBAL)

# Initialize Haiku kit building environment
macro(haiku_kit_init)
    # Detect HAIKU_ROOT if not set
    if(NOT DEFINED HAIKU_ROOT)
        get_filename_component(HAIKU_ROOT "${CMAKE_SOURCE_DIR}/../../.." ABSOLUTE)
    endif()
    
    # Default architecture
    if(NOT DEFINED HAIKU_ARCH)
        set(HAIKU_ARCH "x86_64")
    endif()
    
    # Setup paths - using macro so variables are available in parent scope
    set(HAIKU_CMAKE_OUTPUT_DIR "${HAIKU_ROOT}/cmake_generated")
    set(HAIKU_TEMP_BUILD_DIR "${HAIKU_CMAKE_OUTPUT_DIR}/temp_build")
    set(HAIKU_KITS_DIR "${HAIKU_CMAKE_OUTPUT_DIR}/objects/haiku/${HAIKU_ARCH}/release/kits")
    
    message(STATUS "HaikuKitBuilderHelper: Initialized")
    message(STATUS "  Architecture: ${HAIKU_ARCH}")
    message(STATUS "  Temp build: ${HAIKU_TEMP_BUILD_DIR}")
    message(STATUS "  Kits output: ${HAIKU_KITS_DIR}")
    
    message(STATUS "Available functions:")
    message(STATUS "  haiku_configure_kit(NAME) - configure kit")
    message(STATUS "  haiku_build_kit(NAME) - build kit") 
    message(STATUS "  haiku_quick_build_kit(NAME) - configure + build")
    message(STATUS "  haiku_clean_temp() - clean temp directory")
endmacro()

# Configure kit in temp directory with modern scope management
function(haiku_configure_kit KIT_NAME SUCCESS_VAR)
    # Parse optional ERROR_VAR argument
    set(ERROR_VAR "")
    if(ARGC GREATER 2)
        set(ERROR_VAR ${ARGV2})
    endif()
    
    if(NOT DEFINED HAIKU_ROOT)
        message(FATAL_ERROR "HaikuKitBuilderHelper: Call haiku_kit_init() first!")
    endif()
    
    set(KIT_SOURCE_DIR "${HAIKU_ROOT}/src/kits/${KIT_NAME}")
    
    if(NOT EXISTS "${KIT_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "HaikuKitBuilderHelper: Kit '${KIT_NAME}' not found in ${KIT_SOURCE_DIR}")
    endif()
    
    message(STATUS "HaikuKitBuilderHelper: Configuring ${KIT_NAME} kit...")
    message(STATUS "  Command: cmake -B ${HAIKU_TEMP_BUILD_DIR} ${KIT_SOURCE_DIR}")
    
    # Execute with proper error capture
    execute_process(
        COMMAND ${CMAKE_COMMAND} -B ${HAIKU_TEMP_BUILD_DIR} ${KIT_SOURCE_DIR}
        WORKING_DIRECTORY ${KIT_SOURCE_DIR}
        RESULT_VARIABLE RESULT
        ERROR_VARIABLE ERROR_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    
    # Set result variables
    if(RESULT EQUAL 0)
        set(${SUCCESS_VAR} TRUE)
        if(ERROR_VAR)
            set(${ERROR_VAR} "")
        endif()
        message(STATUS "HaikuKitBuilderHelper: ${KIT_NAME} configured successfully")
    else
        set(${SUCCESS_VAR} FALSE)
        if(ERROR_VAR)
            set(${ERROR_VAR} "${ERROR_OUTPUT}")
        endif()
        message(WARNING "HaikuKitBuilderHelper: Failed to configure ${KIT_NAME}")
    endif()
    
    # Modern scope propagation (CMake 3.25+) vs legacy fallback
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
        if(ERROR_VAR)
            return(PROPAGATE ${SUCCESS_VAR} ${ERROR_VAR})
        else()
            return(PROPAGATE ${SUCCESS_VAR})
        endif()
    else()
        # Legacy PARENT_SCOPE for older CMake
        set(${SUCCESS_VAR} ${${SUCCESS_VAR}} PARENT_SCOPE)
        if(ERROR_VAR)
            set(${ERROR_VAR} "${${ERROR_VAR}}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# Build already-configured kit with modern scope management
function(haiku_build_kit KIT_NAME SUCCESS_VAR)
    # Parse optional ERROR_VAR argument
    set(ERROR_VAR "")
    if(ARGC GREATER 2)
        set(ERROR_VAR ${ARGV2})
    endif()
    
    if(NOT DEFINED HAIKU_TEMP_BUILD_DIR)
        message(FATAL_ERROR "HaikuKitBuilderHelper: Call haiku_kit_init() first!")
    endif()
    
    if(NOT EXISTS "${HAIKU_TEMP_BUILD_DIR}/CMakeCache.txt")
        message(FATAL_ERROR "HaikuKitBuilderHelper: Temp build not configured. Call haiku_configure_kit() first!")
    endif()
    
    message(STATUS "HaikuKitBuilderHelper: Building ${KIT_NAME} kit...")
    message(STATUS "  Command: cmake --build ${HAIKU_TEMP_BUILD_DIR} -j4")
    
    # Execute with proper error capture
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${HAIKU_TEMP_BUILD_DIR} -j4
        RESULT_VARIABLE RESULT
        ERROR_VARIABLE ERROR_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    
    # Set result variables
    if(RESULT EQUAL 0)
        set(${SUCCESS_VAR} TRUE)
        if(ERROR_VAR)
            set(${ERROR_VAR} "")
        endif()
        message(STATUS "HaikuKitBuilderHelper: ${KIT_NAME} built successfully")
        message(STATUS "  Output available in: ${HAIKU_KITS_DIR}/${KIT_NAME}/")
    else
        set(${SUCCESS_VAR} FALSE)
        if(ERROR_VAR)
            set(${ERROR_VAR} "${ERROR_OUTPUT}")
        endif()
        message(WARNING "HaikuKitBuilderHelper: Failed to build ${KIT_NAME}")
    endif()
    
    # Modern scope propagation (CMake 3.25+) vs legacy fallback
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
        if(ERROR_VAR)
            return(PROPAGATE ${SUCCESS_VAR} ${ERROR_VAR})
        else()
            return(PROPAGATE ${SUCCESS_VAR})
        endif()
    else()
        # Legacy PARENT_SCOPE for older CMake
        set(${SUCCESS_VAR} ${${SUCCESS_VAR}} PARENT_SCOPE)
        if(ERROR_VAR)
            set(${ERROR_VAR} "${${ERROR_VAR}}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# Configure + build in one call with modern scope management
function(haiku_quick_build_kit KIT_NAME SUCCESS_VAR)
    # Parse optional ERROR_VAR argument
    set(ERROR_VAR "")
    if(ARGC GREATER 2)
        set(ERROR_VAR ${ARGV2})
    endif()
    
    message(STATUS "HaikuKitBuilderHelper: Quick build ${KIT_NAME} kit...")
    
    # Step 1: Configure
    haiku_configure_kit(${KIT_NAME} CONFIG_SUCCESS CONFIG_ERROR)
    
    if(NOT CONFIG_SUCCESS)
        set(${SUCCESS_VAR} FALSE)
        if(ERROR_VAR)
            set(${ERROR_VAR} "Configuration failed: ${CONFIG_ERROR}")
        endif()
        message(WARNING "HaikuKitBuilderHelper: Quick build failed at configuration step")
    else
        # Step 2: Build
        haiku_build_kit(${KIT_NAME} BUILD_SUCCESS BUILD_ERROR)
        
        if(BUILD_SUCCESS)
            set(${SUCCESS_VAR} TRUE)
            if(ERROR_VAR)
                set(${ERROR_VAR} "")
            endif()
            message(STATUS "HaikuKitBuilderHelper: Quick build ${KIT_NAME} completed successfully")
        else
            set(${SUCCESS_VAR} FALSE)
            if(ERROR_VAR)
                set(${ERROR_VAR} "Build failed: ${BUILD_ERROR}")
            endif()
            message(WARNING "HaikuKitBuilderHelper: Quick build failed at build step")
        endif()
    endif()
    
    # Modern scope propagation (CMake 3.25+) vs legacy fallback
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
        if(ERROR_VAR)
            return(PROPAGATE ${SUCCESS_VAR} ${ERROR_VAR})
        else()
            return(PROPAGATE ${SUCCESS_VAR})
        endif()
    else()
        # Legacy PARENT_SCOPE for older CMake
        set(${SUCCESS_VAR} ${${SUCCESS_VAR}} PARENT_SCOPE)
        if(ERROR_VAR)
            set(${ERROR_VAR} "${${ERROR_VAR}}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# Clean temp directory
function(haiku_clean_temp)
    if(NOT DEFINED HAIKU_TEMP_BUILD_DIR)
        message(FATAL_ERROR "HaikuKitBuilderHelper: Call haiku_kit_init() first!")
    endif()
    
    message(STATUS "HaikuKitBuilderHelper: Cleaning temp directory...")
    message(STATUS "  Removing: ${HAIKU_TEMP_BUILD_DIR}")
    
    if(EXISTS ${HAIKU_TEMP_BUILD_DIR})
        file(REMOVE_RECURSE ${HAIKU_TEMP_BUILD_DIR})
        message(STATUS "HaikuKitBuilderHelper: Temp directory cleaned")
    else
        message(STATUS "HaikuKitBuilderHelper: Temp directory already clean")
    endif()
endfunction()

# Show usage help with modern API
function(haiku_kit_help)
    message(STATUS "HaikuKitBuilderHelper Usage (Modern CMake 2025):")
    message(STATUS "")
    message(STATUS "1. Initialize:")
    message(STATUS "   haiku_kit_init()")
    message(STATUS "")
    message(STATUS "2. Build a kit with error handling:")
    message(STATUS "   haiku_configure_kit(app CONFIG_SUCCESS CONFIG_ERROR)")
    message(STATUS "   if(CONFIG_SUCCESS)")
    message(STATUS "       haiku_build_kit(app BUILD_SUCCESS BUILD_ERROR)")
    message(STATUS "   endif()")
    message(STATUS "")
    message(STATUS "3. Or quick build:")
    message(STATUS "   haiku_quick_build_kit(interface SUCCESS ERROR_MSG)")
    message(STATUS "   if(NOT SUCCESS)")
    message(STATUS "       message(FATAL_ERROR \"Build failed: \${ERROR_MSG}\")")
    message(STATUS "   endif()")
    message(STATUS "")
    message(STATUS "4. Clean temp:")
    message(STATUS "   haiku_clean_temp()")
    message(STATUS "")
    message(STATUS "Available kits: app, interface, locale, support")
    message(STATUS "")
    message(STATUS "Features:")
    message(STATUS "- Modern scope management (return(PROPAGATE) for CMake 3.25+)")
    message(STATUS "- Proper error handling with detailed messages") 
    message(STATUS "- Result variables for programmatic usage")
    message(STATUS "- Fallback to PARENT_SCOPE for older CMake versions")
endfunction()