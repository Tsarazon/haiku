# HaikuHostTools.cmake
# Functions for building Haiku host tools that run during build process
# These tools typically compile and run on the build machine, not the target

cmake_minimum_required(VERSION 3.16)

# Function: haiku_host_tool
# Creates a tool that runs on the build host (not cross-compiled)
# Usage: haiku_host_tool(TARGET_NAME SOURCES source1.cpp LIBRARIES lib1)
function(haiku_host_tool TARGET_NAME)
    cmake_parse_arguments(HAIKU_HOST
        "INSTALL_IN_TOOLS" # Options
        "" # Single value args
        "SOURCES;LIBRARIES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_HOST_SOURCES)
        message(FATAL_ERROR "haiku_host_tool: SOURCES required for ${TARGET_NAME}")
    endif()
    
    # Save current compiler settings
    set(SAVED_C_COMPILER ${CMAKE_C_COMPILER})
    set(SAVED_CXX_COMPILER ${CMAKE_CXX_COMPILER})
    set(SAVED_SYSROOT ${CMAKE_SYSROOT})
    
    # Temporarily use host compiler for this tool
    set(CMAKE_C_COMPILER "/usr/bin/cc")
    set(CMAKE_CXX_COMPILER "/usr/bin/c++")
    set(CMAKE_SYSROOT "")
    
    # Create the executable using host compiler
    add_executable(${TARGET_NAME} ${HAIKU_HOST_SOURCES})
    
    # Set properties for host tools - use architecture-aware location
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${HAIKU_CMAKE_OUTPUT_DIR}/host_tools"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    
    # Use host compiler flags (not cross-compiler flags)
    target_compile_definitions(${TARGET_NAME} PRIVATE 
        _GNU_SOURCE
    )
    
    # Link with host libraries
    if(HAIKU_HOST_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${HAIKU_HOST_LIBRARIES})
    endif()
    
    # Optionally install in tools directory (for use by build system)
    if(HAIKU_HOST_INSTALL_IN_TOOLS)
        install(TARGETS ${TARGET_NAME} 
            RUNTIME DESTINATION tools
        )
    endif()
    
    # Restore compiler settings
    set(CMAKE_C_COMPILER ${SAVED_C_COMPILER})
    set(CMAKE_CXX_COMPILER ${SAVED_CXX_COMPILER}) 
    set(CMAKE_SYSROOT ${SAVED_SYSROOT})
    
    message(STATUS "Created Haiku host tool: ${TARGET_NAME}")
endfunction()

# Function: haiku_build_tool
# Creates a tool needed for the build process (synonym for haiku_host_tool)
# Usage: haiku_build_tool(TARGET_NAME SOURCES source1.cpp)
function(haiku_build_tool TARGET_NAME)
    haiku_host_tool(${TARGET_NAME} ${ARGN} INSTALL_IN_TOOLS)
endfunction()

# Function: haiku_resource_tool
# Creates a tool for resource processing (runs on host)
# Usage: haiku_resource_tool(TARGET_NAME SOURCES source1.cpp)
function(haiku_resource_tool TARGET_NAME)
    cmake_parse_arguments(HAIKU_RESTOOL
        "" # Options
        "" # Single value args
        "SOURCES;LIBRARIES" # Multi-value args
        ${ARGN}
    )
    
    # Create as host tool
    haiku_host_tool(${TARGET_NAME} 
        SOURCES ${HAIKU_RESTOOL_SOURCES}
        LIBRARIES ${HAIKU_RESTOOL_LIBRARIES}
        INSTALL_IN_TOOLS
    )
    
    # Add to resource tools list (for dependency tracking)
    set_property(GLOBAL APPEND PROPERTY HAIKU_RESOURCE_TOOLS ${TARGET_NAME})
    
    message(STATUS "Created Haiku resource tool: ${TARGET_NAME}")
endfunction()

# Function: haiku_code_generator
# Creates a tool that generates source code during build
# Usage: haiku_code_generator(TARGET_NAME SOURCES source1.cpp OUTPUT_DIR dir)
function(haiku_code_generator TARGET_NAME)
    cmake_parse_arguments(HAIKU_CODEGEN
        "" # Options
        "OUTPUT_DIR" # Single value args
        "SOURCES;LIBRARIES" # Multi-value args
        ${ARGN}
    )
    
    # Create as host tool
    haiku_host_tool(${TARGET_NAME}
        SOURCES ${HAIKU_CODEGEN_SOURCES}
        LIBRARIES ${HAIKU_CODEGEN_LIBRARIES}
    )
    
    # Set output directory for generated files
    if(HAIKU_CODEGEN_OUTPUT_DIR)
        set_target_properties(${TARGET_NAME} PROPERTIES
            CODEGEN_OUTPUT_DIR ${HAIKU_CODEGEN_OUTPUT_DIR}
        )
    endif()
    
    message(STATUS "Created code generator: ${TARGET_NAME}")
endfunction()

# Function: haiku_jam_compatibility_tool
# Creates a tool that provides compatibility with Jam build system
# Usage: haiku_jam_compatibility_tool(TARGET_NAME SOURCES source1.cpp JAM_EQUIVALENT jam_target)
function(haiku_jam_compatibility_tool TARGET_NAME)
    cmake_parse_arguments(HAIKU_JAM_COMPAT
        "" # Options
        "JAM_EQUIVALENT" # Single value args
        "SOURCES;LIBRARIES" # Multi-value args
        ${ARGN}
    )
    
    # Create the tool
    haiku_host_tool(${TARGET_NAME}
        SOURCES ${HAIKU_JAM_COMPAT_SOURCES}
        LIBRARIES ${HAIKU_JAM_COMPAT_LIBRARIES}
    )
    
    # Add information about Jam equivalent
    if(HAIKU_JAM_COMPAT_JAM_EQUIVALENT)
        set_target_properties(${TARGET_NAME} PROPERTIES
            JAM_EQUIVALENT ${HAIKU_JAM_COMPAT_JAM_EQUIVALENT}
        )
        
        message(STATUS "Created Jam-compatible tool: ${TARGET_NAME} (equivalent to ${HAIKU_JAM_COMPAT_JAM_EQUIVALENT})")
    endif()
endfunction()

# Function: find_host_tools
# Finds existing host tools in the system or build directory
# Usage: find_host_tools()
function(find_host_tools)
    # Common Haiku host tools
    set(HOST_TOOLS
        rc          # Resource compiler
        xres        # Extended resource tool
        collectcatkeys # Localization key collector
        catattr     # Catalog attribute tool
        addattr     # Add attributes tool
        rmattr      # Remove attributes tool
        data_to_source # Data to C source converter
        elf2aout    # ELF to AOUT converter
    )
    
    foreach(TOOL ${HOST_TOOLS})
        # First check build directory
        find_program(${TOOL}_TOOL 
            NAMES ${TOOL}
            PATHS 
                ${HAIKU_CMAKE_OUTPUT_DIR}/host_tools
                ${HAIKU_TOOLS_DIR}
                ${HAIKU_TOP}/generated/objects/haiku/host/release/tools
            NO_DEFAULT_PATH
        )
        
        # Then check system paths
        if(NOT ${TOOL}_TOOL)
            find_program(${TOOL}_TOOL ${TOOL})
        endif()
        
        if(${TOOL}_TOOL)
            message(STATUS "Found host tool: ${TOOL} at ${${TOOL}_TOOL}")
            # Make available globally
            set(HAIKU_${TOOL}_TOOL ${${TOOL}_TOOL} PARENT_SCOPE)
        else()
            message(STATUS "Host tool not found: ${TOOL}")
        endif()
    endforeach()
endfunction()

# Function: setup_host_build_environment
# Sets up environment for building host tools
# Usage: setup_host_build_environment()
function(setup_host_build_environment)
    message(STATUS "Setting up host build environment")
    
    # Ensure we have host compilers
    find_program(HOST_CC NAMES gcc clang cc)
    find_program(HOST_CXX NAMES g++ clang++ c++)
    
    if(NOT HOST_CC OR NOT HOST_CXX)
        message(FATAL_ERROR "Host compilers not found. Install gcc/g++ or clang/clang++")
    endif()
    
    message(STATUS "Host C compiler: ${HOST_CC}")
    message(STATUS "Host C++ compiler: ${HOST_CXX}")
    
    # Set up host build directory  
    set(HOST_BUILD_DIR "${HAIKU_CMAKE_OUTPUT_DIR}/host_build")
    file(MAKE_DIRECTORY ${HOST_BUILD_DIR})
    
    # Find existing host tools
    find_host_tools()
    
    message(STATUS "Host build environment ready")
endfunction()

# Automatic setup when module is loaded
setup_host_build_environment()

message(STATUS "Loaded HaikuHostTools.cmake module")