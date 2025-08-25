# Simple CMake build for Haiku 'clear' command with cross-compiler
# This demonstrates proper cross-compilation for migrating simple CLI tools

cmake_minimum_required(VERSION 3.16)

# ALWAYS use cross-compiler - this is the proper Haiku way (MUST be before project())
set(CMAKE_C_COMPILER /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-gcc CACHE FILEPATH "" FORCE)

# Set sysroot for cross-compilation
set(CMAKE_SYSROOT /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot)

# Configure cross-compilation paths
set(CMAKE_FIND_ROOT_PATH /home/ruslan/haiku/generated/cross-tools-x86_64/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

project(clear LANGUAGES C)

# Add Haiku CMake modules to path
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/../../../build/cmake/modules)

# Define the executable
add_executable(clear ../clear.c)

# Set output directory to match Haiku's structure
set_target_properties(clear PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${HAIKU_BIN_DIR}
)

# Install to proper location (would be /bin in real Haiku build)
install(TARGETS clear
    RUNTIME DESTINATION bin
    COMPONENT clear
)

message(STATUS "CMake build configured for 'clear' command")
message(STATUS "  Source: ../clear.c") 
message(STATUS "  Output: bin/clear")
message(STATUS "  Dependencies: none (matches StdBinCommands pattern)")