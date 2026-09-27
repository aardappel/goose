# Builds the optional ui module: goose_ui, the C layer over Nuklear that
# stdlib/ui.goose declares its extern fns against (src/ui/), with Nuklear
# itself -- one header, from the third_party/nuklear submodule -- compiled
# into it. Included from CMakeLists.txt only when that submodule is checked
# out and GOOSE_UI is on; without it the compiler builds exactly as before,
# and only running a ui program in JIT mode reports that it was not
# compiled in.
#
# As with gfx and physics, the same archive goes into goose, for JIT runs,
# and into programs built from the generated C, whose link inputs are
# written to ${CMAKE_BINARY_DIR}/ui/<config>/link-{msvc,cc}.rsp for
# `goose --ui-link msvc|cc` to print. The layer has no GPU or window of its
# own: stdlib/ui.goose draws through gfx, so a program that renders its ui
# links gfx as well.

set(UI_NUKLEAR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nuklear")

set(ui_layer_sources
    src/ui/ui_core.c
    src/ui/ui_font.c
    src/ui/ui_window.c
    src/ui/ui_widget.c
    src/ui/ui_style.c
    src/ui/ui_draw.c
)
add_library(goose_ui STATIC
    src/ui/ui_api.h
    src/ui/ui_internal.h
    src/ui/ui_nuklear.c
    ${ui_layer_sources}
)
target_include_directories(goose_ui PRIVATE "${UI_NUKLEAR}")
set_target_properties(goose_ui PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
                      INTERPROCEDURAL_OPTIMIZATION OFF)
# Warnings for the layer, none for Nuklear's implementation: third-party
# code is not ours to act on.
if(MSVC)
    set_source_files_properties(${ui_layer_sources} PROPERTIES COMPILE_OPTIONS "/W4;/utf-8")
    set_source_files_properties(src/ui/ui_nuklear.c PROPERTIES COMPILE_OPTIONS "/W0;/utf-8")
    target_compile_definitions(goose_ui PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    set_source_files_properties(${ui_layer_sources} PROPERTIES COMPILE_OPTIONS
                                "-Wall;-Wextra;-Wno-missing-field-initializers")
    set_source_files_properties(src/ui/ui_nuklear.c PROPERTIES COMPILE_OPTIONS "-w")
endif()
if(NOT WIN32)
    target_link_libraries(goose_ui PUBLIC m)
endif()

# --- link inputs for programs built from the generated C ---------------------------
set(UI_LINK_DIR "${CMAKE_BINARY_DIR}/ui/$<CONFIG>")
set(ui_cc_libs)
if(NOT WIN32)
    set(ui_cc_libs "-lm\n")
endif()
file(GENERATE OUTPUT "${UI_LINK_DIR}/link-cc.rsp" CONTENT
"\"$<TARGET_LINKER_FILE:goose_ui>\"
${ui_cc_libs}")
if(WIN32)
    file(GENERATE OUTPUT "${UI_LINK_DIR}/link-msvc.rsp" CONTENT
"\"$<TARGET_LINKER_FILE:goose_ui>\"
")
endif()

file(READ "${UI_NUKLEAR}/clib.json" ui_clib)
string(REGEX MATCH "\"version\": *\"([0-9.]+)\"" ui_version_match "${ui_clib}")
set(GOOSE_HAVE_UI ON)
message(STATUS "ui: Nuklear ${CMAKE_MATCH_1}, static")
