# POSIX locale components - placeholder
cmake_minimum_required(VERSION 3.16)
add_library(posix_locale STATIC empty_placeholder.c)
set_target_properties(posix_locale PROPERTIES OUTPUT_NAME "posix_locale" PREFIX "")
