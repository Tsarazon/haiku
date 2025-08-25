# HaikuTarget.cmake - Advanced CMake module for building Haiku targets
#
# This module follows Modern CMake 3.16+ best practices and patterns:
# - Proper argument parsing with single parse per function
# - INTERFACE target support with transitive dependencies
# - BUILD_INTERFACE/INSTALL_INTERFACE generator expressions
# - Alias targets for convenience
# - Target-based architecture with clean property inheritance
# - No global property pollution or hardcoded paths
# - Support for imported targets and package configuration
#
# Functions provided:
#   haiku_add_target(TYPE NAME [options...])
#   haiku_create_target_alias(TARGET_NAME ALIAS_NAME)
#   haiku_get_targets_by_type(TYPE OUTPUT_VAR)
#   haiku_get_target_output(NAME OUTPUT_VAR)
#   haiku_target_exists(NAME RESULT_VAR)
#
# Advanced usage examples:
#   # Kit with interface dependencies
#   haiku_add_target(KIT interface
#       SOURCES AboutWindow.cpp Alert.cpp
#       PUBLIC_INCLUDES
#           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
#           $<INSTALL_INTERFACE:include>
#       INTERFACE_INCLUDES
#           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/textview_support>
#       DEPENDS shared_utils
#   )
#
#   # Application with imported target dependencies  
#   haiku_add_target(EXECUTABLE HelloWorld
#       SOURCES HelloWorld.cpp
#       LINK_LIBRARIES Haiku::Interface Haiku::Support
#       INSTALL
#   )

include_guard(GLOBAL)

# Check CMake version for advanced features
if(CMAKE_VERSION VERSION_LESS 3.16)
    message(FATAL_ERROR "HaikuTarget: Requires CMake 3.16 or newer for modern target support")
endif()

# Validate environment and initialize defaults
macro(_haiku_validate_environment)
    # Use parent scope variables directly in macro
    if(NOT HAIKU_ROOT)
        message(FATAL_ERROR "HaikuTarget: HAIKU_ROOT must be set before using this module")
    endif()
    
    if(NOT HAIKU_ARCH)
        set(HAIKU_ARCH "x86_64")
        message(STATUS "HaikuTarget: HAIKU_ARCH defaulted to ${HAIKU_ARCH}")
    endif()
    
    if(NOT HAIKU_TARGET_TRIPLET)
        set(HAIKU_TARGET_TRIPLET "${HAIKU_ARCH}-unknown-haiku")
        message(STATUS "HaikuTarget: HAIKU_TARGET_TRIPLET defaulted to ${HAIKU_TARGET_TRIPLET}")
    endif()
    
    # Validate cross-compilation setup
    if(NOT CMAKE_CROSSCOMPILING)
        message(WARNING "HaikuTarget: Not cross-compiling - ensure CMAKE_TOOLCHAIN_FILE is set for Haiku")
    endif()
    
    # Ensure required tools are available
    if(NOT CMAKE_LINKER)
        message(FATAL_ERROR "HaikuTarget: CMAKE_LINKER not available - check cross-compilation setup")
    endif()
endmacro()

