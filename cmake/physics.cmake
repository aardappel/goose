# Builds the optional physics module: Box3D out of the third_party/box3d
# submodule as a static library, and goose_physics, the C layer over it that
# stdlib/physics.goose declares its extern fns against (src/physics/).
# Included from CMakeLists.txt only when that submodule is checked out and
# GOOSE_PHYSICS is on; without it the compiler builds exactly as before, and
# only running a physics program in JIT mode reports that it was not
# compiled in.
#
# As with gfx, the same archives go into goose, for JIT runs, and into
# programs built from the generated C, whose link inputs are written to
# ${CMAKE_BINARY_DIR}/physics/<config>/link-{msvc,cc}.rsp for
# `goose --physics-link msvc|cc` to print.

set(PHYSICS_BOX3D "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box3d")

if(CMAKE_VERSION VERSION_LESS 3.22)
    message(STATUS "physics: Box3D needs CMake 3.22 or later; building without the physics module")
    return()
endif()

# --- Box3D ---------------------------------------------------------------------
# Only the library: its samples, tests and documentation are options of a
# top-level build alone. Single precision, which the layer's structs are, and
# static whatever BUILD_SHARED_LIBS says elsewhere.
set(BOX3D_DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory("${PHYSICS_BOX3D}" "${CMAKE_BINARY_DIR}/box3d" EXCLUDE_FROM_ALL)
# Box3D asks for the debug C runtime in a debug build; everything here links
# the release one (CMakeLists.txt).
set_target_properties(box3d PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")

# --- the physics layer -----------------------------------------------------------
add_library(goose_physics STATIC
    src/physics/physics_api.h
    src/physics/physics_internal.h
    src/physics/physics_core.c
    src/physics/physics_world.c
    src/physics/physics_body.c
    src/physics/physics_shape.c
    src/physics/physics_joint.c
    src/physics/physics_geometry.c
)
# Box3D starts worker threads of its own inside a step.
find_package(Threads REQUIRED)
target_link_libraries(goose_physics PUBLIC box3d::box3d Threads::Threads)
# C17, as Box3D's headers are.
set_target_properties(goose_physics PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON
                      INTERPROCEDURAL_OPTIMIZATION OFF)
if(MSVC)
    target_compile_options(goose_physics PRIVATE /W4 /utf-8)
    target_compile_definitions(goose_physics PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(goose_physics PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

# --- link inputs for programs built from the generated C -----------------------------
# The two archives, quoted since a build directory may contain spaces, and
# what Box3D needs of the system: nothing beyond the C runtime on Windows,
# libm and threads elsewhere.
set(PHYSICS_LINK_DIR "${CMAKE_BINARY_DIR}/physics/$<CONFIG>")
set(physics_cc_libs)
if(NOT WIN32)
    set(physics_cc_libs "-lm\n-pthread\n")
endif()
file(GENERATE OUTPUT "${PHYSICS_LINK_DIR}/link-cc.rsp" CONTENT
"\"$<TARGET_LINKER_FILE:goose_physics>\"
\"$<TARGET_LINKER_FILE:box3d>\"
${physics_cc_libs}")
if(WIN32)
    file(GENERATE OUTPUT "${PHYSICS_LINK_DIR}/link-msvc.rsp" CONTENT
"\"$<TARGET_LINKER_FILE:goose_physics>\"
\"$<TARGET_LINKER_FILE:box3d>\"
")
endif()

get_directory_property(physics_box3d_version DIRECTORY "${PHYSICS_BOX3D}" DEFINITION box3d_VERSION)
set(GOOSE_HAVE_PHYSICS ON)
message(STATUS "physics: Box3D ${physics_box3d_version}, static")
