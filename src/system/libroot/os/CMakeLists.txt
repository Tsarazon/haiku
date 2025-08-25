# Haiku libroot OS layer
cmake_minimum_required(VERSION 3.16)

message(STATUS "Building libroot OS layer")

# OS layer core sources (from Jamfile)
set(OS_SOURCES
    Architecture.cpp
    area.c
    atomic.c
    debug.c
    driver_settings.cpp
    extended_system_info.cpp
    find_directory.cpp
    find_paths.cpp
    fs_attr.cpp
    fs_index.c
    fs_info.cpp
    fs_query.cpp
    fs_volume.c
    image.cpp
    launch.cpp
    memory.cpp
    parsedate.cpp
    port.c
    scheduler.c
    sem.c
    stack_protector.cpp
    system_info.cpp
    system_revision.c
    team.c
    thread.c
    time.cpp
    syscalls.S
    wait_for_objects.cpp
)

# Lock sources (from locks subdirectory)
set(OS_LOCK_SOURCES
    locks/init_once.cpp
    locks/mutex.cpp
    locks/recursive_lock.cpp
    locks/rw_lock.cpp
)

# KMessage from kernel (referenced in Jamfile)
set(KERNEL_MESSAGE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/system/kernel/messaging/KMessage.cpp
)

# Combine all OS sources
set(ALL_OS_SOURCES ${OS_SOURCES} ${OS_LOCK_SOURCES} ${KERNEL_MESSAGE_SOURCES})

# Add architecture-specific sources
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/arch/x86_64
    ${CMAKE_SOURCE_DIR}/headers/private/kernel
    ${CMAKE_SOURCE_DIR}/headers/private/app  
    ${CMAKE_SOURCE_DIR}/headers/private/libroot
    ${CMAKE_SOURCE_DIR}/headers/private/shared
)

# Architecture-specific sources for x86_64
set(OS_ARCH_SOURCES
    arch/x86_64/byteorder.S
    arch/x86_64/get_stack_frame.S
    arch/x86_64/syscalls.inc
    arch/x86_64/system_info.cpp
    arch/x86_64/system_time.cpp
    arch/x86_64/thread.cpp
    arch/x86_64/time.cpp
    arch/x86_64/tls.cpp
)

# Create os_main.o equivalent as static library
add_library(os_main STATIC ${ALL_OS_SOURCES} ${OS_ARCH_SOURCES})

# Set target properties
set_target_properties(os_main PROPERTIES
    OUTPUT_NAME "os_main"
    PREFIX ""
)

# Handle syscalls dependency (needs generated syscalls.S.inc)
# For now, we'll assume syscalls.S can be compiled directly
# In full implementation, this would need syscall generation

message(STATUS "libroot OS layer configured")
message(STATUS "  Sources: ${CMAKE_CURRENT_LIST_LENGTH}")
message(STATUS "  Target: os_main.a")