# Get configurable output paths with validation
function(_haiku_get_output_paths TYPE NAME BASE_DIR OUTPUT_DIR_VAR OUTPUT_FILE_VAR)
    # Use CMAKE_BINARY_DIR as fallback for flexibility
    if(NOT BASE_DIR)
        if(HAIKU_ROOT AND EXISTS "${HAIKU_ROOT}")
            set(BASE_DIR "${HAIKU_ROOT}/cmake_generated")
        else()
            set(BASE_DIR "${CMAKE_BINARY_DIR}")
        endif()
    endif()
    
    # Create configurable directory structure
    if(CMAKE_BUILD_TYPE)
        string(TOLOWER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_LOWER)
    else()
        set(BUILD_TYPE_LOWER "release")
    endif()
    
    set(ARCH_DIR "${BASE_DIR}/objects/haiku/${HAIKU_ARCH}/${BUILD_TYPE_LOWER}")
    
    # Type-specific output paths
    if(TYPE STREQUAL "KIT")
        set(OUTPUT_DIR "${ARCH_DIR}/kits/${NAME}")
        set(OUTPUT_FILE "${OUTPUT_DIR}/${NAME}_kit.o")
    elseif(TYPE MATCHES "^(EXECUTABLE|APP)$")
        set(OUTPUT_DIR "${ARCH_DIR}/apps")
        set(OUTPUT_FILE "${OUTPUT_DIR}/${NAME}")
    elseif(TYPE STREQUAL "SHARED_LIBRARY")
        set(OUTPUT_DIR "${ARCH_DIR}/libs")
        set(OUTPUT_FILE "${OUTPUT_DIR}/lib${NAME}.so")
    elseif(TYPE STREQUAL "STATIC_LIBRARY")
        set(OUTPUT_DIR "${ARCH_DIR}/libs")
        set(OUTPUT_FILE "${OUTPUT_DIR}/lib${NAME}.a")
    elseif(TYPE STREQUAL "TOOL")
        set(OUTPUT_DIR "${ARCH_DIR}/tools")
        set(OUTPUT_FILE "${OUTPUT_DIR}/${NAME}")
    elseif(TYPE STREQUAL "INTERFACE")
        # Interface targets don't produce files
        set(OUTPUT_DIR "")
        set(OUTPUT_FILE "")
    else()
        message(FATAL_ERROR "HaikuTarget: Unknown target type '${TYPE}'. Valid: KIT, EXECUTABLE, APP, SHARED_LIBRARY, STATIC_LIBRARY, TOOL, INTERFACE")
    endif()
    
    # Create directories if needed
    if(OUTPUT_DIR)
        file(MAKE_DIRECTORY "${OUTPUT_DIR}")
    endif()
    
    set(${OUTPUT_DIR_VAR} "${OUTPUT_DIR}" PARENT_SCOPE)
    set(${OUTPUT_FILE_VAR} "${OUTPUT_FILE}" PARENT_SCOPE)
endfunction()

