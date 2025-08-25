# POSIX string components
cmake_minimum_required(VERSION 3.16)

set(POSIX_STRING_SOURCES
    bcmp.c
    bcopy.c  
    bzero.c
    memcmp.c
    strcasecmp.c
    strcasestr.c
    strchr.c
    strcmp.c
    strcoll.cpp
    strcpy.c
    strdup.cpp
    strerror.c
    strlwr.c
    strncmp.c
    strncpy.cpp
    strndup.cpp
    strrchr.c
    strtok.c
    strupr.c
    strxfrm.cpp
)

add_library(posix_string STATIC ${POSIX_STRING_SOURCES})

set_target_properties(posix_string PROPERTIES
    OUTPUT_NAME "posix_string"
    PREFIX ""
)