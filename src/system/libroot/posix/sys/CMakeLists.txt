# POSIX sys components - placeholder
cmake_minimum_required(VERSION 3.16)
add_library(posix_sys STATIC empty_placeholder.c)
set_target_properties(posix_sys PROPERTIES OUTPUT_NAME "posix_sys" PREFIX "")