# Apply modern target properties with proper inheritance patterns
function(_haiku_apply_target_properties TARGET_NAME PARSED_ARGS_PREFIX)
    # Access already-parsed arguments using prefix
    set(options "${${PARSED_ARGS_PREFIX}_NO_DEFAULT_FLAGS};${${PARSED_ARGS_PREFIX}_VERBOSE}")
    set(oneValueArgs "${${PARSED_ARGS_PREFIX}_OUTPUT_NAME}")
    set(multiValueArgs "${${PARSED_ARGS_PREFIX}_INCLUDES};${${PARSED_ARGS_PREFIX}_PUBLIC_INCLUDES};${${PARSED_ARGS_PREFIX}_INTERFACE_INCLUDES}")
    
    # Modern include directory handling with proper scoping
    if(${PARSED_ARGS_PREFIX}_INCLUDES)
        target_include_directories(${TARGET_NAME} PRIVATE ${${PARSED_ARGS_PREFIX}_INCLUDES})
    endif()
    
    if(${PARSED_ARGS_PREFIX}_PUBLIC_INCLUDES)
        target_include_directories(${TARGET_NAME} PUBLIC ${${PARSED_ARGS_PREFIX}_PUBLIC_INCLUDES})
    endif()
    
    if(${PARSED_ARGS_PREFIX}_INTERFACE_INCLUDES)
        target_include_directories(${TARGET_NAME} INTERFACE ${${PARSED_ARGS_PREFIX}_INTERFACE_INCLUDES})
    endif()
    
    # Modern compile definitions with generator expressions
    set(DEFAULT_DEFINITIONS "")
    if(NOT ${PARSED_ARGS_PREFIX}_NO_DEFAULT_FLAGS)
        list(APPEND DEFAULT_DEFINITIONS 
            ZSTD_ENABLED 
            HAIKU_TARGET_PLATFORM_HAIKU
            $<$<CONFIG:Debug>:DEBUG=1>
            $<$<CONFIG:Release>:NDEBUG>
            $<$<CONFIG:RelWithDebInfo>:NDEBUG>
            $<$<CONFIG:MinSizeRel>:NDEBUG>
        )
    endif()
    
    if(${PARSED_ARGS_PREFIX}_DEFINITIONS)
        list(APPEND DEFAULT_DEFINITIONS ${${PARSED_ARGS_PREFIX}_DEFINITIONS})
    endif()
    
    if(DEFAULT_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PRIVATE ${DEFAULT_DEFINITIONS})
    endif()
    
    # Public and interface definitions
    if(${PARSED_ARGS_PREFIX}_PUBLIC_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} PUBLIC ${${PARSED_ARGS_PREFIX}_PUBLIC_DEFINITIONS})
    endif()
    
    if(${PARSED_ARGS_PREFIX}_INTERFACE_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} INTERFACE ${${PARSED_ARGS_PREFIX}_INTERFACE_DEFINITIONS})
    endif()
    
    # Modern compile options with configuration-aware flags
    set(DEFAULT_FLAGS "")
    if(NOT ${PARSED_ARGS_PREFIX}_NO_DEFAULT_FLAGS)
        list(APPEND DEFAULT_FLAGS 
            -fno-strict-aliasing
            $<$<CONFIG:Debug>:-g -O0 -fno-omit-frame-pointer>
            $<$<CONFIG:Release>:-O2 -DNDEBUG>
            $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
            $<$<CONFIG:MinSizeRel>:-Os -DNDEBUG>
        )
    endif()
    
    if(${PARSED_ARGS_PREFIX}_FLAGS)
        list(APPEND DEFAULT_FLAGS ${${PARSED_ARGS_PREFIX}_FLAGS})
    endif()
    
    if(DEFAULT_FLAGS)
        target_compile_options(${TARGET_NAME} PRIVATE ${DEFAULT_FLAGS})
    endif()
    
    # Advanced library linking with imported target support
    if(${PARSED_ARGS_PREFIX}_LINK_LIBRARIES)
        foreach(LIB ${${PARSED_ARGS_PREFIX}_LINK_LIBRARIES})
            # Prefer imported targets (Haiku::Interface, ZLIB::ZLIB, etc.)
            if(TARGET ${LIB})
                target_link_libraries(${TARGET_NAME} PRIVATE ${LIB})
            else()
                # Legacy library names
                target_link_libraries(${TARGET_NAME} PRIVATE ${LIB})
                if(${PARSED_ARGS_PREFIX}_VERBOSE)
                    message(STATUS "HaikuTarget: Linking '${LIB}' as legacy library (consider creating imported target)")
                endif()
            endif()
        endforeach()
    endif()
    
    # Public and interface library dependencies
    if(${PARSED_ARGS_PREFIX}_PUBLIC_LINK_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PUBLIC ${${PARSED_ARGS_PREFIX}_PUBLIC_LINK_LIBRARIES})
    endif()
    
    if(${PARSED_ARGS_PREFIX}_INTERFACE_LINK_LIBRARIES)
        target_link_libraries(${TARGET_NAME} INTERFACE ${${PARSED_ARGS_PREFIX}_INTERFACE_LINK_LIBRARIES})
    endif()
    
    # Modern target properties for cross-compilation
    set_target_properties(${TARGET_NAME} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        # Disable automatic standard library linking for cross-compilation
        CXX_STANDARD_LIBRARIES ""
        # Export compile commands for IDE support
        EXPORT_COMPILE_COMMANDS ON
    )
    
    # Conditional properties based on parsed arguments
    if(${PARSED_ARGS_PREFIX}_OUTPUT_NAME)
        set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME ${${PARSED_ARGS_PREFIX}_OUTPUT_NAME})
    endif()
    
    if(${PARSED_ARGS_PREFIX}_VERSION)
        set_target_properties(${TARGET_NAME} PROPERTIES VERSION ${${PARSED_ARGS_PREFIX}_VERSION})
    endif()
    
    if(${PARSED_ARGS_PREFIX}_SOVERSION)
        set_target_properties(${TARGET_NAME} PROPERTIES SOVERSION ${${PARSED_ARGS_PREFIX}_SOVERSION})
    endif()
