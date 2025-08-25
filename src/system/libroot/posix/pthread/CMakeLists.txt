# POSIX pthread components - placeholder
cmake_minimum_required(VERSION 3.16)
add_library(posix_pthread STATIC empty_placeholder.c)
set_target_properties(posix_pthread PROPERTIES OUTPUT_NAME "posix_pthread" PREFIX "")
