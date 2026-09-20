# The graphics module (`gfx`)

How the optional graphics module is put together, what was verified where,
and what is left. The reference for using it is `docs/stdlib.md`; the module
itself is `stdlib/gfx.goose`.

## What it is

`import gfx;` gives a Goose program a window, input, GPU buffers and textures,
graphics and compute pipelines, render and compute passes, and reading results
back. Underneath is SDL3's GPU API (SDL_GPU), which draws through Direct3D 12,
Vulkan or Metal. Shaders are GLSL, compiled into the program by the builtin
`embed_shader` when the program is compiled: written in it as a `"""` string
after their stage (`embed_shader("frag", """ ... """)`), or in a file
(`embed_shader("file.frag")`).

    Goose program --extern fn--> gs_gfx_* (goose_gfx, native C) --> SDL3 (static)

The layer between Goose and SDL, `goose_gfx` (`src/gfx/`), is a natively
compiled static library rather than a runtime header spliced into the
generated C:

* Neither the generated C nor TinyCC ever sees an SDL header. A JIT run only
  needs the layer's own functions defined for it with `tcc_add_symbol`: the
  64 of `src/gfx/gfx_api.h`, not SDL's 1,270.
* Code that needs platform headers, such as calling `D3DCompile`, stays in
  native code.
* JIT and AOT execute the same compiled layer: a JIT run gets the copy linked
  into `goose`, an AOT program links the same archive.

## How it is built

* **SDL3** comes from the submodule `third_party/SDL` (shallow, at
  `release-3.4.16`). `cmake/gfx.cmake` builds it static, without its 2D
  renderer, OpenGL or the camera, none of which SDL_GPU needs.
  `SDL_DEPS_SHARED` stays on, so on Linux SDL links only the C library and
  loads X11, Wayland, audio and udev at run time.
* **The static C runtime on Windows** (`CMAKE_MSVC_RUNTIME_LIBRARY
  MultiThreaded`, in every configuration): the compiler, TinyCC, SDL and the
  layer share the runtime a plain `cl game.c` links, so a program can link the
  same archives. `goose.exe` imports KERNEL32 and ADVAPI32 directly; the nine
  system DLLs SDL adds are delay-loaded, so a compiler run that opens no window
  does not load them. Compile times were unchanged.
* **`cute_spirv`** (`third_party/cute_spirv`, copied from cute_framework, see
  its README) compiles GLSL to SPIR-V and transpiles it to HLSL and MSL. It is
  built into every compiler, SDL or not, as `goose_shaderc` (`src/shaderc.c`).
* **Link inputs for AOT programs**: CMake writes
  `build/gfx/<config>/link-cc.rsp` (and `link-msvc.rsp` on Windows): the two
  archives, then the system libraries SDL records in its `SDL3-collector`
  target, the same list its pkg-config file is generated from. `goose
  --gfx-link msvc|cc` prints the path: `GOOSE_GFX_LINK` first, then a `gfx/`
  beside the binary, then the build tree. On Windows, cl and clang-cl both
  take `@link-msvc.rsp` among the sources, and gcc-style clang takes
  `@link-cc.rsp`.

**Opting out** is clean three ways, all verified: `-DGOOSE_GFX=OFF`, a checkout
without the SDL submodule, and a Linux machine without the X11 and Wayland
development files (where SDL's own configure would silently build an SDL that
cannot open a window, `gfx.cmake` says what to install and takes this path).
The compiler then builds and behaves as before: gfx programs typecheck and
generate the same C, shaders still compile, and only running one in-process
fails, with "this compiler was built without SDL3; check out third_party/SDL
and reconfigure". `--gfx-link` fails the same way.

## The compiler's side

* **`embed_shader`** is a builtin whose arguments are string literals, or
  `let` or `const` globals initialized with one, which the checker puts in
  their place. `embed_shader("frag", source, ...)` (`"vert"`, `"frag"` or
  `"comp"`) compiles GLSL written in the program, its parts joined as lines,
  with `#include` relative to the file containing the call;
  `embed_shader("x.vert")` (`.vert`, `.frag` or `.comp`) compiles a file,
  resolved relative to that file, with `#include` relative to the shader.
  The checker compiles each distinct shader once (`TypeCheck::EmbedShader`,
  `src/gfx.h`, `src/shaderc.c`), keeping the blobs in `Ast::shaders` and a
  pointer to its blob on the call for codegen. A failure is a compile error
  at the call. One the shader compiler puts at a line of source written in
  the program is reported at that line instead, since a `"""` string
  spanning lines holds its text line for line (`StrLit::multiline`); in a
  part a global names, the message adds which call compiled it. One in a
  file carries the shader's own `file:line`. The result is a `const u8[:]` into static
  data, like a string literal's, which codegen emits as an initializer list
  of bytes.
