# POSIX wchar components - placeholder
cmake_minimum_required(VERSION 3.16)
add_library(posix_wchar STATIC empty_placeholder.c)
set_target_properties(posix_wchar PROPERTIES OUTPUT_NAME "posix_wchar" PREFIX "")
