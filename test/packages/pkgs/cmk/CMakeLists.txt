cmake_minimum_required(VERSION 3.20)
project(cmk C)
add_library(cmk STATIC src/cmk.c)
target_include_directories(cmk PUBLIC include)