endfunction()

# Advanced kit building with object library + merged output
function(_haiku_build_kit NAME TARGET_NAME INTERNAL_TARGET_NAME OUTPUT_FILE PARSED_ARGS_PREFIX)
    if(NOT ${PARSED_ARGS_PREFIX}_SOURCES)
        message(FATAL_ERROR "HaikuTarget: No SOURCES provided for kit '${NAME}'")
    endif()
    
    # Create object library with proper namespacing
    add_library(${INTERNAL_TARGET_NAME} OBJECT ${${PARSED_ARGS_PREFIX}_SOURCES})
    
    # Apply all properties to object library
    _haiku_apply_target_properties(${INTERNAL_TARGET_NAME} ${PARSED_ARGS_PREFIX})
    
    # Create merged object file with error checking and verbose output
    add_custom_command(
        OUTPUT "${OUTPUT_FILE}"
        COMMAND ${CMAKE_COMMAND} -E echo "HaikuTarget: Merging ${NAME} kit objects..."
        COMMAND "${CMAKE_LINKER}" -r "$<TARGET_OBJECTS:${INTERNAL_TARGET_NAME}>" -o "${OUTPUT_FILE}"
        COMMAND ${CMAKE_COMMAND} -E echo "HaikuTarget: Created ${OUTPUT_FILE}"
        DEPENDS ${INTERNAL_TARGET_NAME}
        COMMENT "Merging objects into ${NAME}_kit.o (Jam MergeObject compatible)"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    
    # Create main target that depends on merged object
    add_custom_target(${TARGET_NAME} ALL DEPENDS "${OUTPUT_FILE}")
    
    # Handle target dependencies with validation
    if(${PARSED_ARGS_PREFIX}_DEPENDS)
        foreach(DEP ${${PARSED_ARGS_PREFIX}_DEPENDS})
            if(TARGET "haiku_${DEP}")
                add_dependencies(${TARGET_NAME} "haiku_${DEP}")
            elseif(TARGET "${DEP}")
                add_dependencies(${TARGET_NAME} "${DEP}")
            else()
                message(FATAL_ERROR "HaikuTarget: Dependency '${DEP}' not found for kit '${NAME}'")
            endif()
        endforeach()
    endif()
endfunction()

# Build executables with modern dependency handling
function(_haiku_build_executable NAME TARGET_NAME OUTPUT_DIR PARSED_ARGS_PREFIX)
    if(NOT ${PARSED_ARGS_PREFIX}_SOURCES)
        message(FATAL_ERROR "HaikuTarget: No SOURCES provided for executable '${NAME}'")
    endif()
    
    add_executable(${TARGET_NAME} ${${PARSED_ARGS_PREFIX}_SOURCES})
    
    # Set output directory with per-config support
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${OUTPUT_DIR}"
    )
    
    # Apply properties
    _haiku_apply_target_properties(${TARGET_NAME} ${PARSED_ARGS_PREFIX})
    
    # Handle dependencies
    if(${PARSED_ARGS_PREFIX}_DEPENDS)
        foreach(DEP ${${PARSED_ARGS_PREFIX}_DEPENDS})
            if(TARGET "haiku_${DEP}")
                add_dependencies(${TARGET_NAME} "haiku_${DEP}")
            elseif(TARGET "${DEP}")
                add_dependencies(${TARGET_NAME} "${DEP}")
            endif()
        endforeach()
    endif()
