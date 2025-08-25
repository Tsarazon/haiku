# POSIX stdio components  
cmake_minimum_required(VERSION 3.16)

set(POSIX_STDIO_SOURCES
    _fseek.c
    remove.c
    rename.c
    tempnam.c
    tmpfile.c
    tmpnam.c
)

add_library(posix_stdio STATIC ${POSIX_STDIO_SOURCES})

set_target_properties(posix_stdio PROPERTIES
    OUTPUT_NAME "posix_stdio"
    PREFIX ""
)