# Plan: an optional graphics module on SDL3

Status: plan only, nothing implemented (written 2026-09-16). This is the brief
for the session that builds it; once built, it gets replaced by a design doc in
the style of `jit_backend.md`.

## Decisions

* **SDL3** provides the GPU (SDL_GPU: D3D12, Metal, Vulkan) and, later, window,
  input and audio. SDL comes from a git submodule at `third_party/SDL`.
* **Source build, static linking, on all three desktop platforms.** SDL is
  compiled by Goose's CMake as a static library. It goes into `goose` itself,
  for JIT runs, and into programs built from the generated C (AOT). No
  prebuilt binaries or shared libraries are involved.
* **Windows uses the static C runtime everywhere.** That covers every target
  Goose's CMake builds, SDL included. Programs compiled with a plain `cl game.c`
  already get it, because `/MT` is cl's default.
* **Shaders are a GLSL 450 subset compiled by `cute_spirv.h`.** It is copied
  into the tree, not a submodule. The compilation happens inside `goose`, at
  compile time, producing:
  * SPIR-V for Vulkan;
  * MSL source for Metal;
  * HLSL for D3D12, which is turned into DXBC at load time by the system's
    `d3dcompiler_47.dll`.
* **Opting out must be clean.** Without the SDL submodule, or with
  `-DGOOSE_GFX=OFF`:
  * the compiler builds and behaves exactly as today;
  * programs using the module still typecheck and still emit C;
  * only a JIT run of such a program fails, with a clear message;
  * the gfx run tests report skips, not failures.
* **Module name:** `gfx` is the working name throughout.

Why this direction, in short:

* SDL_GPU has what Lobster's GL layer uses: compute, storage buffers and
  textures, 3D textures, MSAA, mip generation, and buffer and texture readback.
  It lacks GPU timer queries. Its API has been ABI-stable since 3.2.0.
* `cute_spirv.h` targets SDL_GPU's per-backend binding conventions directly.
  That removes the most error-prone part of SDL_GPU and needs neither DXC nor
  SPIRV-Cross.
* The rest of the research (sokol, WebGPU, bgfx, cute_framework and others)
  is summarized in the auto-memory, not repeated here.

## Layout

New and changed paths:

    .gitmodules                      + third_party/SDL (shallow)
    CMakeLists.txt                   static MSVC runtime; cute_spirv; gfx gate
    cmake/gfx.cmake                  SDL options, goose_gfx, link files, checks
    third_party/SDL/                 submodule at release-3.4.16
    third_party/cute_spirv/          cute_spirv.h, ckit.h, README.md (origin)
    src/shaderc.c                    the one C file implementing cute_spirv+ckit
    src/gfx.h                        compiler side: have_gfx, embed_shader,
                                     JIT symbol registration
    src/gfx/gfx_api.h                X-macro list of every gs_gfx_* function
    src/gfx/gfx_blob.h               the embedded shader blob layout, shared
    src/gfx/gfx.c ...                the C layer over SDL (goose_gfx library)
    stdlib/gfx.goose                 the Goose module
    scripts/toolchain.py             have_gfx probe; link inputs for AOT
    test/gfx/                        new test category
    .github/workflows/ci.yml         Linux packages, headless Vulkan

## Architecture

    Goose program --extern fn--> gs_gfx_* (goose_gfx, native C) --> SDL3 (static)

**The layer between Goose and SDL is a natively compiled static library,
`goose_gfx`.** It is not a runtime header spliced into the generated C the way
`runtime_os.h` is. That choice is what keeps the two backends identical and
the boundary small:

* Neither the generated C nor TinyCC ever sees an SDL header. Nothing
  SDL-shaped has to be staged for JIT runs, and TinyCC never parses SDL.h.
* A JIT run only needs the `gs_gfx_*` symbols registered with
  `tcc_add_symbol`: tens of functions, not SDL's 1,270.
* Code that needs real platform headers lives only in native code. The first
  example is calling D3DCompile.
* JIT and AOT execute the same compiled layer. A JIT run gets it inside
  `goose`; an AOT program links the same archive.

