# HaikuResources.cmake  
# Resource handling functions for Haiku OS build system
# Provides functions for .rdef compilation, resource embedding, and localization

cmake_minimum_required(VERSION 3.16)

# Find the resource compiler (rc)
find_program(HAIKU_RC_TOOL 
    NAMES rc
    PATHS 
        ${HAIKU_TOOLS_DIR}
        ${HAIKU_TOP}/generated/objects/haiku/${HAIKU_ARCH}/release/tools
        ${HAIKU_TOP}/objects/haiku/${HAIKU_ARCH}/release/tools
    NO_DEFAULT_PATH
)

# If not found in build directories, try system paths for host build
if(NOT HAIKU_RC_TOOL)
    find_program(HAIKU_RC_TOOL rc)
endif()

# Check for xres (extended resource tool)
find_program(HAIKU_XRES_TOOL 
    NAMES xres
    PATHS 
        ${HAIKU_TOOLS_DIR}
        ${HAIKU_TOP}/generated/objects/haiku/${HAIKU_ARCH}/release/tools
        ${HAIKU_TOP}/objects/haiku/${HAIKU_ARCH}/release/tools
    NO_DEFAULT_PATH
)

if(NOT HAIKU_XRES_TOOL)
    find_program(HAIKU_XRES_TOOL xres)
endif()

# Set up resource include paths
set(HAIKU_RESOURCE_INCLUDES
    ${HAIKU_TOP}/headers/os
    ${HAIKU_TOP}/headers/posix  
    ${HAIKU_TOP}/data/artwork
    ${HAIKU_TOP}/data/artwork/icons
)

# Function: haiku_add_resource_def
# Compiles .rdef resource definition files (equivalent to Jam's AddResources)
# Usage: haiku_add_resource_def(TARGET_NAME RDEF_FILES file1.rdef file2.rdef)
function(haiku_add_resource_def TARGET_NAME)
    cmake_parse_arguments(HAIKU_RDEF
        "" # Options
        "" # Single value args
        "RDEF_FILES;INCLUDE_DIRS" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_RDEF_RDEF_FILES)
        message(FATAL_ERROR "haiku_add_resource_def: RDEF_FILES required")
    endif()
    
    if(NOT HAIKU_RC_TOOL)
        message(WARNING "haiku_add_resource_def: rc tool not found, skipping resource compilation for ${TARGET_NAME}")
        return()
    endif()
    
    # Set up include directories
    set(RC_INCLUDE_DIRS ${HAIKU_RESOURCE_INCLUDES})
    if(HAIKU_RDEF_INCLUDE_DIRS)
        list(APPEND RC_INCLUDE_DIRS ${HAIKU_RDEF_INCLUDE_DIRS})
    endif()
    
    # Build include flags
    set(RC_INCLUDE_FLAGS)
    foreach(INCLUDE_DIR ${RC_INCLUDE_DIRS})
        list(APPEND RC_INCLUDE_FLAGS "-I" "${INCLUDE_DIR}")
    endforeach()
    
    # Process each .rdef file
    set(RESOURCE_FILES)
    foreach(RDEF_FILE ${HAIKU_RDEF_RDEF_FILES})
        # Get absolute path
        get_filename_component(RDEF_ABS_PATH ${RDEF_FILE} ABSOLUTE)
        
        # Generate output .rsrc file name in architecture-aware location
        get_filename_component(RDEF_NAME ${RDEF_FILE} NAME_WE)
        set(RSRC_FILE "${HAIKU_ARCH_OBJECTS_DIR}/resources/${RDEF_NAME}.rsrc")
        file(MAKE_DIRECTORY "${HAIKU_ARCH_OBJECTS_DIR}/resources")
        
        # Add custom command to compile .rdef to .rsrc
        add_custom_command(
            OUTPUT ${RSRC_FILE}
            COMMAND ${HAIKU_RC_TOOL} ${RC_INCLUDE_FLAGS} -o ${RSRC_FILE} ${RDEF_ABS_PATH}
            DEPENDS ${RDEF_ABS_PATH}
            COMMENT "Compiling resource definition: ${RDEF_FILE}"
            VERBATIM
        )
        
        list(APPEND RESOURCE_FILES ${RSRC_FILE})
    endforeach()
    
    # Create a custom target for the resources
    set(RESOURCE_TARGET "${TARGET_NAME}_resources")
    add_custom_target(${RESOURCE_TARGET} DEPENDS ${RESOURCE_FILES})
    
    # Make the main target depend on the resources
    if(TARGET ${TARGET_NAME})
        add_dependencies(${TARGET_NAME} ${RESOURCE_TARGET})
        
        # If xres is available, embed resources into the binary
        if(HAIKU_XRES_TOOL)
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${HAIKU_XRES_TOOL} -o $<TARGET_FILE:${TARGET_NAME}> ${RESOURCE_FILES}
                COMMENT "Embedding resources into ${TARGET_NAME}"
                VERBATIM
            )
        endif()
    endif()
    
    message(STATUS "Added resource definitions for: ${TARGET_NAME}")
