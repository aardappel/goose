# Builds the optional graphics module: SDL3 out of the third_party/SDL
# submodule as a static library, and goose_gfx, the C layer over SDL_GPU that
# stdlib/gfx.goose declares its extern fns against (src/gfx/). Included from
# CMakeLists.txt only when that submodule is checked out and GOOSE_GFX is on;
# without it the compiler builds exactly as before, and only running a gfx
# program in JIT mode reports that it was not compiled in.
#
# The same archives go into goose itself, for JIT runs, and into programs
# built from the generated C: the link inputs those need are written to
# ${CMAKE_BINARY_DIR}/gfx/<config>/link-{msvc,cc}.rsp, which
# `goose --gfx-link msvc|cc` prints the path of.

set(GFX_SDL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL")

# --- a video backend on Linux ------------------------------------------------
# SDL's configure does not fail without the X11 and Wayland development
# headers: it quietly builds an SDL that cannot open a window. Take the
# opt-out path instead, saying what to install.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 QUIET)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GFX_WAYLAND QUIET wayland-client)
    endif()
    if(NOT X11_FOUND AND NOT GFX_WAYLAND_FOUND)
        message(STATUS "gfx: neither the X11 nor the Wayland development files were found, "
                       "so SDL could not open a window; building without the gfx module. "
                       "Install the packages in third_party/SDL/docs/README-linux.md "
                       "(at least libx11-dev libxext-dev, or libwayland-dev libxkbcommon-dev "
                       "wayland-protocols) and reconfigure.")
        return()
    endif()
endif()

set(GOOSE_SDL_VIDEO_REQUESTED ON)
include("${CMAKE_CURRENT_LIST_DIR}/sdl.cmake")

# --- the gfx layer -----------------------------------------------------------
add_library(goose_gfx STATIC
    src/gfx/gfx_api.h
    src/gfx/gfx_blob.h
    src/gfx/gfx_internal.h
    src/gfx/gfx_device.c
    src/gfx/gfx_resources.c
    src/gfx/gfx_pipeline.c
    src/gfx/gfx_draw.c
)
# Public: goose, and a program built from the generated C, both link SDL
# through it.
target_link_libraries(goose_gfx PUBLIC SDL3::SDL3-static)
set_target_properties(goose_gfx PROPERTIES C_STANDARD 11 INTERPROCEDURAL_OPTIMIZATION OFF)
if(MSVC)
    target_compile_options(goose_gfx PRIVATE /W4 /utf-8)
    target_compile_definitions(goose_gfx PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(goose_gfx PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

# --- link inputs for programs built from the generated C ---------------------
# sdl.cmake collects SDL's system libraries and platform linker flags.
set(GFX_LINK_DIR "${CMAKE_BINARY_DIR}/gfx/$<CONFIG>")
# The archives are quoted, since a build directory may contain spaces; both
# kinds of driver unquote response-file arguments.
file(GENERATE OUTPUT "${GFX_LINK_DIR}/link-cc.rsp" CONTENT
"\"$<TARGET_LINKER_FILE:goose_gfx>\"
\"$<TARGET_LINKER_FILE:SDL3-static>\"
${sdl_cc_libs}
")
set(gfx_rsp_line ${sdl_cc_libs})
if(WIN32)
    # And the MSVC-style one (cl, clang-cl), which names libraries by file.
    file(GENERATE OUTPUT "${GFX_LINK_DIR}/link-msvc.rsp" CONTENT
"\"$<TARGET_LINKER_FILE:goose_gfx>\"
\"$<TARGET_LINKER_FILE:SDL3-static>\"
${sdl_msvc_libs}
")
    set(gfx_rsp_line ${sdl_msvc_libs})
endif()

file(STRINGS "${GFX_SDL}/include/SDL3/SDL_version.h" gfx_sdl_version
     REGEX "^#define SDL_(MAJOR|MINOR|MICRO)_VERSION +[0-9]+")
string(REGEX REPLACE "[^;]* ([0-9]+);[^;]* ([0-9]+);[^;]* ([0-9]+)" "\\1.\\2.\\3"
       gfx_sdl_version "${gfx_sdl_version}")
set(GOOSE_HAVE_GFX ON)
string(REPLACE "\n" " " gfx_rsp_line "${gfx_rsp_line}")
message(STATUS "gfx: SDL ${gfx_sdl_version}, static; programs link ${gfx_rsp_line}")
