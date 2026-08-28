cmake_minimum_required(VERSION 3.20)
project(goose CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(goose
    src/main.cpp
    src/includes.h
    src/utils.h
    src/lexer.h
    src/ast.h
    src/dump.h
    src/parser.h
    src/resolve.h
)

if(MSVC)
    target_compile_options(goose PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_definitions(goose PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(goose PRIVATE -Wall -Wextra)
endif()