endfunction()

# Function: haiku_compile_rdef
# Low-level function to compile a single .rdef file
# Usage: haiku_compile_rdef(OUTPUT_VAR input.rdef)
function(haiku_compile_rdef OUTPUT_VAR RDEF_FILE)
    cmake_parse_arguments(HAIKU_COMPILE
        "" # Options
        "" # Single value args
        "INCLUDE_DIRS;DEFINES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_RC_TOOL)
        message(FATAL_ERROR "haiku_compile_rdef: rc tool not found")
    endif()
    
    # Build command line
    set(RC_INCLUDES ${HAIKU_RESOURCE_INCLUDES})
    if(HAIKU_COMPILE_INCLUDE_DIRS)
        list(APPEND RC_INCLUDES ${HAIKU_COMPILE_INCLUDE_DIRS})
    endif()
    
    set(RC_FLAGS)
    foreach(INCLUDE_DIR ${RC_INCLUDES})
        list(APPEND RC_FLAGS "-I" "${INCLUDE_DIR}")
    endforeach()
    
    # Add defines
    if(HAIKU_COMPILE_DEFINES)
        foreach(DEFINE ${HAIKU_COMPILE_DEFINES})
            list(APPEND RC_FLAGS "-D${DEFINE}")
        endforeach()
    endif()
    
    # Generate output file name in architecture-aware location
    get_filename_component(RDEF_NAME ${RDEF_FILE} NAME_WE)
    set(RSRC_FILE "${HAIKU_ARCH_OBJECTS_DIR}/resources/${RDEF_NAME}.rsrc")
    file(MAKE_DIRECTORY "${HAIKU_ARCH_OBJECTS_DIR}/resources")
    
    # Create custom command
    add_custom_command(
        OUTPUT ${RSRC_FILE}
        COMMAND ${HAIKU_RC_TOOL} ${RC_FLAGS} -o ${RSRC_FILE} ${RDEF_FILE}
        DEPENDS ${RDEF_FILE}
        COMMENT "Compiling: ${RDEF_FILE} -> ${RSRC_FILE}"
        VERBATIM
    )
    
    # Return the output file
    set(${OUTPUT_VAR} ${RSRC_FILE} PARENT_SCOPE)
endfunction()

# Function: haiku_add_resource_file
# Adds a binary resource file (equivalent to Jam's AddFileDataResource)
# Usage: haiku_add_resource_file(TARGET_NAME FILE_PATH RESOURCE_ID RESOURCE_NAME)
function(haiku_add_resource_file TARGET_NAME FILE_PATH RESOURCE_ID RESOURCE_NAME)
    if(NOT HAIKU_XRES_TOOL)
        message(WARNING "haiku_add_resource_file: xres tool not found, skipping ${FILE_PATH}")
        return()
    endif()
    
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "haiku_add_resource_file: Target ${TARGET_NAME} does not exist")
    endif()
    
    # Add post-build command to embed the file
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${HAIKU_XRES_TOOL} -o $<TARGET_FILE:${TARGET_NAME}> -a ${FILE_PATH} ${RESOURCE_ID} ${RESOURCE_NAME}
        DEPENDS ${FILE_PATH}
        COMMENT "Adding resource file: ${FILE_PATH} as ${RESOURCE_NAME}"
        VERBATIM
    )
    
    message(STATUS "Added resource file: ${FILE_PATH} to ${TARGET_NAME}")
endfunction()

# Function: haiku_add_hvif_icon
# Adds a Haiku Vector Icon File (HVIF) as a resource
# Usage: haiku_add_hvif_icon(TARGET_NAME ICON_FILE)
function(haiku_add_hvif_icon TARGET_NAME ICON_FILE)
    # HVIF files are added as vector icon resources
    haiku_add_resource_file(${TARGET_NAME} ${ICON_FILE} 101 "BEOS:ICON")
endfunction()

