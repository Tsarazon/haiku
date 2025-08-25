# POSIX time components - placeholder
cmake_minimum_required(VERSION 3.16)
add_library(posix_time STATIC empty_placeholder.c)
set_target_properties(posix_time PROPERTIES OUTPUT_NAME "posix_time" PREFIX "")