endfunction()

# Build shared libraries with object library support
function(_haiku_build_shared_library NAME TARGET_NAME OUTPUT_DIR PARSED_ARGS_PREFIX)
    if(${PARSED_ARGS_PREFIX}_SOURCES)
        # Build from source files
        add_library(${TARGET_NAME} SHARED ${${PARSED_ARGS_PREFIX}_SOURCES})
    elseif(${PARSED_ARGS_PREFIX}_OBJECT_LIBRARIES)
        # Build from object libraries (like libbe.so from kits)
        set(OBJECT_SOURCES "")
        foreach(OBJ_LIB ${${PARSED_ARGS_PREFIX}_OBJECT_LIBRARIES})
            set(OBJ_TARGET "haiku_${OBJ_LIB}_internal")
            if(TARGET ${OBJ_TARGET})
                list(APPEND OBJECT_SOURCES "$<TARGET_OBJECTS:${OBJ_TARGET}>")
            else()
                message(FATAL_ERROR "HaikuTarget: Object library '${OBJ_LIB}' not found for shared library '${NAME}'")
            endif()
        endforeach()
        add_library(${TARGET_NAME} SHARED ${OBJECT_SOURCES})
    else()
        message(FATAL_ERROR "HaikuTarget: No SOURCES or OBJECT_LIBRARIES provided for shared library '${NAME}'")
    endif()
    
    # Set output directory with per-config support
    set_target_properties(${TARGET_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${OUTPUT_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${OUTPUT_DIR}"
    )
    
    # Apply properties
    _haiku_apply_target_properties(${TARGET_NAME} ${PARSED_ARGS_PREFIX})
    
    # Handle dependencies
    if(${PARSED_ARGS_PREFIX}_DEPENDS)
        foreach(DEP ${${PARSED_ARGS_PREFIX}_DEPENDS})
            if(TARGET "haiku_${DEP}")
                add_dependencies(${TARGET_NAME} "haiku_${DEP}")
            elseif(TARGET "${DEP}")
                add_dependencies(${TARGET_NAME} "${DEP}")
            endif()
        endforeach()
    endif()
endfunction()

# Build interface libraries for header-only or configuration targets
function(_haiku_build_interface NAME TARGET_NAME PARSED_ARGS_PREFIX)
    add_library(${TARGET_NAME} INTERFACE)
    
    # Apply properties (only INTERFACE properties make sense)
    if(${PARSED_ARGS_PREFIX}_INTERFACE_INCLUDES)
        target_include_directories(${TARGET_NAME} INTERFACE ${${PARSED_ARGS_PREFIX}_INTERFACE_INCLUDES})
    endif()
    
    if(${PARSED_ARGS_PREFIX}_INTERFACE_DEFINITIONS)
        target_compile_definitions(${TARGET_NAME} INTERFACE ${${PARSED_ARGS_PREFIX}_INTERFACE_DEFINITIONS})
    endif()
    
    if(${PARSED_ARGS_PREFIX}_INTERFACE_LINK_LIBRARIES)
        target_link_libraries(${TARGET_NAME} INTERFACE ${${PARSED_ARGS_PREFIX}_INTERFACE_LINK_LIBRARIES})
    endif()
endfunction()

# Universal target builder with single argument parsing
function(haiku_add_target TYPE NAME)
    # Validate environment
    _haiku_validate_environment()
    
    # Single argument parsing point - prevents scope contamination
    set(options NO_DEFAULT_FLAGS VERBOSE INSTALL)
    set(oneValueArgs OUTPUT_NAME VERSION SOVERSION BASE_DIR)
    set(multiValueArgs 
        SOURCES INCLUDES DEFINITIONS FLAGS 
        PUBLIC_INCLUDES PUBLIC_DEFINITIONS PUBLIC_LINK_LIBRARIES
        INTERFACE_INCLUDES INTERFACE_DEFINITIONS INTERFACE_LINK_LIBRARIES
        LINK_LIBRARIES OBJECT_LIBRARIES DEPENDS
    )
    cmake_parse_arguments(HAIKU_TARGET "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    # Check for unparsed arguments
    if(HAIKU_TARGET_UNPARSED_ARGUMENTS)
        message(WARNING "HaikuTarget: Unparsed arguments for ${TYPE} '${NAME}': ${HAIKU_TARGET_UNPARSED_ARGUMENTS}")
    endif()
    
    # Generate unique target names
    set(TARGET_NAME "haiku_${NAME}")
    set(INTERNAL_TARGET_NAME "${TARGET_NAME}_internal")
    
    # Idempotent check
    if(TARGET ${TARGET_NAME})
        if(HAIKU_TARGET_VERBOSE)
            message(STATUS "HaikuTarget: ${TYPE} '${NAME}' already exists, skipping...")
        endif()
        return()
    endif()
    
    # Get output paths
    _haiku_get_output_paths("${TYPE}" "${NAME}" "${HAIKU_TARGET_BASE_DIR}" OUTPUT_DIR OUTPUT_FILE)
    
    message(STATUS "HaikuTarget: Configuring ${TYPE} '${NAME}'...")
    
    # Build target based on type - pass prefix for parsed arguments
    if(TYPE STREQUAL "KIT")
        _haiku_build_kit("${NAME}" "${TARGET_NAME}" "${INTERNAL_TARGET_NAME}" "${OUTPUT_FILE}" "HAIKU_TARGET")
    elseif(TYPE MATCHES "^(EXECUTABLE|APP|TOOL)$")
        _haiku_build_executable("${NAME}" "${TARGET_NAME}" "${OUTPUT_DIR}" "HAIKU_TARGET")
    elseif(TYPE STREQUAL "SHARED_LIBRARY")
        _haiku_build_shared_library("${NAME}" "${TARGET_NAME}" "${OUTPUT_DIR}" "HAIKU_TARGET")
    elseif(TYPE STREQUAL "STATIC_LIBRARY")
        add_library(${TARGET_NAME} STATIC ${HAIKU_TARGET_SOURCES})
        _haiku_apply_target_properties(${TARGET_NAME} "HAIKU_TARGET")
    elseif(TYPE STREQUAL "INTERFACE")
        _haiku_build_interface("${NAME}" "${TARGET_NAME}" "HAIKU_TARGET")
    else()
        message(FATAL_ERROR "HaikuTarget: Unknown type '${TYPE}'. Valid: KIT, EXECUTABLE, APP, SHARED_LIBRARY, STATIC_LIBRARY, TOOL, INTERFACE")
    endif()
    
    # Store target metadata using target properties (no global pollution)
    set_target_properties(${TARGET_NAME} PROPERTIES
        HAIKU_TARGET_TYPE "${TYPE}"
        HAIKU_TARGET_OUTPUT "${OUTPUT_FILE}"
    )
    
    # Install rules if requested
    if(HAIKU_TARGET_INSTALL)
        if(TYPE MATCHES "^(EXECUTABLE|APP|TOOL)$")
            install(TARGETS ${TARGET_NAME} DESTINATION bin)
        elseif(TYPE MATCHES "^(SHARED_LIBRARY|STATIC_LIBRARY)$")
            install(TARGETS ${TARGET_NAME} 
                LIBRARY DESTINATION lib
                ARCHIVE DESTINATION lib
            )
        endif()
    endif()
    
    # Verbose output
    if(HAIKU_TARGET_VERBOSE OR CMAKE_VERBOSE_MAKEFILE)
        message(STATUS "HaikuTarget: ${TYPE} '${NAME}' configured successfully")
        message(STATUS "  CMake Target: ${TARGET_NAME}")
        if(OUTPUT_FILE)
            message(STATUS "  Output: ${OUTPUT_FILE}")
        endif()
        if(HAIKU_TARGET_SOURCES)
            list(LENGTH HAIKU_TARGET_SOURCES SOURCE_COUNT)
            message(STATUS "  Sources: ${SOURCE_COUNT} files")
        endif()
    endif()
endfunction()

# Create convenient alias targets
function(haiku_create_target_alias TARGET_NAME ALIAS_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "HaikuTarget: Target '${TARGET_NAME}' does not exist for alias '${ALIAS_NAME}'")
    endif()
    
    get_target_property(TARGET_TYPE ${TARGET_NAME} TYPE)
    if(TARGET_TYPE STREQUAL "EXECUTABLE")
        add_executable(${ALIAS_NAME} ALIAS ${TARGET_NAME})
    else()
        add_library(${ALIAS_NAME} ALIAS ${TARGET_NAME})
    endif()
    
    message(STATUS "HaikuTarget: Created alias '${ALIAS_NAME}' -> '${TARGET_NAME}'")
endfunction()

# Utility functions using target properties instead of global state
function(haiku_get_targets_by_type TYPE OUTPUT_VAR)
    get_property(ALL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(RESULT_TARGETS "")
    
    foreach(TARGET ${ALL_TARGETS})
        if(TARGET ${TARGET})
            get_target_property(TARGET_TYPE ${TARGET} HAIKU_TARGET_TYPE)
            if(TARGET_TYPE STREQUAL "${TYPE}")
                string(REGEX REPLACE "^haiku_" "" TARGET_NAME "${TARGET}")
                list(APPEND RESULT_TARGETS "${TARGET_NAME}")
            endif()
        endif()
    endforeach()
    
    set(${OUTPUT_VAR} "${RESULT_TARGETS}" PARENT_SCOPE)
endfunction()

function(haiku_get_target_output NAME OUTPUT_VAR)
    set(TARGET_NAME "haiku_${NAME}")
    if(TARGET ${TARGET_NAME})
        get_target_property(OUTPUT_FILE ${TARGET_NAME} HAIKU_TARGET_OUTPUT)
        set(${OUTPUT_VAR} "${OUTPUT_FILE}" PARENT_SCOPE)
    else()
        set(${OUTPUT_VAR} "" PARENT_SCOPE)
    endif()
endfunction()

function(haiku_target_exists NAME RESULT_VAR)
    if(TARGET "haiku_${NAME}")
        set(${RESULT_VAR} TRUE PARENT_SCOPE)
    else()
        set(${RESULT_VAR} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(haiku_print_targets_summary)
    get_property(ALL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(HAIKU_TARGETS "")
    
    foreach(TARGET ${ALL_TARGETS})
        if(TARGET ${TARGET})
            get_target_property(TARGET_TYPE ${TARGET} HAIKU_TARGET_TYPE)
            if(TARGET_TYPE)
                list(APPEND HAIKU_TARGETS ${TARGET})
            endif()
        endif()
    endforeach()
    
    if(NOT HAIKU_TARGETS)
        message(STATUS "HaikuTarget: No targets built")
        return()
    endif()
    
    list(LENGTH HAIKU_TARGETS TARGET_COUNT)
    message(STATUS "HaikuTarget: Built ${TARGET_COUNT} targets:")
    
    foreach(TARGET ${HAIKU_TARGETS})
        get_target_property(TARGET_TYPE ${TARGET} HAIKU_TARGET_TYPE)
        get_target_property(TARGET_OUTPUT ${TARGET} HAIKU_TARGET_OUTPUT)
        string(REGEX REPLACE "^haiku_" "" TARGET_NAME "${TARGET}")
        message(STATUS "  ${TARGET_TYPE}: ${TARGET_NAME} -> ${TARGET_OUTPUT}")
    endforeach()
endfunction()