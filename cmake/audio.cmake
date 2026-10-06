# The optional PCM mixer: independent of graphics or a video backend.
include("${CMAKE_CURRENT_LIST_DIR}/sdl.cmake")

add_library(goose_audio STATIC src/audio/audio.c src/audio/audio_api.h)
target_link_libraries(goose_audio PUBLIC SDL3::SDL3-static)
set_target_properties(goose_audio PROPERTIES C_STANDARD 11 INTERPROCEDURAL_OPTIMIZATION OFF)
if(MSVC)
    target_compile_options(goose_audio PRIVATE /W4 /utf-8)
    target_compile_definitions(goose_audio PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(goose_audio PRIVATE -Wall -Wextra)
endif()

set(AUDIO_LINK_DIR "${CMAKE_BINARY_DIR}/audio/$<CONFIG>")
file(GENERATE OUTPUT "${AUDIO_LINK_DIR}/link-cc.rsp" CONTENT
"\"$<TARGET_LINKER_FILE:goose_audio>\"
\"$<TARGET_LINKER_FILE:SDL3-static>\"
${sdl_cc_libs}
")
if(WIN32)
    file(GENERATE OUTPUT "${AUDIO_LINK_DIR}/link-msvc.rsp" CONTENT
"\"$<TARGET_LINKER_FILE:goose_audio>\"
\"$<TARGET_LINKER_FILE:SDL3-static>\"
${sdl_msvc_libs}
")
endif()
set(GOOSE_HAVE_AUDIO ON)
message(STATUS "audio: SDL3 PCM mixer, static")

# Explicit test target: never opens a physical device or adds to normal builds.
add_executable(goose_audio_test EXCLUDE_FROM_ALL test/audio/audio_device.c)
target_link_libraries(goose_audio_test PRIVATE goose_audio)
set_target_properties(goose_audio_test PROPERTIES C_STANDARD 11)
