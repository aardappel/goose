# Builds the optional ngfx module: NoGraphicsAPI out of the
# third_party/NoGraphicsAPI submodule, and goose_ngfx, the layer that
# stdlib/ngfx.goose declares its extern fns against (src/ngfx/): gfx's
# platform half over SDL3, and drawing over NoGraphicsAPI and its utility
# library. Included from CMakeLists.txt only when that submodule is checked
# out and GOOSE_NGFX is on. Whatever else it lacks, it says so and returns,
# and the compiler builds exactly as before.
#
# As with gfx, the same archives go into goose, for JIT runs, and into
# programs built from the generated C, whose link inputs
# `goose --ngfx-link msvc|cc` prints.

set(NGFX_NG "${CMAKE_CURRENT_SOURCE_DIR}/third_party/NoGraphicsAPI")

# --- what NoGraphicsAPI needs -------------------------------------------------------
# Its CMakeLists stops the configure for each of these, so check them first:
# CMake 3.24, a 64-bit target, no MinGW, and its backend's platform: Metal 4
# (macOS 26) on Apple, Vulkan 1.4.357 elsewhere. It presents on Apple and
# Windows only; on Linux it is headless-only, and ngfx is not built there yet.
if(CMAKE_VERSION VERSION_LESS 3.24)
    message(STATUS "ngfx: NoGraphicsAPI needs CMake 3.24 or later; building without ngfx")
    return()
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR MINGW)
    message(STATUS "ngfx: NoGraphicsAPI needs a 64-bit, non-MinGW target; building without ngfx")
    return()
endif()
if(NOT APPLE AND NOT WIN32)
    message(STATUS "ngfx: NoGraphicsAPI is headless-only on ${CMAKE_SYSTEM_NAME}; building without ngfx")
    return()
endif()
if(APPLE)
    if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 26.0)
        message(STATUS "ngfx: Metal 4 needs macOS 26, but CMAKE_OSX_DEPLOYMENT_TARGET is "
                       "${CMAKE_OSX_DEPLOYMENT_TARGET}; building without ngfx")
        return()
    endif()
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-version
                    OUTPUT_VARIABLE ngfx_sdk_version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT ngfx_sdk_version OR ngfx_sdk_version VERSION_LESS 26.0)
        message(STATUS "ngfx: Metal 4 needs the macOS 26 SDK (Xcode 26), found '${ngfx_sdk_version}'; "
                       "building without ngfx")
        return()
    endif()
else()
    find_package(Vulkan 1.4.357 QUIET)
    if(NOT Vulkan_FOUND)
        message(STATUS "ngfx: NoGraphicsAPI's Vulkan backend needs the Vulkan SDK 1.4.357 or later "
                       "(https://vulkan.lunarg.com); building without ngfx")
        return()
    endif()
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt")
    message(STATUS "ngfx: the window needs the third_party/SDL submodule; building without ngfx")
    return()
endif()

# --- the shader tools ----------------------------------------------------------------
# The layer's own shaders need the tools embed_slang runs (CMakeLists.txt
# finds them): slangc, and Xcode's Metal toolchain on Apple or spirv-val
# elsewhere.
if(NOT GOOSE_SLANGC_PATH)
    message(STATUS "ngfx: no slangc (set GOOSE_SLANGC_PATH); building without ngfx")
    return()
endif()
if(APPLE)
    execute_process(COMMAND xcrun -sdk macosx metal --version RESULT_VARIABLE ngfx_metal_result
                    OUTPUT_QUIET ERROR_QUIET)
    if(NOT ngfx_metal_result EQUAL 0)
        message(STATUS "ngfx: no Metal toolchain (xcodebuild -downloadComponent MetalToolchain); "
                       "building without ngfx")
        return()
    endif()
elseif(NOT GOOSE_SPIRV_VAL_PATH)
    message(STATUS "ngfx: no spirv-val (from the Vulkan SDK); building without ngfx")
    return()
endif()

add_subdirectory("${NGFX_NG}" "${CMAKE_BINARY_DIR}/NoGraphicsAPI" EXCLUDE_FROM_ALL)
set(GOOSE_SDL_VIDEO_REQUESTED ON)
include("${CMAKE_CURRENT_LIST_DIR}/sdl.cmake")

set(NGFX_OUT "${CMAKE_BINARY_DIR}/ngfx")

# --- the layer's own shaders --------------------------------------------------------
# The screen's copy into the window, and the downsampling generate_mips
# dispatches, each compiled as embed_slang compiles a module (src/slangc.h):
# every entry point at once, NoGraphicsAPI's options, into a header of
# bytes. Built here rather than embedded in stdlib/ngfx.goose, which every
# program importing ngfx would compile (docs/design/ngfx.md).
set(ngfx_shader_options -fvk-use-c-layout -matrix-layout-row-major
    -I "${NGFX_NG}/include" -I "${NGFX_NG}/utility/include")