# Function: haiku_set_mime_signature  
# Sets the MIME signature for an application (equivalent to setting app signature in .rdef)
# Usage: haiku_set_mime_signature(TARGET_NAME "application/x-vnd.Haiku-AppName")
function(haiku_set_mime_signature TARGET_NAME SIGNATURE)
    if(NOT HAIKU_XRES_TOOL)
        message(WARNING "haiku_set_mime_signature: xres tool not found")
        return()
    endif()
    
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "haiku_set_mime_signature: Target ${TARGET_NAME} does not exist")
    endif()
    
    # Create a temporary file with the signature in architecture-aware location
    set(SIG_FILE "${HAIKU_ARCH_OBJECTS_DIR}/resources/${TARGET_NAME}_signature.txt")
    file(MAKE_DIRECTORY "${HAIKU_ARCH_OBJECTS_DIR}/resources")
    file(WRITE ${SIG_FILE} ${SIGNATURE})
    
    # Add the signature as a resource attribute
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${HAIKU_XRES_TOOL} -o $<TARGET_FILE:${TARGET_NAME}> -a ${SIG_FILE} 'MIMS' "BEOS:APP_SIG"
        DEPENDS ${SIG_FILE}
        COMMENT "Setting MIME signature: ${SIGNATURE}"
        VERBATIM
    )
endfunction()

