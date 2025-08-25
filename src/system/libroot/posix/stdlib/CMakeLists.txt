# POSIX stdlib components
cmake_minimum_required(VERSION 3.16)

# Standard library sources (from directory listing)
set(POSIX_STDLIB_SOURCES
    abs.c
    atfork.c
    atof.c
    atoi.c
    bsearch.c
    div.c
    env.cpp
    exit.cpp
    heapsort.c
    merge.c
    mktemp.c
    pty.cpp
    qsort.c
    qsort_r.c
    radixsort.c
    random.c
    reallocarray.cpp
    realpath.cpp
    strfmon.c
)

add_library(posix_stdlib STATIC ${POSIX_STDLIB_SOURCES})

set_target_properties(posix_stdlib PROPERTIES
    OUTPUT_NAME "posix_stdlib"
    PREFIX ""
)