**One list is the source of truth.** `src/gfx/gfx_api.h` names every function
once, as `GFX_FN(ret, name, params)`. It expands into:

* the C prototypes;
* the JIT symbol table in `src/gfx.h`;
* a consistency test against the `extern` declarations in `stdlib/gfx.goose`.

**Rules for the boundary.** These are forced by the layer being compiled
separately from the program, and by TinyCC linking a different C runtime:

* Only what `extern fn` can pass crosses: scalars, slices, `T&` to flat
  structs, and flat structs by value.
* Slices arrive as `{ uint8_t *data; int64_t len; }`. The layer declares that
  shape itself, and it must stay identical to the generated `sl_u8`.
* A struct that crosses is declared on both sides. It is packed on the Goose
  side, so the C side uses `#pragma pack(1)` and a static assert on the size.
* The layer never calls back into the Goose runtime. `gs_bld_append` is
  `static` in the generated C and unreachable from a library, so text comes
  back through caller-provided `u8` slices plus a returned length.
* Nothing owned by the C runtime crosses: no `FILE *`, and no allocation freed
  on the other side. On Windows a JIT program runs on TinyCC's `msvcrt.dll`,
  while `goose` has its own static CRT.
* Handles are flat structs such as `struct Texture { id: u32 }`. The layer
  keeps slot tables with a generation count, so a stale handle is an error,
  not a crash.
* Everything happens on the main thread. `tcc_run` already calls the
  program's `main` on the compiler's main thread, which macOS requires for
  windowing.

## Build

### 1. Static runtime on Windows (do first, commit alone)

In `CMakeLists.txt`, before any target and before any `add_subdirectory`:

```cmake
# One C runtime for the compiler, TinyCC, SDL and the gfx library, and the
# one `cl game.c` links by default, so a program can link the same archives.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
```

Things to know about this setting:

* **It is the non-debug variant in every configuration, deliberately.** A
  Debug `goose` built with `/MTd` would produce a `goose_gfx.lib` that a plain
  `cl game.c` (`/MT`) cannot link cleanly. `find_goose` also prefers Debug
  builds.
* **The cost:** MSVC's debug iterators and debug heap are gone from Debug
  builds. Asserts are unaffected, since those come from `NDEBUG`.
* **Why it propagates:** `cmake_minimum_required(VERSION 3.20)` gives policy
  CMP0091, and SDL requires 3.16, so SDL's targets inherit the variable.
  SDL's CMake adds `/NODEFAULTLIB` only to its shared target.
* **Verify:**
  * The full suite, JIT included, passes.
  * `dumpbin /imports goose.exe` no longer lists `vcruntime140.dll`,
    `msvcp140.dll` or `api-ms-win-crt-*`.
  * Benchmark numbers for the compiler itself are unaffected.

### 2. The submodule

```
[submodule "third_party/SDL"]
	path = third_party/SDL
	url = https://github.com/libsdl-org/SDL
	shallow = true
```

Details:

* **Pin:** tag `release-3.4.16`, commit
  `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c`.
* **Size:** a checkout of that tag is about 50 MB and 2,200 files; the full
  history is about 150 MB. `shallow = true` is honored by
  `git submodule update --init`.
* **README build section:** `git clone --recursive` now includes SDL. Add a
  line on opting out: clone without `--recursive`, then run
  `git submodule update --init third_party/tinycc`.

### 3. `cute_spirv` (always built, independent of SDL)

**The copy:**

* Copy `libraries/cute/cute_spirv.h` (v1.09) and `libraries/cute/ckit.h` from
  RandyGaul/cute_framework at commit
  `d7db751c67e723aea0724571ed22705641f06eac` (2026-09-15) into
  `third_party/cute_spirv/`.
* Add a README there recording that commit and the licenses. `cute_spirv.h`
  is zlib or public domain (the text is at the end of the file); `ckit.h` is
  public domain.

**Building it:**

* `src/shaderc.c` defines `CKIT_IMPLEMENTATION` and
  `CUTE_SPIRV_IMPLEMENTATION` and includes both headers.
