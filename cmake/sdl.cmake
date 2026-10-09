# Shared SDL3 build and link dependencies for gfx, audio and ngfx.
include_guard(GLOBAL)
set(GFX_SDL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL")

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
# SDL otherwise rejects a Unix build with neither X11 nor Wayland. Audio
# needs neither: this also covers gfx returning early for missing headers.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT GOOSE_SDL_VIDEO_REQUESTED)
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "Allow audio without a desktop backend" FORCE)
endif()
add_subdirectory("${GFX_SDL}" "${CMAKE_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)

# SDL records the dependencies used by its own pkg-config file in this target:
# plain library names and flags such as -Wl,-framework,Cocoa on macOS.
get_property(sdl_dep_ids TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_IDS)
set(sdl_syslibs)
set(sdl_ldflags)
foreach(id IN LISTS sdl_dep_ids)
    get_property(pc_specs TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_PKG_CONFIG_SPECS)
    get_property(pc_libs TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_PKG_CONFIG_LIBS)
    get_property(pc_ldflags TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_PKG_CONFIG_LINK_OPTIONS)
    get_property(libs TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_LIBS)
    get_property(ldflags TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_LINK_OPTIONS)
    get_property(cmake_module TARGET SDL3-collector PROPERTY INTERFACE_SDL_DEP_${id}_CMAKE_MODULE)
    list(APPEND sdl_syslibs ${pc_libs})
    if(pc_specs OR pc_libs OR pc_ldflags)
        list(APPEND sdl_ldflags ${pc_ldflags})
    else()
        list(APPEND sdl_ldflags ${ldflags})
        if(NOT cmake_module)
            list(APPEND sdl_syslibs ${libs})
        endif()
    endif()
endforeach()
list(REMOVE_DUPLICATES sdl_syslibs)
list(REMOVE_DUPLICATES sdl_ldflags)
# The gcc-style spelling (gcc, clang, and clang's gcc-style driver on
# Windows), plus what the runtime links anyway.
set(sdl_cc_libs ${sdl_ldflags})
foreach(lib IN LISTS sdl_syslibs)
    list(APPEND sdl_cc_libs "-l${lib}")
endforeach()
if(NOT WIN32)
    list(APPEND sdl_cc_libs -lm -pthread)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND sdl_cc_libs -ldl -lrt)
    endif()
endif()
list(REMOVE_DUPLICATES sdl_cc_libs)
string(JOIN "\n" sdl_cc_libs ${sdl_cc_libs})
if(WIN32)
    set(sdl_msvc_libs)
    foreach(lib IN LISTS sdl_syslibs)
        list(APPEND sdl_msvc_libs "${lib}.lib")
    endforeach()
    string(JOIN "\n" sdl_msvc_libs ${sdl_msvc_libs})
endif()
