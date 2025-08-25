# Haiku libroot POSIX layer
cmake_minimum_required(VERSION 3.16)

message(STATUS "Building libroot POSIX layer")

# POSIX main sources (from Jamfile)
set(POSIX_MAIN_SOURCES
    assert.cpp
    cat.cpp
    devctl.c
    dlfcn.c
    dirent.cpp
    errno.c
    fcntl.cpp
    glob.c
    inttypes.c
    math.c
    poll.cpp
    scheduler.cpp
    semaphore.cpp
    spawn.cpp
    syslog.cpp
    termios.c
    utime.c
    utmpx.cpp
)

# User/group backend (simplified - use non-multiuser version)
set(USER_GROUP_SOURCES
    pwd.cpp
    grp.cpp
    shadow.cpp
    user_group_common.cpp
)

# C11 threads library sources (from libstdthreads)
set(STDTHREADS_SOURCES
    libstdthreads/call_once.c
    libstdthreads/cnd.c
    libstdthreads/mtx.c
    libstdthreads/thrd.c
    libstdthreads/tss.c
)

# Architecture-specific POSIX sources for x86_64
set(POSIX_ARCH_SOURCES
    arch/x86_64/fenv.c
    arch/x86_64/siglongjmp.S
    arch/x86_64/sigsetjmp.S
)

# Additional include directories for POSIX layer
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/arch/x86_64
    ${CMAKE_SOURCE_DIR}/headers/private/app
    ${CMAKE_SOURCE_DIR}/headers/private/shared
    ${CMAKE_SOURCE_DIR}/headers/private/syslog_daemon
    ${CMAKE_SOURCE_DIR}/headers/private/kernel
    ${CMAKE_SOURCE_DIR}/headers/private/libroot
)

# Create posix_main.o equivalent as static library
add_library(posix_main STATIC 
    ${POSIX_MAIN_SOURCES} 
    ${USER_GROUP_SOURCES}
    ${STDTHREADS_SOURCES}
)

# Create posix_arch.o for architecture-specific code
add_library(posix_arch STATIC ${POSIX_ARCH_SOURCES})

# Set target properties
set_target_properties(posix_main PROPERTIES
    OUTPUT_NAME "posix_main"
    PREFIX ""
)

set_target_properties(posix_arch PROPERTIES
    OUTPUT_NAME "posix_arch_x86_64"
    PREFIX ""
)

# Add subdirectories for additional POSIX components
# These will build additional object files as per Jamfile
add_subdirectory(crypt)
add_subdirectory(locale)
add_subdirectory(malloc)
add_subdirectory(pthread)
add_subdirectory(signal)
add_subdirectory(stdio)
add_subdirectory(stdlib)
add_subdirectory(string)
add_subdirectory(sys)
add_subdirectory(time)
add_subdirectory(unistd)
add_subdirectory(wchar)

# GNU/musl compatibility layers
add_subdirectory(glibc)
add_subdirectory(musl)

message(STATUS "libroot POSIX layer configured")
message(STATUS "  Core sources: ${POSIX_MAIN_SOURCES}")
message(STATUS "  Targets: posix_main.a, posix_arch_x86_64.a")