* Build it as a C11 static library with warnings off (`/W0` / `-w`, as
  `libtcc.cmake` does for third-party code), linked into `goose`
  unconditionally. `project(goose CXX)` needs C enabled for this.
* It is always built because it is small, plain C with no dependencies, and
  because `--check` and `-o` must treat gfx programs identically with or
  without SDL.
* **Verify:** it compiles as C under MSVC, clang and gcc. cute_framework
  compiles its tests as C (`tools/cute_spirv_test.c`) with `C_STANDARD 23`, so
  C11 may need raising.

**What the API gives the compiler:**

* `cspv_compile_ex(source, stage, &opts)`, with `opts.include_resolve` for
  `#include`, `opts.defines`, `emit_hlsl` and `emit_msl`.
* It returns SPIR-V words, HLSL (Shader Model 5.1, entry `main`) and MSL
  (entry `main0`). Errors come back as a message with `file:line`.
* **Reflection:** samplers, storage images and storage buffers, each with a
  read-only flag. Uniform blocks come with their std140 sizes and members,
  vertex stages with their inputs, and compute stages with `local_size`.
* **Limits:** no doubles, no non-square matrices, no geometry or tessellation
  stages.

### 4. `cmake/gfx.cmake`

Gated the way TinyCC is:

```cmake
option(GOOSE_GFX "Build the SDL3 graphics module when the submodule is present" ON)
if(GOOSE_GFX AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt")
    include(cmake/gfx.cmake)
endif()
if(GOOSE_HAVE_GFX)
    target_link_libraries(goose PRIVATE goose_gfx)
    target_compile_definitions(goose PRIVATE GOOSE_HAVE_GFX=1
        GOOSE_GFX_LINK_PATH="${CMAKE_BINARY_DIR}/gfx")
endif()
```

What `gfx.cmake` does:

1. **Checks for a Linux video backend first.** SDL's configure does not fail
   when the X11 and Wayland development headers are missing; it quietly builds
   an SDL that cannot open a window.
   * Check with `find_package(X11)` and `pkg_check_modules(wayland-client)`.
   * If neither is found, print a status message saying what to install, and
     `return()` without setting `GOOSE_HAVE_GFX`. The opt-out path is then
     taken automatically.
   * The package list is `third_party/SDL/docs/README-linux.md` (the Ubuntu
     22.04+ line).
2. **SDL options, then `add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)`:**
   * `SDL_STATIC ON`, `SDL_SHARED OFF`, `SDL_TEST_LIBRARY OFF`. Tests,
     examples and install rules are already off when SDL is a subproject.
   * Leave `SDL_DEPS_SHARED` on. On Linux SDL then links only glibc and loads
     X11, Wayland, audio and udev at runtime, so `goose` gains no hard GUI
     dependencies.
   * Candidates to trim, each to verify: `SDL_CAMERA OFF` drops AVFoundation
     and CoreMedia on macOS. `SDL_OPENGL`/`SDL_OPENGLES OFF` would not affect
     SDL_GPU; check what `SDL_RENDER` then still needs.
   * Keep `SDL_VULKAN` on. SDL_GPU compiles its Vulkan backend only with
     `SDL_VIDEO_VULKAN`, Metal only with `SDL_VIDEO_METAL`, and D3D12 only when
     `dxgi1_6.h` is found.
3. **`add_library(goose_gfx STATIC src/gfx/*.c)`**, linking
   `SDL3::SDL3-static` publicly, with no interprocedural optimization. clang-cl
   must be able to link MSVC-built archives and the other way round.
4. **Windows:** delay-load the system DLLs static SDL adds to `goose`, so
   compiler runs that never open a window do not load them:

   ```cmake
   set(gfx_delayload user32 gdi32 winmm imm32 ole32 oleaut32 version setupapi shell32)
   ```

   Link `goose` with `/DELAYLOAD:<dll>` for each, plus `delayimp.lib`.
   * Take the final list from `dumpbin /imports goose.exe` once SDL is linked.
     The list above is what static SDL3 put into `lobster.exe`.
   * `advapi32` is loaded in every process anyway.
   * Measured on this machine, with test executables importing that set: 400
     launches each gave a median of 22.1 ms against 18.6 ms without the imports,
     and 18.5 ms delay-loaded. For scale, `goose --check` on the tour sample
     took 19.6 ms.
   * Delay-loading applies only to `goose`. Programs built with gfx open a
     window anyway.
