# Haiku Host Tools - Dual Build System Demo
# This CMakeLists.txt works alongside the existing Jamfile
# It should be included from the main Haiku CMakeLists.txt

# Include our host tools module (path relative to main CMakeLists.txt)
include(HaikuHostTools)

message(STATUS "=== Haiku Host Tools - Dual Build System ===")
message(STATUS "Building alongside Jam system in: ${CMAKE_CURRENT_SOURCE_DIR}")

# Simple host tools that can be built with both systems
haiku_dual_build_tool(data_to_source
    SOURCES data_to_source.cpp
    JAM_TARGET "<build>data_to_source"
)

haiku_dual_build_tool(elf2aout
    SOURCES elf2aout.c
    JAM_TARGET "<build>elf2aout"
)

# More complex tool with libraries
haiku_dual_build_tool(create_image
    SOURCES create_image.cpp
    JAM_TARGET "<build>create_image"
    # Note: Jam uses $(HOST_LIBSUPC++) but we'll use standard linking
)

# Tool with simple C code
haiku_dual_build_tool(cpuidtool
    SOURCES cpuidtool.c
    JAM_TARGET "<build>cpuidtool"
)

message(STATUS "=== Dual Build Summary ===")
message(STATUS "Jam builds: Run 'jam <build>data_to_source' etc.")
message(STATUS "CMake builds: Run 'make cmake_data_to_source' etc.")
message(STATUS "Both systems can coexist and build the same tools")
message(STATUS "CMake tools prefixed with 'cmake_' to avoid conflicts")