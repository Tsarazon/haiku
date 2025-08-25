# POSIX malloc components - placeholder
cmake_minimum_required(VERSION 3.16)
add_library(posix_malloc STATIC empty_placeholder.c)
set_target_properties(posix_malloc PROPERTIES OUTPUT_NAME "posix_malloc" PREFIX "")