5. **Link inputs for AOT programs.** Use `file(GENERATE)` to write
   `${CMAKE_BINARY_DIR}/gfx/link-msvc.rsp` and `link-cc.rsp`. Each holds the
   absolute paths of `goose_gfx` and `SDL3-static` (via
   `$<TARGET_LINKER_FILE:...>`) followed by the system libraries:
   * **Windows:** `kernel32 user32 gdi32 winmm imm32 ole32 oleaut32 version
     uuid advapi32 setupapi shell32`, plus `dinput8` if the joystick subsystem
     links it. This comes from SDL's `sdl_link_dependency(base ...)`.
   * **macOS:** the frameworks from SDL's `CMakeLists.txt`: Cocoa, Metal,
     QuartzCore, IOKit, CoreAudio, AudioToolbox, CoreVideo, CoreHaptics,
     GameController, ForceFeedback, Carbon, Foundation,
     UniformTypeIdentifiers (weak), and more.
   * **Linux:** `-ldl -lpthread -lm -lrt`.

   SDL's interface link libraries use `$<LINK_LIBRARY:FRAMEWORK,...>`, which is
   only valid in a link context, so the lists are maintained by hand. An AOT
   gfx test on each CI platform catches anything missing.

`goose` finds `GOOSE_GFX_LINK_PATH` the way `JitLibPath` finds `tcclib`: an
environment override first, then beside the binary, then the configure-time
path.

## Compiler changes (`src/gfx.h` plus small hooks)

1. **`have_gfx`** is a `constexpr` like `have_jit`. Stubs throw
   `CompileError { "this compiler was built without SDL3; check out
   third_party/SDL and reconfigure" }`.

2. **`embed_shader(path) -> u8[:]`** is a new builtin; the name is still open.
   * **Argument:** a string literal, resolved relative to the file containing
     the call, the way `import .x` resolves.
   * **Stage:** taken from the extension: `.vert`, `.frag` or `.comp`.
   * **Compilation:** during typechecking the compiler runs `cspv_compile_ex`
     with HLSL and MSL output enabled. `#include` resolves relative to the
     shader file.
   * **Errors:** a failure is a `CompileError` at the call's line, carrying the
     shader's own `file:line` message.
   * **Result:** a read-only view of static data, emitted by codegen like a
     string literal, holding the blob below.
   * **Independent of SDL:** this works without SDL, since `cute_spirv` is
     always built in.

3. **The blob** is laid out in `src/gfx/gfx_blob.h`, which the compiler and
   `goose_gfx` both include:

   ```c
   typedef struct {            /* little-endian, packed */
       char     magic[4];      /* "GSHD" */
       uint16_t version;
       uint8_t  stage;         /* 0 vertex, 1 fragment, 2 compute */
       uint8_t  pad;
       uint32_t local_size[3];
       uint16_t num_samplers, num_storage_textures_ro, num_storage_textures_rw,
                num_storage_buffers_ro, num_storage_buffers_rw, num_uniform_buffers;
       /* then: uniform block sizes, vertex inputs (location, type),
          and offset/length pairs for the SPIR-V, MSL and HLSL sections */
   } gs_gfx_blob_header;
   ```

   All three formats are always embedded, so a generated `.c` builds on any
   platform. The counts are what `SDL_GPUShaderCreateInfo` and
   `SDL_GPUComputePipelineCreateInfo` require.

4. **Codegen** sets `usesgfx` when a used extern's C name starts with `gs_gfx_`,
   as it sets `usesthreads` today. In `main.cpp`'s JIT branch:
   * if `!have_gfx`, throw the error from item 1;
   * otherwise call `RegisterGfxSymbols(state)` before `tcc_relocate`/`tcc_run`.
     That needs a hook in `RunJit`, such as a callback or a `bool gfx` parameter.

