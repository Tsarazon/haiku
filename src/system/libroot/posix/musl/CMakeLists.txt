# POSIX musl components - placeholder
cmake_minimum_required(VERSION 3.16)
add_library(posix_musl STATIC empty_placeholder.c)
set_target_properties(posix_musl PROPERTIES OUTPUT_NAME "posix_musl" PREFIX "")