* **The blob** (`src/gfx/gfx_blob.h`, shared by `shaderc.c` and the layer)
  holds SPIR-V, MSL and HLSL together, so the generated C builds anywhere,
  plus the reflection SDL_GPU needs: counts of samplers, storage textures and
  storage buffers (read-only and read-write), the std140 size of each uniform
  block, the vertex inputs by location, and a compute shader's local size.
* **SDL_GPU's binding contract is checked at compile time.** Each resource must
  sit in the set SDL_GPU gives its kind and stage, numbered from 0 in the order
  samplers, storage textures, storage buffers. A resource in the wrong place
  works on no backend or on only some; here it is a compile error naming the
  rule (`test/gfx/gfx_err_shader_binding.goose`).
* **Calls into the layer** are recognized by their C symbol's `gs_gfx_` prefix.
  Codegen sets `usesgfx`, a JIT run of such a program registers the layer's
  functions before relocating it (`AddGfxSymbols`, `src/jit.h`), and a
  compiler without the layer refuses the run.
* **A `thread_fn` never reaches the layer**: SDL_GPU and windowing are
  main-thread only, and a thread program calling into gfx is a compile error.

## The layer (`src/gfx/`)

It absorbs what makes SDL_GPU error-prone, so the Goose API does not inherit
it. `gfx_api.h` lists every function once (`GS_GFX_API`) and every constant
(`GS_GFX_CONSTANTS`); `test/api_check.py` checks `stdlib/gfx.goose`
declares exactly those functions with the same C shapes, the same struct
fields in the same order, and the same constant values.

* **Handles** are one `u32`: a slot index and a generation, so a released
  handle is an error, not a crash.
* **Errors come in two kinds.** A failure outside the program's control (no
  GPU, a missing file) returns false or a zero handle, with the reason in
  `gfx::error()`. A call the program should not have made is a *misuse*: it is
  skipped, printed as it happens, counted, and the Goose side aborts with the
  reason at the next `frame()`, `check()`, read back or `close()`. SDL_GPU's
  own validation asserts are routed into the same path instead of SDL's
  dialog. Draws and dispatches check that every sampler, storage resource,
  uniform block and vertex buffer the bound pipeline's shaders read was
  provided, and a pushed uniform block must have the shader's std140 size.
* **The screen** is an RGBA8 texture with a depth texture, not the swapchain:
  a frame draws into it, and `frame()` blits it to the window. Pipelines then
  never depend on the swapchain's format, screenshots and readback of the
  screen are ordinary texture downloads, and a minimized window (no swapchain
  image) only skips the blit. A headless device (`open_headless`, or `open`
  under `GOOSE_GFX_HEADLESS=1`) has the same screen and no window.
* **Pipelines are made for the targets they draw into**, which their
  description does not name: each is created on first use in a pass with a
  new combination of color formats, depth format and sample count.
* **The vertex layout comes from the shader**: its inputs in location order,
  packed one after another as a Goose struct lays out its fields, per vertex
  from buffer 0, from `instance_location` on per instance from buffer 1.
  `PipelineDesc.formats` overrides a location's format, for bytes as colors.
* **Copies are in program order.** A new resource's first contents go through
  a command buffer of their own; any other upload, readback or mip generation
  is recorded in the frame's command buffer, and so happens between passes. A
  rewrite of a whole buffer lets SDL cycle it rather than wait for a draw still
  using it. Readback submits and waits.
* **Shaders**: the device takes SPIR-V where it can, else DXBC, else MSL. DXBC
  is the blob's HLSL compiled at load time by `d3dcompiler_47.dll`, which ships
  with Windows 10 and 11, loaded by name. Each SDL shader is made once per blob.
* **Debug mode** (SDL's validation, and the platform's validation layers where
  installed) is on in a debug build of the layer, with the `DEBUG` open flag,
  or `GOOSE_GFX_DEBUG=1`.

## The Goose module (`stdlib/gfx.goose`)

Namespace `gfx`: constants, the handle and description structs, the `extern`
declarations, and thin wrappers. Descriptions have defaults and are passed by
value to the wrappers, which pass them on by reference, so
`gfx::pipeline(vs, fs, gfx::PipelineDesc { depth_test: true })` works.
`uniforms(stage, slot, value)` pushes any flat struct; readback helpers infer
the element type from the array they append to (`read_buffer(b, floats, n)`),
and `read_pixels(t)` gives a texture's bytes. Column-major `mat4` helpers
(`perspective`, `ortho`, `look_at`, `rotate`, `mul`) produce matrices for
SDL_GPU's clip space. `docs/stdlib.md` is the reference.