5. **Threads:** reaching a `gs_gfx_*` extern from a `thread_fn` is a compile
   error. The layer's tables and SDL_GPU are main-thread only. This is the
   recommendation; see the open questions.

6. **Byte views for uploads:** `bytes_of(a) -> u8[:]` already covers arrays,
   such as vertex data in a `Vertex[>..]`. A single uniform struct needs either
   a one-element array or an extension of `bytes_of` to flat values.
   Read-only, as today.

7. **Link inputs:** `goose --gfx-link msvc|cc` prints the path of the matching
   `.rsp`, and errors without gfx. `toolchain.CC.compile` needs a `libs=`
   argument placed after the sources, because GNU ld resolves static archives
   in order and `extra` currently goes before them.

## The C layer (`src/gfx/`, library `goose_gfx`)

It absorbs what makes SDL_GPU error-prone, so the Goose API does not inherit
it:

* **Device:** `SDL_CreateGPUDevice(SPIRV | DXBC | MSL, debug_mode, NULL)`,
  with debug mode on in Debug builds. SDL checks little on its own, so native
  validation layers remain the backstop.
* **Frames:** one command buffer per frame.
  `SDL_WaitAndAcquireGPUSwapchainTexture` can succeed and still return NULL
  (for example, while minimized). In that case skip drawing but still submit.
* **Uploads:** transfer buffers and a copy pass. Pass `cycle = true` only for
  resources rewritten every frame; cycling has caused multi-GB growth when
  misused.
* **Uniforms:** `SDL_PushGPU{Vertex,Fragment,Compute}UniformData`. There are 4
  slots per stage, each up to 32 KB.
* **Shaders:** pick the format from `SDL_GetGPUShaderFormats`.
  * SPIR-V: the words as they are.
  * MSL: the text, entry `main0`.
  * DXBC: `D3DCompile` on the HLSL, targets `vs_5_1`/`ps_5_1`/`cs_5_1`, entry
    `main`. The function comes from `LoadLibraryW(L"d3dcompiler_47.dll")`,
    which ships with Windows 10 and 11, so no SDK import library is needed.
    Cache the result per blob.
* **Errors:** functions return `false` or a zero handle. `gs_gfx_error(buf)`
  copies `SDL_GetError()` into a caller-provided slice.
* **Files:** split by area: device and window, resources, pipelines, compute.

## The Goose module (`stdlib/gfx.goose`)

`namespace gfx;`, with `extern "gs_gfx_..."` declarations, handle structs and
thin Goose wrappers. Version 1 is deliberately small, but covers everything
Lobster-class code needs to start:

* **Window and loop:** open a window; `frame()` pumps events and returns false
  on quit; keyboard and mouse state.
* **Buffers:** vertex, index and storage buffers from `bytes_of` views; updates.
* **Textures:** 2D, 3D and cube; upload; render targets with depth; samplers.
* **Pipelines:** graphics pipelines (shader blobs, vertex layout, blend, depth,
  cull) and compute pipelines.
* **Drawing:** clear, draw, draw indexed, instanced; set uniforms; bind
  textures and storage resources.
* **Compute:** dispatch.
* **Readback:** download buffers and textures.

A sketch of use, not compiled:

```goose
import gfx;
import vec;

struct Vertex { pos: float3, uv: float2 }

fn main() {
    guard gfx::open("demo", 1280, 720) else { return; }
    let lit = gfx::pipeline(embed_shader("lit.vert"), embed_shader("lit.frag"));
    var verts: Vertex[>..] = [];
    build_level(verts);
    let level = gfx::vertex_buffer(bytes_of(verts));
    while gfx::frame() { gfx::draw(lit, level, verts.len); }
}
```

Later, not version 1: audio, gamepads, mip generation, indirect draw, a 2D and
3D immediate-mode layer written in Goose, checking uniform structs against
reflection, and hot reload.

## Testing

* **The probe:** `toolchain.have_gfx(exe)` mirrors `have_jit`. It JIT-runs a
  one-line program printing `gfx::available()`, which proves the compiler has
  gfx built in.
