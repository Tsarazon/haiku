# Haiku System Glue - Startup Objects for Cross-Compiler
# This builds the essential startup objects needed by the cross-compiler

cmake_minimum_required(VERSION 3.16)

# Use cross-compiler BEFORE project() call
set(CMAKE_C_COMPILER /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-gcc CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-gcc CACHE FILEPATH "" FORCE)

# Skip compiler testing for glue objects (they need special linking)
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_ASM_COMPILER_WORKS TRUE)

project(haiku_glue LANGUAGES C ASM)

# Add Haiku CMake modules to path
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../../build/cmake/modules)

message(STATUS "=== Building Haiku System Glue Objects ===")

# Include headers directly from source tree (like Jam build does)
include_directories(SYSTEM
    ${CMAKE_SOURCE_DIR}/../../../headers
    ${CMAKE_SOURCE_DIR}/../../../headers/os
    ${CMAKE_SOURCE_DIR}/../../../headers/os/support
    ${CMAKE_SOURCE_DIR}/../../../headers/os/kernel
    ${CMAKE_SOURCE_DIR}/../../../headers/os/storage
    ${CMAKE_SOURCE_DIR}/../../../headers/posix
    ${CMAKE_SOURCE_DIR}/../../../headers/private/system
    ${CMAKE_SOURCE_DIR}/../../../headers/private/system/arch/x86_64
    ${CMAKE_SOURCE_DIR}/../../../headers/private/kernel
    ${CMAKE_SOURCE_DIR}/../../../headers/private/shared
)

# Common glue sources (from Jamfile analysis)
set(COMMON_GLUE_SOURCES
    init_term_dyn.c
    start_dyn.c  
    haiku_version_glue.c
)

# Architecture-specific glue sources for x86_64
set(ARCH_GLUE_SOURCES
    arch/x86_64/crti.S
    arch/x86_64/crtn.S
)

# Build individual glue objects (not a library, individual .o files)
foreach(source ${COMMON_GLUE_SOURCES})
    get_filename_component(object_name ${source} NAME_WE)
    add_library(${object_name}_obj OBJECT ${source})
    set_target_properties(${object_name}_obj PROPERTIES
        POSITION_INDEPENDENT_CODE OFF
        OUTPUT_NAME ${object_name}
    )
endforeach()

foreach(source ${ARCH_GLUE_SOURCES})
    get_filename_component(object_name ${source} NAME_WE)
    add_library(${object_name}_obj OBJECT ${source})
    set_target_properties(${object_name}_obj PROPERTIES
        POSITION_INDEPENDENT_CODE OFF
        OUTPUT_NAME ${object_name}
    )
endforeach()

# Create custom commands to copy objects with proper names
add_custom_command(
    OUTPUT ${HAIKU_LIBS_DIR}/crti.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:crti_obj> ${HAIKU_LIBS_DIR}/crti.o
    DEPENDS crti_obj
)

add_custom_command(
    OUTPUT ${HAIKU_LIBS_DIR}/crtn.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:crtn_obj> ${HAIKU_LIBS_DIR}/crtn.o
    DEPENDS crtn_obj
)

add_custom_command(
    OUTPUT ${HAIKU_LIBS_DIR}/start_dyn.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:start_dyn_obj> ${HAIKU_LIBS_DIR}/start_dyn.o
    DEPENDS start_dyn_obj
)

add_custom_command(
    OUTPUT ${HAIKU_LIBS_DIR}/init_term_dyn.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:init_term_dyn_obj> ${HAIKU_LIBS_DIR}/init_term_dyn.o
    DEPENDS init_term_dyn_obj
)

add_custom_target(glue_objects ALL DEPENDS 
    ${HAIKU_LIBS_DIR}/crti.o
    ${HAIKU_LIBS_DIR}/crtn.o
    ${HAIKU_LIBS_DIR}/start_dyn.o
    ${HAIKU_LIBS_DIR}/init_term_dyn.o
)

# Install glue objects to GCC's expected location  
# This is where the cross-compiler looks for startup objects
install(FILES 
    ${HAIKU_LIBS_DIR}/crti.o
    ${HAIKU_LIBS_DIR}/crtn.o
    ${HAIKU_LIBS_DIR}/start_dyn.o
    ${HAIKU_LIBS_DIR}/init_term_dyn.o
    DESTINATION /home/ruslan/haiku/generated/cross-tools-x86_64/lib/gcc/x86_64-unknown-haiku/13.3.0/
    COMPONENT glue
)

message(STATUS "Haiku Glue Objects configured:")
message(STATUS "  Common objects: ${COMMON_GLUE_SOURCES}")
message(STATUS "  Arch objects: ${ARCH_GLUE_SOURCES}")
message(STATUS "  Install to: cross-tools GCC lib directory")