# Function: haiku_add_version_info
# Adds version information to a binary (equivalent to Jam's version info)
# Usage: haiku_add_version_info(TARGET_NAME MAJOR MINOR MICRO VARIETY BUILD_NUMBER SHORT_INFO LONG_INFO)
function(haiku_add_version_info TARGET_NAME MAJOR MINOR MICRO)
    cmake_parse_arguments(HAIKU_VER
        "" # Options
        "VARIETY;BUILD_NUMBER" # Single value args
        "SHORT_INFO;LONG_INFO" # Multi-value args (could be lists)
        ${ARGN}
    )
    
    # Set defaults
    if(NOT HAIKU_VER_VARIETY)
        set(HAIKU_VER_VARIETY "B_APPV_FINAL")
    endif()
    
    if(NOT HAIKU_VER_BUILD_NUMBER)
        set(HAIKU_VER_BUILD_NUMBER 1)
    endif()
    
    # Create version info resource definition in architecture-aware location
    set(VERSION_RDEF "${HAIKU_ARCH_OBJECTS_DIR}/resources/${TARGET_NAME}_version.rdef")
    file(MAKE_DIRECTORY "${HAIKU_ARCH_OBJECTS_DIR}/resources")
    
    file(WRITE ${VERSION_RDEF} "
#include <AppDefs.h>

resource app_version {
    major  = ${MAJOR},
    middle = ${MINOR},
    minor  = ${MICRO},
    variety = ${HAIKU_VER_VARIETY},
    internal = ${HAIKU_VER_BUILD_NUMBER}
")
    
    if(HAIKU_VER_SHORT_INFO)
        file(APPEND ${VERSION_RDEF} ",\n    short_info = \"${HAIKU_VER_SHORT_INFO}\"")
    endif()
    
    if(HAIKU_VER_LONG_INFO) 
        file(APPEND ${VERSION_RDEF} ",\n    long_info = \"${HAIKU_VER_LONG_INFO}\"")
    endif()
    
    file(APPEND ${VERSION_RDEF} "\n};\n")
    
    # Compile and add the version resource
    haiku_add_resource_def(${TARGET_NAME} RDEF_FILES ${VERSION_RDEF})
endfunction()

# Function: haiku_add_localization_catalog
# Adds localization catalog support (equivalent to Jam's DoCatalogs)
# Usage: haiku_add_localization_catalog(TARGET_NAME SIGNATURE SOURCES source1.cpp source2.cpp)
function(haiku_add_localization_catalog TARGET_NAME SIGNATURE)
    cmake_parse_arguments(HAIKU_LOCALE
        "" # Options
        "" # Single value args
        "SOURCES;LANGUAGES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_LOCALE_SOURCES)
        message(WARNING "haiku_add_localization_catalog: No sources specified for ${TARGET_NAME}")
        return()
    endif()
    
    # Default to English if no languages specified
    if(NOT HAIKU_LOCALE_LANGUAGES)
        set(HAIKU_LOCALE_LANGUAGES "en")
    endif()
    
    # Find collectcatkeys tool
    find_program(COLLECTCATKEYS_TOOL 
        NAMES collectcatkeys
        PATHS ${HAIKU_TOP}/generated/objects/haiku/${HAIKU_ARCH}/release/tools/collectcatkeys
    )
    
    if(NOT COLLECTCATKEYS_TOOL)
        message(WARNING "collectcatkeys tool not found, skipping localization for ${TARGET_NAME}")
        return()
    endif()
    
    # Generate .catkeys file in architecture-aware location  
    set(CATKEYS_FILE "${HAIKU_ARCH_OBJECTS_DIR}/catalogs/${TARGET_NAME}.catkeys")
    file(MAKE_DIRECTORY "${HAIKU_ARCH_OBJECTS_DIR}/catalogs")
    
    add_custom_command(
        OUTPUT ${CATKEYS_FILE}
        COMMAND ${COLLECTCATKEYS_TOOL} -o ${CATKEYS_FILE} ${HAIKU_LOCALE_SOURCES}
        DEPENDS ${HAIKU_LOCALE_SOURCES}
        COMMENT "Collecting catalog keys for ${TARGET_NAME}"
        VERBATIM
    )
    
    # Create catalog target
    set(CATALOG_TARGET "${TARGET_NAME}_catalog")
    add_custom_target(${CATALOG_TARGET} DEPENDS ${CATKEYS_FILE})
    
    # Make main target depend on catalog
    if(TARGET ${TARGET_NAME})
        add_dependencies(${TARGET_NAME} ${CATALOG_TARGET})
    endif()
    
    message(STATUS "Added localization catalog for: ${TARGET_NAME}")
endfunction()

# Function: haiku_add_file_type_icon
# Associates a file type with an icon
# Usage: haiku_add_file_type_icon(MIME_TYPE ICON_FILE)
function(haiku_add_file_type_icon MIME_TYPE ICON_FILE)
    # This would typically be handled by the package system
    # For now, just report what we would do
    message(STATUS "Would associate ${MIME_TYPE} with icon ${ICON_FILE}")
endfunction()

# Function: haiku_merge_resources
# Merges multiple resource files into one
# Usage: haiku_merge_resources(OUTPUT_FILE INPUT_FILES file1.rsrc file2.rsrc)
function(haiku_merge_resources OUTPUT_FILE)
    cmake_parse_arguments(HAIKU_MERGE
        "" # Options
        "" # Single value args
        "INPUT_FILES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_MERGE_INPUT_FILES)
        message(FATAL_ERROR "haiku_merge_resources: INPUT_FILES required")
    endif()
    
    if(NOT HAIKU_XRES_TOOL)
        message(FATAL_ERROR "haiku_merge_resources: xres tool not found")
    endif()
    
    # Create merge command
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${HAIKU_XRES_TOOL} -o ${OUTPUT_FILE} ${HAIKU_MERGE_INPUT_FILES}
        DEPENDS ${HAIKU_MERGE_INPUT_FILES}
        COMMENT "Merging resources into: ${OUTPUT_FILE}"
        VERBATIM
    )
endfunction()

# Function: haiku_set_attributes
# Sets extended attributes on a file (Haiku-specific feature)
# Usage: haiku_set_attributes(TARGET_FILE ATTRIBUTES attr1=value1 attr2=value2)
function(haiku_set_attributes TARGET_FILE)
    cmake_parse_arguments(HAIKU_ATTRS
        "" # Options
        "" # Single value args
        "ATTRIBUTES" # Multi-value args
        ${ARGN}
    )
    
    if(NOT HAIKU_ATTRS_ATTRIBUTES)
        message(WARNING "haiku_set_attributes: No attributes specified for ${TARGET_FILE}")
        return()
    endif()
    
    # Find addattr tool
    find_program(ADDATTR_TOOL addattr)
    
    if(NOT ADDATTR_TOOL)
        message(WARNING "addattr tool not found, cannot set attributes on ${TARGET_FILE}")
        return()
    endif()
    
    # Process each attribute
    foreach(ATTR ${HAIKU_ATTRS_ATTRIBUTES})
        # Parse name=value
        string(REGEX MATCH "^([^=]+)=(.*)$" MATCH_RESULT ${ATTR})
        if(MATCH_RESULT)
            set(ATTR_NAME ${CMAKE_MATCH_1})
            set(ATTR_VALUE ${CMAKE_MATCH_2})
            
            # Add command to set the attribute
            add_custom_command(TARGET ${TARGET_FILE} POST_BUILD
                COMMAND ${ADDATTR_TOOL} ${ATTR_NAME} ${ATTR_VALUE} $<TARGET_FILE:${TARGET_FILE}>
                COMMENT "Setting attribute ${ATTR_NAME}=${ATTR_VALUE} on ${TARGET_FILE}"
                VERBATIM
            )
        endif()
    endforeach()
endfunction()

if(HAIKU_RC_TOOL)
    message(STATUS "Found Haiku resource compiler: ${HAIKU_RC_TOOL}")
else()
    message(STATUS "Haiku resource compiler (rc) not found - resource compilation will be skipped")
endif()

if(HAIKU_XRES_TOOL)
    message(STATUS "Found Haiku extended resource tool: ${HAIKU_XRES_TOOL}")
else()
    message(STATUS "Haiku extended resource tool (xres) not found - resource embedding will be skipped")
endif()

message(STATUS "Loaded HaikuResources.cmake module")