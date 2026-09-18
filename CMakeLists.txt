cmake_minimum_required(VERSION 3.20)
project(goose C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# One C runtime for the compiler, TinyCC, SDL and the gfx library, and the
# one `cl game.c` links by default, so a program can link the same archives.
# The non-debug variant even in Debug builds: a /MTd archive does not link
# cleanly into a plain /MT program.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

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
    src/jit.h
    src/runtime/runtime.h
    src/runtime/runtime_threads.h
    src/runtime/runtime_os.h
    src/gfx.h
)

# The shader compiler behind embed_shader: third-party C (third_party/
# cute_spirv), always built, so a gfx program typechecks and emits the same C
# with or without SDL.
add_library(goose_shaderc STATIC src/shaderc.c src/shaderc.h src/gfx/gfx_blob.h)
target_include_directories(goose_shaderc PRIVATE third_party/cute_spirv)
if(MSVC)
    # ckit.h uses typeof and C11 atomics, which MSVC has only in its latest C
    # mode. /W0 as for TinyCC: third-party warnings are not ours to act on.
    target_compile_options(goose_shaderc PRIVATE /W0 /std:clatest /experimental:c11atomics)
else()
    set_target_properties(goose_shaderc PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
    target_compile_options(goose_shaderc PRIVATE -w)
endif()
target_link_libraries(goose PRIVATE goose_shaderc)

# The optional in-process C backend. Present only when the TinyCC submodule has
# been checked out (`git submodule update --init`); the compiler builds and
# behaves exactly the same without it, minus JIT mode.
option(GOOSE_LIBTCC "Build the in-process TinyCC backend when the submodule is present" ON)
if(GOOSE_LIBTCC AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinycc/libtcc.h")
    include(cmake/libtcc.cmake)
endif()
if(GOOSE_HAVE_LIBTCC)
    target_link_libraries(goose PRIVATE libtcc)
    add_dependencies(goose tcc_runtime)
    target_compile_definitions(goose PRIVATE
        GOOSE_HAVE_LIBTCC=1 GOOSE_TCC_LIB_PATH="${TCC_STAGE}")
endif()

if(MSVC)
    # /bigobj: the compiler is one translation unit of header-inline
    # functions, past the object format's default COMDAT section limit.
    target_compile_options(goose PRIVATE /W4 /permissive- /utf-8 /EHsc /bigobj)
    target_compile_definitions(goose PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # -Wmissing-field-initializers (in -Wextra) fires on every aggregate
    # initialised with a prefix of its fields, which is deliberate throughout:
    # the rest are value-initialised.
    target_compile_options(goose PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()