## Testing

* **`test/gfx/`** in the regular suite: headless programs that render into
  textures and read them back (geometry, depth, instancing, storage-buffer
  vertex pulling, blending, MSAA resolve; 2D, 3D, cube and array textures,
  regions, mips, float texels, rendering into a cube face), compute (storage
  buffers, a storage texture, atomics, a compute shader sampling a texture),
  frames and injected input, and one runtime misuse aborting as designed. They
  draw only on pixel boundaries, so their output is exact on every backend.
  They build and run AOT and JIT where the compiler has the layer, and a
  machine without a GPU device reports them skipped (the program prints `gfx:
  no GPU device`). Their shaders are written in them. Beside them are a
  check that a shader from a file, written in the program and given in parts
  compiles to the same blob, the rejection tests (a shader that does not
  compile, whose error must land on the offending line of the program, also
  in a part a global holds; a binding in the wrong set; a missing file; an
  unknown extension or stage name; source given without a stage; a local
  variable as an argument; gfx from a `thread_fn`), probes of the hidden
  `--compile-shader` flag on the shader files kept for it, and the API check.
  The suite sets `GOOSE_GFX_HEADLESS=1`, so nothing it runs opens a window.
* **`samples/27_gfx_cube.goose`** runs in the samples runner, headless, for 30
  frames.
* **`test/gfx/window/`** is not in the suite, since it needs a display: a
  showcase program using a wide spread of the API in one scene (a shadow map,
  a multisampled HDR pass, instances and particles moved by compute shaders, a
  cube-map sky, 2D, 3D and array textures, an sRGB PNG round trip, wireframe,
  lines, blending, a viewport and scissor, tone mapping), checking itself
  through readbacks and saving screenshots. `run_window_test.py` runs it in
  every way the machine can and compares the screenshots across them.

### Verified where

| | Windows 11 (RTX 3080) | Linux (Ubuntu 24.04 under WSL2, WSLg) | macOS |
|---|---|---|---|
| Build, opt-out builds | yes | yes | not run |
| Suite, incl. `test/gfx/` | yes: MSVC, TinyCC | yes: gcc, TinyCC; sanitizer profile without SDL | not run |
| Showcase | JIT, cl, clang-cl, clang; Direct3D 12 and Vulkan | JIT, gcc, clang; Vulkan | not run |

On Windows the showcase's screenshots from Direct3D 12 and Vulkan differ by
0.65 of 255 per channel on average. On Linux with no display at all (the CI
runner's case, simulated by unsetting `DISPLAY` and `WAYLAND_DISPLAY`), SDL's
video initialization fails and `open_headless` falls back to SDL's offscreen
video driver, which gives the Vulkan backend what it needs: the `test/gfx/`
programs run there unchanged. Nothing on macOS has run: the MSL that
cute_spirv emits, SDL's Metal backend under this layer, the framework list
in `link-cc.rsp`, and TinyCC's arm64 calls into the layer are all unverified
until CI or a Mac runs them.

## Decisions on the plan's open questions

* The builtin is `embed_shader`, one shader per stage: its GLSL written in
  the program after the stage's name, or a file whose extension names it.
* gfx from a `thread_fn` is a compile error.
* Uniform structs are checked against the shader's block size, not member
  offsets.
* AOT programs stay console applications.
* The cost of static SDL on macOS compiler start-up, and whether `goose` should
  split off a `goose-gfx`, is still to be measured on a Mac.

## Follow-up work

* **macOS**: run the suite and the showcase; measure the start-up cost above.
* **Compiler bugs found on the way**, each with a workaround in the module or
  the tests: a fresh resizable result passed straight to an inlined
  function's slice parameter produces invalid C at `-O1`/`-O2` (bind it to a
  local first); and a struct field's default cannot name a global constant
  (the gfx descriptions spell their defaults as numbers).
* **API**: text input, gamepads and audio (SDL has them all), indirect draws,
  stencil, blend constants, debug labels, a `Region` for readback of depth
  textures (SDL_GPU cannot download one directly), checking uniform block
  member offsets rather than size alone, and readback forms that return a
  fresh array of an explicit element type (`read_buffer<f32>(b, n)`) beside
  the ones that append to an out-array.
* **A 2D and 3D immediate-mode layer in Goose** on top of this one, as
  Lobster has, and shader hot reload.
