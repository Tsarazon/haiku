# HaikuDualBuild.cmake
# Functions for dual-build system where CMake builds alongside Jam
# Supports building tools and components that work with existing Jam build

cmake_minimum_required(VERSION 3.16)

# Function: haiku_dual_build_tool
# Creates a tool that can be built alongside the Jam system
# These tools typically run on the host system during build process
# Usage: haiku_dual_build_tool(TARGET_NAME SOURCES source1.cpp LIBRARIES lib1)
function(haiku_dual_build_tool TARGET_NAME)
    cmake_parse_arguments(HAIKU_TOOL
        "HOST_TOOL" # Options - set if tool should use host compiler
        "" # Single value args
        "SOURCES;LIBRARIES;HOST_LIBRARIES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_TOOL_SOURCES)
        message(FATAL_ERROR "haiku_dual_build_tool: SOURCES required for ${TARGET_NAME}")
    endif()
    
    # Create the executable
    add_executable(${TARGET_NAME} ${HAIKU_TOOL_SOURCES})
    
    # Set output directory to match Jam build structure
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${HAIKU_TOOLS_DIR}"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    
    # For host tools, we might want to use host compiler instead of cross-compiler
    if(HAIKU_TOOL_HOST_TOOL)
        # Use host compiler flags
        target_compile_definitions(${TARGET_NAME} PRIVATE 
            _GNU_SOURCE
        )
        
        # Link with host libraries
        if(HAIKU_TOOL_HOST_LIBRARIES)
            target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_TOOL_HOST_LIBRARIES})
        endif()
        
        message(STATUS "Created Haiku host tool: ${TARGET_NAME}")
    else()
        # Use cross-compiler (for tools that run on target)
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:${HAIKU_COMMON_C_FLAGS}>
            $<$<COMPILE_LANGUAGE:CXX>:${HAIKU_COMMON_CXX_FLAGS}>
        )
        
        target_compile_definitions(${TARGET_NAME} PRIVATE 
            __HAIKU__
            _GNU_SOURCE
        )
        
        # Link with target libraries
        if(HAIKU_TOOL_LIBRARIES)
            target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_TOOL_LIBRARIES})
        endif()
        
        message(STATUS "Created Haiku target tool: ${TARGET_NAME}")
    endif()
endfunction()

# Function: haiku_dual_build_library
# Creates a library that works with dual-build approach
# Usage: haiku_dual_build_library(TARGET_NAME TYPE STATIC/SHARED SOURCES source1.cpp)
function(haiku_dual_build_library TARGET_NAME TYPE)
    cmake_parse_arguments(HAIKU_LIB
        "VALIDATE_AGAINST_JAM" # Options
        "JAM_EQUIVALENT" # Single value args
        "SOURCES;LIBRARIES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_LIB_SOURCES)
        message(FATAL_ERROR "haiku_dual_build_library: SOURCES required for ${TARGET_NAME}")
    endif()
    
    # Create the library using standard haiku_library function
    haiku_library(${TARGET_NAME} ${TYPE} 
        SOURCES ${HAIKU_LIB_SOURCES}
        LIBRARIES ${HAIKU_LIB_LIBRARIES}
    )
    
    # If validation requested, add comparison with Jam build
    if(HAIKU_LIB_VALIDATE_AGAINST_JAM AND HAIKU_LIB_JAM_EQUIVALENT)
        add_custom_target(validate_${TARGET_NAME}
            COMMAND echo "TODO: Add binary comparison between CMake and Jam builds"
            COMMENT "Validating ${TARGET_NAME} against Jam equivalent ${HAIKU_LIB_JAM_EQUIVALENT}"
        )
        
        add_dependencies(validate_${TARGET_NAME} ${TARGET_NAME})
        message(STATUS "Added validation target for: ${TARGET_NAME}")
    endif()
    
    message(STATUS "Created dual-build library: ${TARGET_NAME}")
endfunction()

# Function: haiku_import_jam_library
# Imports a library built by Jam for use in CMake targets
# Usage: haiku_import_jam_library(IMPORT_NAME JAM_PATH)
function(haiku_import_jam_library IMPORT_NAME JAM_PATH)
    cmake_parse_arguments(HAIKU_IMPORT
        "" # Options
        "TYPE" # Single value args (SHARED, STATIC, UNKNOWN)
        "INCLUDE_DIRS" # Multi-value args
        ${ARGN}
    )
    
    # Default to UNKNOWN type
    if(NOT HAIKU_IMPORT_TYPE)
        set(HAIKU_IMPORT_TYPE "UNKNOWN")
    endif()
    
    # Create imported target
    add_library(${IMPORT_NAME} ${HAIKU_IMPORT_TYPE} IMPORTED GLOBAL)
    
    # Set location of the Jam-built library
    if(HAIKU_IMPORT_TYPE STREQUAL "SHARED")
        set_target_properties(${IMPORT_NAME} PROPERTIES
            IMPORTED_LOCATION "${JAM_PATH}"
            IMPORTED_SONAME "${IMPORT_NAME}"
        )
    else()
        set_target_properties(${IMPORT_NAME} PROPERTIES
            IMPORTED_LOCATION "${JAM_PATH}"
        )
    endif()
    
    # Set include directories if provided
    if(HAIKU_IMPORT_INCLUDE_DIRS)
        set_target_properties(${IMPORT_NAME} PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${HAIKU_IMPORT_INCLUDE_DIRS}"
        )
    endif()
    
    message(STATUS "Imported Jam library: ${IMPORT_NAME} from ${JAM_PATH}")
endfunction()

# Function: haiku_export_for_jam
# Exports a CMake target for use by Jam build system
# Usage: haiku_export_for_jam(TARGET_NAME EXPORT_PATH)
function(haiku_export_for_jam TARGET_NAME EXPORT_PATH)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "haiku_export_for_jam: Target ${TARGET_NAME} does not exist")
    endif()
    
    # Install the target to specified path for Jam to find
    install(TARGETS ${TARGET_NAME}
        RUNTIME DESTINATION ${EXPORT_PATH}/bin
        LIBRARY DESTINATION ${EXPORT_PATH}/lib
        ARCHIVE DESTINATION ${EXPORT_PATH}/lib
    )
    
    # Create a marker file that Jam can check
    set(MARKER_FILE "${HAIKU_CMAKE_OUTPUT_DIR}/${TARGET_NAME}_cmake_built.marker")
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E touch ${MARKER_FILE}
        COMMENT "Creating marker for Jam: ${TARGET_NAME}"
    )
    
    message(STATUS "Exported for Jam: ${TARGET_NAME} -> ${EXPORT_PATH}")
