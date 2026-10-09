# The ngfx module (`ngfx`)

This document describes the experimental ngfx module: what it is, what it costs, and
why it is shaped as it is. See [`docs/stdlib.md`](../stdlib.md#ngfx) for the API
overview and `stdlib/ngfx.goose` for the module source, whose header is the reference.

## What it is

`import ngfx;` gives a Goose program graphics on Sebastian Aaltonen's
[NoGraphicsAPI](https://github.com/sebbbi/NoGraphicsAPI) (`third_party/NoGraphicsAPI`), a
small C++ API over Metal 4 and Vulkan 1.4 with no bindings, no resource states, and GPU
pointers in place of descriptors. ngfx keeps gfx's platform half (window, input, time,
the screen, textures, read-back) and draws the way NoGraphicsAPI does: a draw takes the
GPU address of a root struct the program fills and shares with its shader, and shaders
fetch their own vertices through pointers.

    Goose program --extern fn--> gs_ngfx_* (goose_ngfx, C and C++) --> NoGraphicsAPI (static)

Shaders are Slang modules, compiled into the program at Goose compile time by the
`embed_slang` builtin. gfx's four larger programs are ported to it as the acceptance set:
`27_ngfx_cube`, `29_ngfx_ui_todo`, `30_ngfx_mini_minecraft`, and `31_ngfx_mini_doom`,
next to their originals, with the `test/gfx` fixtures ported to `test/ngfx`.

## Status

ngfx is tested on one machine: macOS 26 on an Apple M1 Pro (Metal 4). There, the build,
`test/run_tests.py`, and `samples/run_samples.py` pass, and run headless, the cube's,
mini Doom's, and the to-do list's screenshots are byte-identical to the gfx originals'.
Mini Minecraft's differ in 36 isolated pixels of nearest-sampled leaves, where Slang and
cute_spirv's MSL round a texture coordinate differently.

The Vulkan path is unverified:

| Platform | State |
| --- | --- |
| macOS 26, Metal 4 | Tested on an M1 Pro. |
| Windows, Vulkan 1.4 | Not yet built or run. Needs verification. |
| Linux, Vulkan 1.4 | Not built. NoGraphicsAPI is headless-only on Linux, and `cmake/ngfx.cmake` opts out there. Needs a little work to build headless. |

What a first Vulkan run has to check:

- `embed_slang`'s SPIR-V half. On macOS it is produced but never run, and `spirv-val`
  only runs where it is found.
- The HWND path in `src/ngfx/ngfx_platform.c`.
- `--ngfx-link msvc|cc`: `cmake/ngfx.cmake` adds `Vulkan::Vulkan` and no C++ runtime.
- `goose.exe` delay-loads `vulkan-1.dll`, so it should start on a machine without it.
- `RunTool`'s `cmd /c` quoting in `src/slangc.h`.
- On RADV (AMD on Linux), descriptor heaps may still need `RADV_EXPERIMENTAL=heap`.

## What upstream may not want

These choices follow NoGraphicsAPI rather than the rest of Goose. Each is deliberate,
and each is a reason this is a fork rather than a pull request as it stands.

- **A separate module, not a gfx backend.** NoGraphicsAPI needs Metal 4 or Vulkan 1.4 on
  recent GPUs and drivers, and cannot present on Linux. Under gfx it would either hold
  gfx's API to what SDL_GPU and NoGraphicsAPI can both do, or drop platforms gfx supports.
- **Slang, not GLSL.** NoGraphicsAPI's shader headers are Slang. `embed_slang` runs
  `slangc` (and Xcode's `metal` and `metallib` on macOS) as subprocesses, so compiling an
  ngfx program needs those tools installed, where `embed_shader` compiles in-process.
- **Y-down clip space** with depth from 0 to 1, NoGraphicsAPI's on both backends. 3D code
  using ngfx's `perspective`, `ortho`, and `look_at` sees no difference; 2D code that
  writes clip positions directly sees y flipped relative to gfx.
- **Row-major matrices** (`float4x4`), as NoGraphicsAPI's `shader_types.h` and its
  `-matrix-layout-row-major` build have them, so a struct shared with a shader means the
  same in C++, Slang, and Goose. gfx's `mat4` is column-major.
- **C++ in the native layer.** Goose's other layers are C. ngfx's GPU half is C++ because
  it uses NoGraphicsAPIUtility's `UploadQueue`, `DeleteQueue`, and allocators directly,
  which is what keeps its uploads synchronized exactly as NoGraphicsAPI's are. A program
  built from the generated C links the C++ runtime.
- **A high hardware bar**: macOS 26 and Metal 4, or Vulkan 1.4.357 with descriptor heaps,
  64-bit only, and CMake 3.24.
- **Duplication with gfx and ui.** The platform half of `src/ngfx/ngfx_platform.c` is a
  copy of `src/gfx/gfx_device.c`'s with `gs_ngfx_` names, and `stdlib/ngfx_ui.goose`
  repeats ui's 90-line input bridge. Sharing either would change gfx or ui.
- **Objective-C++ enabled for the whole build on Apple** in `CMakeLists.txt`, before SDL is
  added. NoGraphicsAPI's Metal backend is Objective-C++, and once the language exists, SDL
  links as Objective-C++ and fails unless its directory saw the language enabled.

## How it is built

The layer follows the native-layer conventions of `docs/design/physics.md`: packed C
structs, `let` constants, `gs_ngfx_` names, and one X-macro list (`GS_NGFX_API` in
`src/ngfx/ngfx_api.h`) that expands into the prototypes and the JIT symbol table, and
that `test/api_check.py` compares with the `extern` declarations. The hooks are the usual
ones: `--ngfx-link`, the main-thread check, the JIT's refusal without the layer, and the
test runners' `native` maps.

- `src/ngfx/ngfx_platform.c`: window, input, and time over SDL3, from gfx.
- `src/ngfx/ngfx_device.cpp`, `ngfx_resources.cpp`, `ngfx_draw.cpp`, `ngfx_gpu.hpp`: the GPU
  half over NoGraphicsAPI and NoGraphicsAPIUtility.
- `src/ngfx/ngfx_screen.slang`, `ngfx_mips.slang`: the layer's own shaders, compiled when
  the layer is built and embedded as byte arrays (`cmake/bin2h.cmake`).
- `cmake/ngfx.cmake` opts out with a `STATUS` message for each thing NoGraphicsAPI's own
  CMake would stop the configure on, and when the SDL submodule, `slangc`, or the
  backend's shader tools are missing. The compiler then builds as before.

### `embed_slang`

- **One blob holds a metallib and SPIR-V.** This keeps `embed_shader`'s property that the
  generated C is one self-contained file, which `--standalone` and the JIT rely on.
  SPIR-V is always built. The metallib is built only on macOS, where missing Xcode's
  Metal toolchain is a compile error; a program compiled elsewhere fails at run time on
  Metal, saying why.
- **Vertex, fragment, and compute entry points only.** NoGraphicsAPI also has mesh and
  task shaders; ngfx has no pipeline for them yet, so `embed_slang` rejects them.
- **One `slangc` run per target compiles the whole module**, every `[shader(...)]` entry
  point, and the blob shares the code between stages. Entry points, stages, and compute
  `numthreads` come from slangc's reflection JSON.
- **A subprocess, not libslang.** Linking would make the compiler depend on a library of
  about 100 MB. NoGraphicsAPI's own build runs `slangc` too.
- **One plain string literal is a path; a `"""` literal or several parts is source**, joined
  as lines with errors mapped back, as `embed_shader` does.
- **Tool paths are found at configure time**, overridable with `GOOSE_SLANGC`,
  `GOOSE_SPIRV_VAL`, and `GOOSE_NOGRAPHICSAPI`. The include directories are the source
  tree's, so a moved compiler needs `GOOSE_NOGRAPHICSAPI`.

## Decisions

### Surface

- **gfx's platform half, NoGraphicsAPI's drawing.** `open`, `frame`, input, events, the
  screen, textures, samplers, read-back, `screenshot`, and `release` keep gfx's names and
  behavior. Drawing takes roots and GPU addresses. gfx's API rebuilt over NoGraphicsAPI
  would recreate the binding model NoGraphicsAPI removes.
- **Gone, with no NoGraphicsAPI equivalent:** `bind_*`, `uniforms`,
  `begin_compute`/`end_compute`, vertex formats, buffer usage flags, primitive types other
  than triangles, wireframe, MSAA, and the `NO_VSYNC` and `DEBUG` flags.
- **Every texture gets a descriptor slot** in heaps the module owns. NoGraphicsAPI leaves
  heaps to the application, but gfx programs never managed them.
- **The screen is an offscreen RGBA8 texture**, copied to the swapchain by a fullscreen
  draw at `frame()`. NoGraphicsAPI gives the swapchain image only as a render target, and
  this is what makes `screenshot`, `read_pixels`, and headless mode possible. The copy
  does no sRGB conversion, as gfx's swapchain does.
- **Pipelines are created lazily per set of target formats**, as gfx does, because
  NoGraphicsAPI fixes formats in the pipeline.
- **The name is `ngfx`.** It reads as gfx on NoGraphicsAPI. `gfx2` would presume it
  replaces gfx, `gpu` is NoGraphicsAPI's C++ namespace, and `nogfx` reads as "no graphics".
- `CULL_BACK` with counterclockwise front faces is NoGraphicsAPI's `CullMode::clockwise`,
  as in its cube example. `COMPARE_*` keep gfx's values, and the layer converts.
- `GOOSE_GFX_HEADLESS` makes `ngfx::open` headless too, so the test runners need no second
  variable.

### Memory and submission

- **Memory is fixed-size**, suballocated with NoGraphicsAPIUtility's allocators: buffers
  from 64 MB chunks (larger ones get their own heap), textures from one 512 MB heap, 16 MB
  of frame memory per frame in flight (running out is a misuse), 8192 texture and 1024
  sampler descriptors. Releases wait in a `DeleteQueue`.
- **All GPU work runs on queue zero.** No program in the acceptance set needs async
  compute, and on M1 NoGraphicsAPI leaves open whether its queues are independent
  engines, so no gain could be measured. Adding it later extends the surface without
  changing it.
- **`update_buffer` takes effect for the whole frame** it is called in. Uploads go through
  `UploadQueue`, flushed before each submission of the frame's commands. Data that changes
  between draws goes in `frame_data`.
- **Texture initialization is submitted at once**, in its own command buffer, as
  NoGraphicsAPI's cube example does, so no upload can overtake it.
- **Every texture can be updated and read back** (`transfer_source |
  transfer_destination`), as in gfx. This may cost compression on some GPUs.

### Synchronization

- **The module records barriers only where it sees both sides; the program states the
  rest** with `ngfx::barrier(before, after)`. The module orders uploads, texture
  creation, read-back, the screen copy, each frame's first pass, and a pass drawing into
  a target an earlier pass drew into. It cannot see what shaders reach through roots,
  pointers, and descriptor indices, so it records nothing for that. Independent
  dispatches and passes wait for nothing, which is what NoGraphicsAPI exists for. Hidden
  conservative barriers at every pass and dispatch were tried first: they serialized
  independent work and still missed hazards.
- **Barrier points are packed `i32` constants**: a NoGraphicsAPI stage mask in bits 0 to 11
  and an access mask in bits 16 to 27, with NoGraphicsAPI's own bit values, combined with
  `|`. Six common points are named (`COMPUTE_WRITE`, `VERTEX_READ`, and so on). A barrier
  inside a pass is a misuse.
- **`GOOSE_NGFX_SYNC=full` puts a full barrier around every pass and dispatch.** A program
  whose output changes under it is missing a barrier. `run_tests.py` runs every
  `test/ngfx` fixture both ways. No validation layer would catch a missing barrier
  through device addresses, so this check is the safety net.
- **`generate_mips` is Goose code in `stdlib/ngfx.goose`**: a compute dispatch per level,
  reading the level above through the sampled view and writing through a per-level
  storage view (`storage_index(t, mip)`), with a barrier between levels. NoGraphicsAPI
  has no mip generation and leaves it to applications. It supports 2D textures with
  `SAMPLED | STORAGE` and not sRGB formats, which neither backend allows as storage.

### Ports

- **Each port changes only what drawing needs** and prints what the original prints.
- **Data rebuilt every frame moves to `frame_data`**; data that persists stays in buffers.
- **One root per Slang module** (`GPU_ROOT` binds slot 0), so a program whose pipelines
  take different roots embeds one module per root, sharing Slang through a `let` global.
- **ui draws through ngfx in a companion module, `stdlib/ngfx_ui.goose`**, not in `ui`.
  Putting it in `ui` would make every ui program compile ngfx, and either way the code
  moves again if ngfx folds into gfx. ui's atlas texture ids are one namespace shared by
  both renderers, which is safe only because a program uses one.

## Goose constraints met on the way

- **The stdlib keeps no `embed_slang`.** Every non-generic function of an imported module
  is type-checked, so an `embed_slang` in `stdlib/ngfx.goose` would run `slangc` and
  Xcode's tools at every compile of every program importing ngfx: 0.68 s against 0.01 s.
  The layer's shaders are built with the layer instead.
- **`embed_slang` parts must be literals or `let` globals**, as `embed_shader`'s are.
- **An entry point named like an MSL type fails on Metal only.** `half` compiles to SPIR-V,
  then breaks Xcode's Metal compiler; Slang does not rename it.
- **Goose structs are packed; Slang structs have C layout.** A root spells C's padding
  with `pad n`: a `u64` pointer on an 8-byte boundary, `float3` as 12 bytes aligned to 4.
- **By value, a packed Goose struct stands in for a naturally aligned C one** when the
  bytes match and the struct is larger than 16 bytes or integer-only. A layout test of
  an earlier one-to-one binding checked this on arm64; the rule is unverified elsewhere.

## Open

- Verify on Vulkan: Windows with an NVIDIA GPU first, then Linux headless on AMD, which
  needs `cmake/ngfx.cmake` to build there (see [Status](#status)).
- Uploads on a copy queue, waiting on `UploadQueue::flush()`'s timeline point, would stop
  a frame with uploads from serializing against the previous frame. It needs no surface
  change, and waits for a machine whose queues are independent engines.
- Mipmapped sRGB textures: a UNORM texture with `mutable_format`, an sRGB sampled view,
  and a UNORM storage view, which may cost compression.
- Per-pass barrier hints (`PassDesc.after`), if ports keep writing `barrier(); begin_pass()`.