* **`test/gfx/` compile-only tests** run everywhere, including the no-SDL
  sanitize job:
  * shader error tests, using the existing expected-error mechanism;
  * a `--check` of every gfx program;
  * a test that `gfx_api.h` and `stdlib/gfx.goose` declare the same set.
* **`test/gfx/` run tests** go through both the JIT and AOT.
  * They never create a swapchain: render into a texture and read it back,
    compare a checksum, and run a compute pass over a storage buffer.
  * If device creation fails (a runner without a GPU), report a skip, as
    `JIT_UNSUPPORTED` does.
  * **Verify:** does the offscreen video driver (`SDL_VIDEODRIVER=offscreen`)
    let the Vulkan backend initialize, or does it need no video driver at all?
* **AOT link:** linking with the `.rsp` files is itself a test on every
  platform.

## CI

* **Baseline job:** `submodules: true` already checks SDL out.
  * Ubuntu needs the SDL development packages from README-linux, plus
    `mesa-vulkan-drivers` for lavapipe (software Vulkan), so run tests have a
    device.
  * Windows and macOS runners may have no usable device (WARP, paravirtualized
    Metal); those run tests skip. **Verify** what actually happens there.
* **Sanitize job:** keep it without submodules. It then exercises the opt-out
  build on every push, and the compile-only gfx tests still run.
* **Build time:** measure what SDL adds per job, and cache the build directory
  if it matters.

## Milestones

Each one ends with the full suite green on all three platforms, then a commit.

1. Static MSVC runtime.
2. Copy `cute_spirv`; build it into `goose`; add a unit test or a hidden
   `--compile-shader` flag proving it runs.
3. SDL submodule, `cmake/gfx.cmake`, and a `goose_gfx` that is empty apart
   from `gs_gfx_available`.
   * Delay loading, link files and the probe.
   * Opt-out verified three ways: with the submodule, without it, and with
     `-DGOOSE_GFX=OFF`.
4. A window and clear-color loop end to end, in JIT and AOT on all three
   platforms.
5. `embed_shader`, the blob format, pipeline creation, and a triangle rendered
   into a texture and read back. This exercises the SPIR-V, MSL and DXBC paths.
6. Resources: buffers, textures, samplers, depth render targets and uniforms.
7. Compute pipelines, storage buffers, dispatch and readback.
8. Docs: replace this file with `docs/design/gfx.md`; update `docs/stdlib.md`,
   the README build section, `docs/implementation.md` and `docs/testing.md`.
   Add one sample.

## Open questions

* The builtin's name, and whether a stage is its own file or a section of a
  shared file, selected with a define.
* Whether gfx calls from a `thread_fn` should be a compile error (recommended)
  or allowed.
* How far to check Goose uniform structs (packed, `pad` for std140) against
  reflection in version 1: total size only, or member offsets.
* **macOS launch cost:** static SDL makes `goose` load about 15 frameworks at
  every start, and there is no delay-loading to rely on. Measure it. If it
  matters, link a second executable from the same objects (`goose` without
  SDL, `goose-gfx` with it), with `goose` handing gfx programs over.
* **Windows console window:** AOT programs are console applications for now.
  A gfx program may want `/SUBSYSTEM:WINDOWS`.
* **Mobile:** later, and AOT only. The generated C goes into SDL's Android
  Gradle and Xcode project templates.

## Reference

* **Lobster** already builds SDL3 statically through `add_subdirectory`, next
  to libtcc, and registers its runtime with `tcc_add_symbol`. See
  `C:\W\lobster\dev\CMakeLists.txt` and `dev/src/tccbind.cpp`.
* **TinyCC:** a symbol added with `tcc_add_symbol` is defined before relocation
  on every target; on PE it goes through the import table (`tccpe.c`).
  In-memory runs refuse thread-local storage, so gfx programs using threads
  stay AOT only, as today.
* **SDL_GPU shader formats:**
  * Vulkan takes SPIR-V.
  * D3D12 takes DXBC (Shader Model 5.1), or DXIL.
  * Metal takes MSL source or a metallib.
