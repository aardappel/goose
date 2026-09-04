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
    src/clone.h
    src/parser.h
    src/resolve.h
    src/builtins.h
    src/typecheck_cycles.h
    src/typecheck.h
    src/typecheck_types.h
    src/typecheck_exprs.h
    src/typecheck_flow.h
    src/typecheck_calls.h
    src/typecheck_builtins.h
    src/typecheck_nodes.h
    src/optimize.h
    src/optimize_basecase.h
    src/optimize_tre.h
    src/bce.h
    src/codegen.h
    src/codegen_types.h
    src/codegen_frames.h
    src/codegen_values.h
    src/codegen_construct.h
    src/codegen_stmts.h
    src/codegen_calls.h
    src/codegen_builtins.h
    src/codegen_render.h
    src/codegen_emit.h
    src/codegen_nodes.h
    src/runtime_inline.h
    src/runtime/runtime.h
    src/runtime/runtime_threads.h
)

if(MSVC)
    # /bigobj: the compiler is one translation unit of header-inline
    # functions, past the object format's default COMDAT section limit.
    target_compile_options(goose PRIVATE /W4 /permissive- /utf-8 /EHsc /bigobj)
    target_compile_definitions(goose PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(goose PRIVATE -Wall -Wextra)
endif()