function(ngfx_layer_shader name)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/src/ngfx/${name}.slang")
    set(bin "${NGFX_OUT}/${name}.bin")
    if(APPLE)
        add_custom_command(
            OUTPUT "${bin}"
            BYPRODUCTS "${NGFX_OUT}/${name}.metal" "${NGFX_OUT}/${name}.air"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${NGFX_OUT}"
            COMMAND "${GOOSE_SLANGC_PATH}" "${source}" -target metal -DNOGRAPHICSAPI_METAL ${ngfx_shader_options}
                    -o "${NGFX_OUT}/${name}.metal"
            COMMAND xcrun -sdk macosx metal -std=metal4.0 -c "${NGFX_OUT}/${name}.metal" -o "${NGFX_OUT}/${name}.air"
            COMMAND xcrun -sdk macosx metallib "${NGFX_OUT}/${name}.air" -o "${bin}"
            DEPENDS "${source}" "${NGFX_NG}/include/NoGraphicsAPI/shader.slang"
            VERBATIM
            COMMENT "Compiling ngfx's ${name} to a metallib")
    else()
        add_custom_command(
            OUTPUT "${bin}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${NGFX_OUT}"
            COMMAND "${GOOSE_SLANGC_PATH}" "${source}" -target spirv -profile spirv_1_5 -emit-spirv-directly
                    -fvk-use-entrypoint-name ${ngfx_shader_options} -capability spvDescriptorHeapEXT -o "${bin}"
            COMMAND "${GOOSE_SPIRV_VAL_PATH}" --target-env vulkan1.4 --scalar-block-layout "${bin}"
            DEPENDS "${source}" "${NGFX_NG}/include/NoGraphicsAPI/shader.slang"
            VERBATIM
            COMMENT "Compiling ngfx's ${name} to SPIR-V")
    endif()
    add_custom_command(
        OUTPUT "${NGFX_OUT}/${name}_code.h"
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${bin} -DOUTPUT=${NGFX_OUT}/${name}_code.h
                -DNAME=${name}_code -P "${CMAKE_CURRENT_LIST_DIR}/bin2h.cmake"
        DEPENDS "${bin}" "${CMAKE_CURRENT_LIST_DIR}/bin2h.cmake"
        VERBATIM)
endfunction()
ngfx_layer_shader(ngfx_screen)
ngfx_layer_shader(ngfx_mips)

# --- the layer ------------------------------------------------------------------
add_library(goose_ngfx STATIC
    src/ngfx/ngfx_api.h
    src/ngfx/ngfx_blob.h
    src/ngfx/ngfx_internal.h
    src/ngfx/ngfx_gpu.hpp
    src/ngfx/ngfx_platform.c
    src/ngfx/ngfx_device.cpp
    src/ngfx/ngfx_resources.cpp
    src/ngfx/ngfx_draw.cpp
    "${NGFX_OUT}/ngfx_screen_code.h"
    "${NGFX_OUT}/ngfx_mips_code.h"
)
target_include_directories(goose_ngfx PRIVATE src/ngfx "${NGFX_OUT}")
target_link_libraries(goose_ngfx PUBLIC SDL3::SDL3-static NoGraphicsAPI::NoGraphicsAPI
                      NoGraphicsAPIUtility::uploads NoGraphicsAPIUtility::textures NoGraphicsAPIUtility::allocators)
target_compile_features(goose_ngfx PRIVATE cxx_std_20)
set_target_properties(goose_ngfx PROPERTIES C_STANDARD 11 INTERPROCEDURAL_OPTIMIZATION OFF)
NoGraphicsAPI_disable_exceptions(goose_ngfx)
if(MSVC)
    target_compile_options(goose_ngfx PRIVATE /W4 /utf-8)
    target_compile_definitions(goose_ngfx PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(goose_ngfx PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

# --- link inputs for programs built from the generated C -----------------------------
# The layer, NoGraphicsAPI's archives, SDL's, and what they need of the
# system: Metal's frameworks and the C++ runtime on Apple, since a program's
# own driver is the C one, and Vulkan's loader on Windows.
set(NGFX_LINK_DIR "${CMAKE_BINARY_DIR}/ngfx/$<CONFIG>")
set(ngfx_archives
"\"$<TARGET_LINKER_FILE:goose_ngfx>\"
\"$<TARGET_LINKER_FILE:NoGraphicsAPIUtility_uploads>\"
\"$<TARGET_LINKER_FILE:NoGraphicsAPIUtility_textures>\"
\"$<TARGET_LINKER_FILE:NoGraphicsAPIUtility_allocators>\"
\"$<TARGET_LINKER_FILE:NoGraphicsAPI>\"
\"$<TARGET_LINKER_FILE:SDL3-static>\"")
if(APPLE)
    set(ngfx_system "-framework\nMetal\n-framework\nQuartzCore\n-framework\nFoundation\n-lc++\n-lobjc")
else()
    set(ngfx_system "\"$<TARGET_LINKER_FILE:Vulkan::Vulkan>\"")
endif()
file(GENERATE OUTPUT "${NGFX_LINK_DIR}/link-cc.rsp" CONTENT
"${ngfx_archives}
${sdl_cc_libs}
${ngfx_system}
")
if(WIN32)
    file(GENERATE OUTPUT "${NGFX_LINK_DIR}/link-msvc.rsp" CONTENT
"${ngfx_archives}
${ngfx_system}
${sdl_msvc_libs}
")
endif()

set(GOOSE_HAVE_NGFX ON)
message(STATUS "ngfx: the module's layer over NoGraphicsAPI and SDL")
