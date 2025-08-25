# POSIX unistd components - placeholder
cmake_minimum_required(VERSION 3.16)
add_library(posix_unistd STATIC empty_placeholder.c)
set_target_properties(posix_unistd PROPERTIES OUTPUT_NAME "posix_unistd" PREFIX "")