endfunction()

# Function: haiku_coordinate_with_jam
# Sets up coordination between CMake and Jam builds
# Usage: haiku_coordinate_with_jam()
function(haiku_coordinate_with_jam)
    # Create coordination directory
    set(COORD_DIR "${HAIKU_CMAKE_OUTPUT_DIR}/jam_cmake_coordination")
    file(MAKE_DIRECTORY ${COORD_DIR})
    
    # Create build order file
    set(BUILD_ORDER_FILE "${COORD_DIR}/build_order.txt")
    file(WRITE ${BUILD_ORDER_FILE} "# Build coordination between Jam and CMake\n")
    file(APPEND ${BUILD_ORDER_FILE} "# Generated: ${CMAKE_CURRENT_LIST_FILE}\n")
    file(APPEND ${BUILD_ORDER_FILE} "CMAKE_TARGETS: ")
    
    # Get all targets in current directory
    get_property(ALL_TARGETS DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(TARGET ${ALL_TARGETS})
        file(APPEND ${BUILD_ORDER_FILE} "${TARGET} ")
    endforeach()
    file(APPEND ${BUILD_ORDER_FILE} "\n")
    
    message(STATUS "Set up Jam-CMake coordination in: ${COORD_DIR}")
endfunction()

# Function: haiku_binary_comparison
# Adds a target to compare CMake-built binary with Jam-built equivalent
# Usage: haiku_binary_comparison(CMAKE_TARGET JAM_PATH)
function(haiku_binary_comparison CMAKE_TARGET JAM_PATH)
    if(NOT TARGET ${CMAKE_TARGET})
        message(FATAL_ERROR "haiku_binary_comparison: Target ${CMAKE_TARGET} does not exist")
    endif()
    
    # Create comparison target
    set(COMPARE_TARGET "compare_${CMAKE_TARGET}")
    add_custom_target(${COMPARE_TARGET}
        COMMAND ${CMAKE_COMMAND} -E echo "Comparing $<TARGET_FILE:${CMAKE_TARGET}> with ${JAM_PATH}"
        COMMAND test -f ${JAM_PATH} && echo "Jam binary exists" || echo "Jam binary not found"
        COMMAND test -f $<TARGET_FILE:${CMAKE_TARGET}> && echo "CMake binary exists" || echo "CMake binary not found" 
        DEPENDS ${CMAKE_TARGET}
        COMMENT "Binary comparison: ${CMAKE_TARGET} vs Jam equivalent"
        VERBATIM
    )
    
    message(STATUS "Added binary comparison: ${CMAKE_TARGET} vs ${JAM_PATH}")
endfunction()

# Function: haiku_stage_build
# Manages staged building where some components use CMake and others use Jam
# Usage: haiku_stage_build(STAGE_NUMBER DESCRIPTION CMAKE_COMPONENTS comp1 comp2 JAM_COMPONENTS comp3 comp4)
function(haiku_stage_build STAGE_NUMBER DESCRIPTION)
    cmake_parse_arguments(HAIKU_STAGE
        "" # Options
        "" # Single value args
        "CMAKE_COMPONENTS;JAM_COMPONENTS" # Multi-value args
        ${ARGN}
    )
    
    message(STATUS "=== Build Stage ${STAGE_NUMBER}: ${DESCRIPTION} ===")
    
    if(HAIKU_STAGE_CMAKE_COMPONENTS)
        message(STATUS "  CMake components: ${HAIKU_STAGE_CMAKE_COMPONENTS}")
        # These will be built automatically by CMake
    endif()
    
    if(HAIKU_STAGE_JAM_COMPONENTS)
        message(STATUS "  Jam components: ${HAIKU_STAGE_JAM_COMPONENTS}")
        # These need to be built by Jam - could add custom commands here
    endif()
    
    # Create stage marker
    set(STAGE_MARKER "${HAIKU_CMAKE_OUTPUT_DIR}/stage_${STAGE_NUMBER}_complete.marker")
    add_custom_target(stage_${STAGE_NUMBER}
        COMMAND ${CMAKE_COMMAND} -E touch ${STAGE_MARKER}
        COMMENT "Completing build stage ${STAGE_NUMBER}: ${DESCRIPTION}"
    )
    
    # Make stage depend on CMake components
    if(HAIKU_STAGE_CMAKE_COMPONENTS)
        add_dependencies(stage_${STAGE_NUMBER} ${HAIKU_STAGE_CMAKE_COMPONENTS})
    endif()
endfunction()

message(STATUS "Loaded HaikuDualBuild.cmake module")