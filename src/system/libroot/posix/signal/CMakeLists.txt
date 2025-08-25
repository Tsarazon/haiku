# POSIX signal components - placeholder
cmake_minimum_required(VERSION 3.16)
add_library(posix_signal STATIC empty_placeholder.c)
set_target_properties(posix_signal PROPERTIES OUTPUT_NAME "posix_signal" PREFIX "")
