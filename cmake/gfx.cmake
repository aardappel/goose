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

# --- SDL ---------------------------------------------------------------------
# Static only. SDL_DEPS_SHARED stays on: on Linux SDL then links only the C
# library and loads X11, Wayland, audio and udev at runtime, so goose gains no
# hard GUI dependencies. SDL_GPU draws through neither the 2D renderer nor
# OpenGL, and nothing uses the camera, so those are left out.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${GFX_SDL}" "${CMAKE_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)

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
# SDL records what it links in the properties of its SDL3-collector target,
# which is also what its own pkg-config file is generated from: plain library
# names, and linker flags such as -Wl,-framework,Cocoa for the macOS
# frameworks that its target spells as $<LINK_LIBRARY:FRAMEWORK,...>.
get_property(gfx_dep_ids TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_IDS)
set(gfx_syslibs)
set(gfx_ldflags)
foreach(id IN LISTS gfx_dep_ids)
    get_property(pc_specs TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_PKG_CONFIG_SPECS)
    get_property(pc_libs TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_PKG_CONFIG_LIBS)
    get_property(pc_ldflags TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_PKG_CONFIG_LINK_OPTIONS)
    get_property(libs TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_LIBS)
    get_property(ldflags TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_LINK_OPTIONS)
    get_property(cmake_module TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_CMAKE_MODULE)
    list(APPEND gfx_syslibs ${pc_libs})
    if(pc_specs OR pc_libs OR pc_ldflags)
        list(APPEND gfx_ldflags ${pc_ldflags})
    else()
        list(APPEND gfx_ldflags ${ldflags})
        if(NOT cmake_module)
            list(APPEND gfx_syslibs ${libs})
        endif()
    endif()
endforeach()
list(REMOVE_DUPLICATES gfx_syslibs)
list(REMOVE_DUPLICATES gfx_ldflags)
# The gcc-style spelling (gcc, clang, and clang's gcc-style driver on
# Windows), plus what the runtime links anyway.
set(gfx_cc_libs ${gfx_ldflags})
foreach(lib IN LISTS gfx_syslibs)
    list(APPEND gfx_cc_libs "-l${lib}")
endforeach()
if(NOT WIN32)
    list(APPEND gfx_cc_libs -lm -pthread)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND gfx_cc_libs -ldl -lrt)
    endif()
endif()
list(REMOVE_DUPLICATES gfx_cc_libs)
string(JOIN "\n" gfx_cc_libs ${gfx_cc_libs})
set(GFX_LINK_DIR "${CMAKE_BINARY_DIR}/gfx/$<CONFIG>")
# The archives are quoted, since a build directory may contain spaces; both
# kinds of driver unquote response-file arguments.
file(GENERATE OUTPUT "${GFX_LINK_DIR}/link-cc.rsp" CONTENT
"\"$<TARGET_LINKER_FILE:goose_gfx>\"
\"$<TARGET_LINKER_FILE:SDL3-static>\"
${gfx_cc_libs}
")
set(gfx_rsp_line ${gfx_cc_libs})
if(WIN32)
    # And the MSVC-style one (cl, clang-cl), which names libraries by file.
    set(gfx_msvc_libs)
    foreach(lib IN LISTS gfx_syslibs)
        list(APPEND gfx_msvc_libs "${lib}.lib")
    endforeach()
    string(JOIN "\n" gfx_msvc_libs ${gfx_msvc_libs})
    file(GENERATE OUTPUT "${GFX_LINK_DIR}/link-msvc.rsp" CONTENT
"\"$<TARGET_LINKER_FILE:goose_gfx>\"
\"$<TARGET_LINKER_FILE:SDL3-static>\"
${gfx_msvc_libs}
")
    set(gfx_rsp_line ${gfx_msvc_libs})
endif()

file(STRINGS "${GFX_SDL}/include/SDL3/SDL_version.h" gfx_sdl_version
     REGEX "^#define SDL_(MAJOR|MINOR|MICRO)_VERSION +[0-9]+")
string(REGEX REPLACE "[^;]* ([0-9]+);[^;]* ([0-9]+);[^;]* ([0-9]+)" "\\1.\\2.\\3"
       gfx_sdl_version "${gfx_sdl_version}")
set(GOOSE_HAVE_GFX ON)
string(REPLACE "\n" " " gfx_rsp_line "${gfx_rsp_line}")
message(STATUS "gfx: SDL ${gfx_sdl_version}, static; programs link ${gfx_rsp_line}")
