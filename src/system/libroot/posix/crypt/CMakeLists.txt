# POSIX crypt components - placeholder
cmake_minimum_required(VERSION 3.16)
add_library(posix_crypt STATIC empty_placeholder.c)
set_target_properties(posix_crypt PROPERTIES OUTPUT_NAME "posix_crypt" PREFIX "")
