// The Goose standard library: `gfx` (docs/stdlib.md), graphics on SDL3's GPU
// API: a window, input, buffers, textures, pipelines, render and compute
// passes, and reading results back. The functions are C, the layer in
// src/gfx/ over SDL_GPU, declared here as extern fns against the list in
// src/gfx/gfx_api.h; the test suite checks the two agree.
//
// Shaders are GLSL 450, compiled into the program by the builtin
// embed_shader when it is compiled, for every backend at once: SPIR-V for
// Vulkan, MSL for Metal, HLSL for Direct3D 12. A shader is written in the
// program after its stage, `embed_shader("frag", """ ... """)` (also "vert"
// and "comp"), or read from a file, `embed_shader("file.frag")`. It declares
// its resources where SDL_GPU expects them, which the compiler checks:
//
//   vertex    set 0: samplers, then storage textures, then storage buffers;
//             set 1: uniform blocks
//   fragment  set 2: samplers, then storage textures, then storage buffers;
//             set 3: uniform blocks
//   compute   set 0: samplers, read-only storage textures, read-only storage
//             buffers; set 1: read-write storage textures, then read-write
//             storage buffers; set 2: uniform blocks
//
// Bindings number each set from 0 in that order. The slot a bind_* call
// takes counts within one kind: the first storage buffer is slot 0 however
// many samplers come before it. Uniform blocks are laid out by std140, a
// Goose struct is packed: pad a struct pushed as uniforms to match (a vec3
// or vec4 member starts on a 16-byte boundary, a block's size rounds up to
// 16), which push_uniforms checks against the shader's block size.
//
// Clip space is SDL_GPU's on every backend: y up, depth from 0 to 1, and
// texture coordinates with (0, 0) at the top left. perspective() and
// look_at() below produce matrices for it.
//
// Errors: a call that can fail for reasons outside the program returns
// false or a zero handle, and error() says why. A call the program should
// not have made -- drawing with no pipeline bound, a released handle, a
// uniform block of the wrong size -- is reported as it happens and aborts
// the program at the next frame(), check() or read back.

namespace gfx;

import std;
import vec;
import math;

// --- constants: GS_GFX_CONSTANTS in src/gfx/gfx_api.h -----------------------

// open() and open_headless() flags.
let WINDOW_RESIZABLE: i32 = 1;
let WINDOW_HIDDEN: i32 = 2;
let WINDOW_FULLSCREEN: i32 = 4;
let WINDOW_HIGH_DPI: i32 = 8;
let NO_VSYNC: i32 = 16;
let DEBUG: i32 = 32;            // validation layers, where installed; on in a debug build of gfx

// Shader stages.
let VERTEX: i32 = 0;
let FRAGMENT: i32 = 1;
let COMPUTE: i32 = 2;

// Buffer usage, combined with |.
let BUFFER_VERTEX: i32 = 1;
let BUFFER_INDEX: i32 = 2;
let BUFFER_INDIRECT: i32 = 4;
let BUFFER_STORAGE: i32 = 8;          // read by vertex and fragment shaders
let BUFFER_COMPUTE_READ: i32 = 16;
let BUFFER_COMPUTE_WRITE: i32 = 32;

// Texture kinds.
let TEXTURE_2D: i32 = 0;
let TEXTURE_2D_ARRAY: i32 = 1;
let TEXTURE_3D: i32 = 2;
let TEXTURE_CUBE: i32 = 3;

// Texture usage, combined with |. A multisampled texture is a target only.
let SAMPLED: i32 = 1;
let COLOR_TARGET: i32 = 2;
let DEPTH_TARGET: i32 = 4;
let STORAGE_READ: i32 = 8;            // read as a storage texture by vertex and fragment shaders
let COMPUTE_READ: i32 = 16;
let COMPUTE_WRITE: i32 = 32;
let COMPUTE_READ_WRITE: i32 = 64;

// Texture formats. DEPTH is DEPTH32F where it can also be sampled, else
// DEPTH24.
let RGBA8: i32 = 1;
let BGRA8: i32 = 2;
let RGBA8_SRGB: i32 = 3;
let R8: i32 = 4;
let RG8: i32 = 5;
let RGBA16F: i32 = 6;
let RGBA32F: i32 = 7;
let R16F: i32 = 8;
let RG16F: i32 = 9;
let R32F: i32 = 10;
let RG32F: i32 = 11;
let R32UI: i32 = 12;
let RGBA8UI: i32 = 13;
let RGB10A2: i32 = 14;
let RG11B10F: i32 = 15;
let DEPTH16: i32 = 16;
let DEPTH24: i32 = 17;
let DEPTH32F: i32 = 18;
let DEPTH24_STENCIL8: i32 = 19;
let DEPTH: i32 = 20;

// Samplers.
let NEAREST: i32 = 0;
let LINEAR: i32 = 1;
let REPEAT: i32 = 0;
let MIRROR: i32 = 1;
let CLAMP: i32 = 2;
let COMPARE_NEVER: i32 = 1;
let COMPARE_LESS: i32 = 2;
let COMPARE_EQUAL: i32 = 3;
let COMPARE_LESS_EQUAL: i32 = 4;
let COMPARE_GREATER: i32 = 5;
let COMPARE_NOT_EQUAL: i32 = 6;
let COMPARE_GREATER_EQUAL: i32 = 7;
let COMPARE_ALWAYS: i32 = 8;

// Pipelines.
let TRIANGLES: i32 = 0;
let TRIANGLE_STRIP: i32 = 1;
let LINES: i32 = 2;
let LINE_STRIP: i32 = 3;
let POINTS: i32 = 4;
let CULL_NONE: i32 = 0;
let CULL_FRONT: i32 = 1;
let CULL_BACK: i32 = 2;
let BLEND_NONE: i32 = 0;
let BLEND_ALPHA: i32 = 1;
let BLEND_ADD: i32 = 2;
let BLEND_PREMULTIPLIED: i32 = 3;
let BLEND_MULTIPLY: i32 = 4;

// Vertex attribute formats, for PipelineDesc.formats: what a vertex buffer
// holds for a shader input when that is not the input's own type as 32-bit
// components (float for a vec, i32 for an ivec, u32 for a uvec).
let VERTEX_FLOAT: u8 = 1;
let VERTEX_FLOAT2: u8 = 2;
let VERTEX_FLOAT3: u8 = 3;
let VERTEX_FLOAT4: u8 = 4;
let VERTEX_INT: u8 = 5;
let VERTEX_INT2: u8 = 6;
let VERTEX_INT3: u8 = 7;
let VERTEX_INT4: u8 = 8;
let VERTEX_UINT: u8 = 9;
let VERTEX_UINT2: u8 = 10;
let VERTEX_UINT3: u8 = 11;
let VERTEX_UINT4: u8 = 12;
let VERTEX_UBYTE4_NORM: u8 = 13;      // four u8, read as floats from 0 to 1: colors
let VERTEX_BYTE4_NORM: u8 = 14;
let VERTEX_UBYTE4: u8 = 15;
let VERTEX_SHORT2: u8 = 16;
let VERTEX_SHORT4: u8 = 17;
let VERTEX_USHORT2_NORM: u8 = 18;
let VERTEX_USHORT4_NORM: u8 = 19;
let VERTEX_HALF2: u8 = 20;
let VERTEX_HALF4: u8 = 21;

// load_texture() flags.
let LOAD_MIPS: i32 = 1;
let LOAD_SRGB: i32 = 2;

// Mouse buttons.
let MOUSE_LEFT: i32 = 1;
let MOUSE_MIDDLE: i32 = 2;
let MOUSE_RIGHT: i32 = 3;

// --- handles and descriptions: the structs of src/gfx/gfx_api.h -------------

// A GPU object, by handle. A released handle is an error to use, not a
// crash; a zero handle is none.
struct Buffer { id: u32 }
struct Texture { id: u32 }
struct Sampler { id: u32 }
struct Pipeline { id: u32 }
struct ComputePipeline { id: u32 }

// Field defaults cannot name the constants above (a default may run before
// any global is initialized), so they spell the values, and say which.
struct TextureDesc {
    kind: i32 = 0,          // TEXTURE_2D
    format: i32 = 1,        // RGBA8
    usage: i32 = 1,         // SAMPLED
    width: i32,
    height: i32,
    depth: i32 = 1,         // a 3D texture's depth, or an array's layers
    mips: i32 = 1,          // 0: the full chain
    samples: i32 = 1,
}

// Part of one mip of one layer of a texture; w == 0 is the whole mip.
struct Region {
    mip: i32 = 0,
    layer: i32 = 0,         // array layer, or cube face (+x, -x, +y, -y, +z, -z)
    x: i32 = 0,
    y: i32 = 0,
    z: i32 = 0,
    w: i32 = 0,
    h: i32 = 0,
    d: i32 = 0,
}

struct SamplerDesc {
    min_filter: i32 = 1,    // LINEAR
    mag_filter: i32 = 1,
    mip_filter: i32 = 1,
    wrap_u: i32 = 0,        // REPEAT
    wrap_v: i32 = 0,
    wrap_w: i32 = 0,
    max_anisotropy: f32 = 1.0,
    compare: i32 = 0,       // a COMPARE_*, for a shadow sampler
}

// A pipeline is not tied to the formats it draws into: it is made for the
// targets of each pass it is bound in, the first time.
struct PipelineDesc {
    primitive: i32 = 0,     // TRIANGLES
    cull: i32 = 0,          // CULL_NONE
    clockwise: bool = false,        // front faces wind clockwise
    wireframe: bool = false,
    depth_test: bool = false,
    depth_write: bool = false,
    depth_compare: i32 = 2, // COMPARE_LESS
    blend: i32 = 0,         // BLEND_NONE
    // The vertex shader's inputs, in location order, read one after the
    // other from one packed element, the way a Goose struct lays out its
    // fields: from vertex buffer 0, and from this location on per instance
    // from vertex buffer 1. -1: nothing is per instance.
    instance_location: i32 = -1,
    depth_bias: f32 = 0.0,
    depth_bias_slope: f32 = 0.0,
    formats: u8[16] = [0; 16],      // by location, a VERTEX_* override
}

struct PassDesc {
    color: Texture[4] = [Texture { 0 }; 4],     // targets, up to the first zero handle
    resolve: Texture[4] = [Texture { 0 }; 4],   // where a multisampled target resolves to
    depth: Texture = Texture { 0 },
    layer: i32 = 0,                             // of the color targets: a face, a slice
    mip: i32 = 0,
    clear_color: bool = true,
    clear_depth: bool = true,
    color_value: float4 = float4 { 0.0, 0.0, 0.0, 1.0 },
    depth_value: f32 = 1.0,
}

// --- device, window and frames -----------------------------------------------

// Whether the gfx layer is linked in. A compiler without it refuses to run
// a gfx program in-process, so this is true wherever it runs.
extern "gs_gfx_available" fn available() -> bool;

// A window of width x height (in points: WINDOW_HIGH_DPI may give it more
// pixels) and the GPU device, or false with error() set.
extern "gs_gfx_open" fn open(title: const u8[:], width: i64, height: i64, flags: i64) -> bool;
fn open(title: const u8[:], width: i64, height: i64) -> bool { open(title, width, height, 0) }
// The device alone, drawing into a screen of width x height that no window
// shows: for tests, and for tools that only compute or render to files.
extern "gs_gfx_open_headless" fn open_headless(width: i64, height: i64, flags: i64) -> bool;
fn open_headless(width: i64, height: i64) -> bool { open_headless(width, height, 0) }
extern "gs_gfx_close" fn close_raw();
fn close() {
    check();
    close_raw();
}

extern "gs_gfx_error" fn error_into(out: u8[:]) -> i64;
extern "gs_gfx_misuse_count" fn misuse_count() -> i64;
// Why the last call that failed did, as text.
fn error() -> u8[>..] {
    var text: u8[>..] = [];
    text.resize(1024, 0);
    let n = error_into(text);
    text.resize(min(n, 1024));
    return text;
}
// Aborts, saying why, if the program has used gfx wrongly.
fn check() {
    if misuse_count() > 0 { abort(str("gfx: ", error())); }
}

extern "gs_gfx_driver" fn driver_into(out: u8[:]) -> i64;
// The GPU backend in use: "vulkan", "direct3d12" or "metal".
fn driver() -> u8[>..] {
    var name: u8[>..] = [];
    name.resize(64, 0);
    let n = driver_into(name);
    name.resize(min(n, 64));
    return name;
}

// Ends the frame drawn since the last call, showing the screen in the
// window, and handles the window's events: false once it has been asked to
// close, or quit() was called. Called at the top of the loop, before the
// first frame is drawn.
extern "gs_gfx_frame" fn frame_raw() -> bool;
fn frame() -> bool {
    let running = frame_raw();
    check();
    running
}
extern "gs_gfx_quit" fn quit();
// Submits everything drawn so far and waits until the GPU has done it.
extern "gs_gfx_flush" fn flush_raw();
fn flush() {
    flush_raw();
    check();
}
extern "gs_gfx_set_title" fn set_title(title: const u8[:]);

// The screen: what a frame draws into, an RGBA8 texture of the window's
// size in pixels, with a depth texture to go with it. Both keep their
// handles when the window resizes.
extern "gs_gfx_screen" fn screen() -> Texture;
extern "gs_gfx_screen_depth" fn screen_depth() -> Texture;
extern "gs_gfx_screen_size" fn screen_size_into(out: int2&);
fn screen_size() -> int2 {
    var size = int2_0;
    screen_size_into(size);
    size
}

extern "gs_gfx_time" fn time() -> f64;               // seconds since open
extern "gs_gfx_delta_time" fn delta_time() -> f64;   // seconds the last frame took
extern "gs_gfx_frame_count" fn frame_count() -> i64;

// --- input -------------------------------------------------------------------

// Keys by SDL's scancode names: "A", "1", "Space", "Return", "Escape",
// "Left", "Left Shift", "F1". Down now; pressed or released since the last
// frame().
extern "gs_gfx_key_down" fn key_down(name: const u8[:]) -> bool;
extern "gs_gfx_key_pressed" fn key_pressed(name: const u8[:]) -> bool;
extern "gs_gfx_key_released" fn key_released(name: const u8[:]) -> bool;
extern "gs_gfx_mouse_down" fn mouse_down(button: i64) -> bool;
extern "gs_gfx_mouse_pressed" fn mouse_pressed(button: i64) -> bool;
extern "gs_gfx_mouse_released" fn mouse_released(button: i64) -> bool;
extern "gs_gfx_mouse_pos" fn mouse_pos_into(out: float2&);
extern "gs_gfx_mouse_delta" fn mouse_delta_into(out: float2&);
extern "gs_gfx_mouse_wheel" fn mouse_wheel() -> f32;
fn mouse_pos() -> float2 {
    var p = float2_0;
    mouse_pos_into(p);
    p
}
fn mouse_delta() -> float2 {
    var d = float2_0;
    mouse_delta_into(d);
    d
}

// Input as if from the keyboard or mouse, seen at the next frame(): what a
// test drives a program with. A mouse button of 0 only moves the pointer.
extern "gs_gfx_inject_key" fn inject_key(name: const u8[:], down: bool) -> bool;
extern "gs_gfx_inject_mouse" fn inject_mouse(x: f32, y: f32, button: i64, down: bool);

// --- buffers -----------------------------------------------------------------

// A buffer of `size` bytes, holding `data` and zeros after it. Data comes
// from bytes_of: gfx::vertex_buffer(bytes_of(vertices)).
extern "gs_gfx_create_buffer" fn buffer(usage: i64, size: i64, data: const u8[:]) -> Buffer;
fn buffer(usage: i64, data: const u8[:]) -> Buffer { buffer(usage, data.len, data) }
fn vertex_buffer(data: const u8[:]) -> Buffer { buffer(BUFFER_VERTEX, data.len, data) }
fn index_buffer(data: const u8[:]) -> Buffer { buffer(BUFFER_INDEX, data.len, data) }
extern "gs_gfx_update_buffer" fn update_buffer(b: Buffer, offset: i64, data: const u8[:]) -> bool;
fn update_buffer(b: Buffer, data: const u8[:]) -> bool { update_buffer(b, 0, data) }
extern "gs_gfx_read_buffer" fn read_buffer_into(b: Buffer, offset: i64, out: u8[:]) -> bool;
extern "gs_gfx_buffer_size" fn buffer_size(b: Buffer) -> i64;
extern "gs_gfx_release_buffer" fn release(b: Buffer);

// Reads back `out.len` bytes from `offset`, once the GPU has done
// everything submitted before. Not inside a pass.
fn read_buffer(b: Buffer, offset: i64, out: u8[:]) -> bool {
    let ok = read_buffer_into(b, offset, out);
    check();
    ok
}
// Appends the buffer's first `count` elements to `out`, as the elements of
// out's type: gfx::read_buffer(results, floats, 256) into an f32[>..].
fn read_buffer<T>(b: Buffer, out: T[>..]&, count: i64) -> bool {
    var raw: u8[>..] = [];
    raw.resize(count * elem_size(out), 0);
    let ok = read_buffer(b, 0, raw);
    append_as(raw, out);
    ok
}

// --- textures and samplers -----------------------------------------------------

// The extern fns taking a description by reference are wrapped by ones
// taking it by value, which a struct literal can be passed to.
extern "gs_gfx_create_texture" fn create_texture(desc: const TextureDesc&) -> Texture;
fn texture(desc: TextureDesc) -> Texture { create_texture(desc) }
fn texture2d(width: i64, height: i64, format: i32, usage: i32) -> Texture {
    texture(TextureDesc { format: format, usage: usage, width: width as i32,
                          height: height as i32 })
}
fn texture2d(width: i64, height: i64) -> Texture { texture2d(width, height, RGBA8, SAMPLED) }
// A texture to draw into and then sample.
fn render_target(width: i64, height: i64, format: i32) -> Texture {
    texture2d(width, height, format, COLOR_TARGET | SAMPLED)
}
// A depth buffer that can also be sampled, as a shadow map is.
fn depth_target(width: i64, height: i64) -> Texture {
    texture2d(width, height, DEPTH, DEPTH_TARGET | SAMPLED)
}
fn texture3d(width: i64, height: i64, depth: i64, format: i32, usage: i32) -> Texture {
    texture(TextureDesc { kind: TEXTURE_3D, format: format, usage: usage, width: width as i32,
                          height: height as i32, depth: depth as i32 })
}
fn texture_cube(size: i64, format: i32, usage: i32) -> Texture {
    texture(TextureDesc { kind: TEXTURE_CUBE, format: format, usage: usage, width: size as i32,
                          height: size as i32 })
}
fn texture_array(width: i64, height: i64, layers: i64, format: i32, usage: i32) -> Texture {
    texture(TextureDesc { kind: TEXTURE_2D_ARRAY, format: format, usage: usage,
                          width: width as i32, height: height as i32, depth: layers as i32 })
}
// A PNG (or .bmp) as an RGBA8 texture, with its mips when flags has
// LOAD_MIPS.
extern "gs_gfx_load_texture" fn load_texture(path: const u8[:], flags: i64) -> Texture;

extern "gs_gfx_update_texture" fn update_texture_region(t: Texture, region: const Region&,
                                                        data: const u8[:]) -> bool;
fn update_texture(t: Texture, region: Region, data: const u8[:]) -> bool {
    update_texture_region(t, region, data)
}
// The whole of mip 0 of layer 0: a 2D texture, or all of a 3D one.
fn update_texture(t: Texture, data: const u8[:]) -> bool { update_texture(t, Region {}, data) }
extern "gs_gfx_read_texture" fn read_texture_into(t: Texture, region: const Region&,
                                                  out: u8[:]) -> bool;
fn read_texture(t: Texture, region: Region, out: u8[:]) -> bool {
    let ok = read_texture_into(t, region, out);
    check();
    ok
}
// Appends the whole of mip 0 of layer 0 to `out`, as the elements of out's
// type: u8 for the bytes of an RGBA8 texture, float4 for RGBA32F texels.
fn read_texture<T>(t: Texture, out: T[>..]&) -> bool {
    let info = texture_info(t);
    var raw: u8[>..] = [];
    raw.resize(texel_size(info.format) * info.width * info.height *
               (if info.kind == TEXTURE_3D { info.depth } else { 1 }), 0);
    let ok = read_texture(t, Region {}, raw);
    append_as(raw, out);
    ok
}
// The bytes of mip 0 of layer 0, fresh: an RGBA8 texture's pixels, four
// bytes each, from the top left.
fn read_pixels(t: Texture) -> u8[>..] {
    var px: u8[>..] = [];
    read_texture(t, px);
    return px;
}
// An RGBA8 or BGRA8 texture's mip 0 as a PNG file; the screen as it is
// shown, opaque.
extern "gs_gfx_save_png" fn save_png_raw(t: Texture, path: const u8[:]) -> bool;
fn save_png(t: Texture, path: const u8[:]) -> bool {
    let ok = save_png_raw(t, path);
    check();
    ok
}
fn screenshot(path: const u8[:]) -> bool { save_png(screen(), path) }
// Fills mips 1 and up from mip 0. Not inside a pass.
extern "gs_gfx_generate_mips" fn generate_mips(t: Texture);
extern "gs_gfx_texture_info" fn texture_info_into(t: Texture, out: TextureDesc&) -> bool;
fn texture_info(t: Texture) -> TextureDesc {
    var d = TextureDesc { width: 0, height: 0 };
    texture_info_into(t, d);
    d
}
extern "gs_gfx_release_texture" fn release(t: Texture);

// The bytes one texel of `format` takes.
fn texel_size(format: i32) -> i64 {
    if format == R8 { return 1; }
    if format == RG8 || format == R16F || format == DEPTH16 { return 2; }
    if format == RGBA16F || format == RG32F { return 8; }
    if format == RGBA32F { return 16; }
    return 4;
}

extern "gs_gfx_create_sampler" fn create_sampler(desc: const SamplerDesc&) -> Sampler;
fn sampler(desc: SamplerDesc) -> Sampler { create_sampler(desc) }
fn sampler(filter: i32, wrap: i32) -> Sampler {
    sampler(SamplerDesc { min_filter: filter, mag_filter: filter, mip_filter: filter,
                          wrap_u: wrap, wrap_v: wrap, wrap_w: wrap })
}
// For sampler2DShadow: a hardware depth comparison, filtered.
fn shadow_sampler() -> Sampler {
    sampler(SamplerDesc { wrap_u: CLAMP, wrap_v: CLAMP, wrap_w: CLAMP,
                          compare: COMPARE_LESS_EQUAL })
}
extern "gs_gfx_release_sampler" fn release(s: Sampler);

// --- pipelines ---------------------------------------------------------------

// vs and fs are embed_shader() results.
extern "gs_gfx_create_pipeline" fn create_pipeline(vs: const u8[:], fs: const u8[:],
                                                   desc: const PipelineDesc&) -> Pipeline;
fn pipeline(vs: const u8[:], fs: const u8[:], desc: PipelineDesc) -> Pipeline {
    create_pipeline(vs, fs, desc)
}
fn pipeline(vs: const u8[:], fs: const u8[:]) -> Pipeline { pipeline(vs, fs, PipelineDesc {}) }
extern "gs_gfx_release_pipeline" fn release(p: Pipeline);
extern "gs_gfx_create_compute_pipeline" fn compute_pipeline(cs: const u8[:]) -> ComputePipeline;
extern "gs_gfx_release_compute_pipeline" fn release(p: ComputePipeline);

// --- render passes and drawing -------------------------------------------------

// Draws go between begin_pass and end_pass, into the targets the pass
// names; binds last until the pass ends.
extern "gs_gfx_begin_pass" fn begin_pass_desc(desc: const PassDesc&) -> bool;
fn begin_pass(desc: PassDesc) -> bool { begin_pass_desc(desc) }
extern "gs_gfx_end_pass" fn end_pass();
// Into one target, with a depth target or Texture { 0 }, cleared.
fn begin_pass(target: Texture, depth: Texture, clear: float4) -> bool {
    var d = PassDesc { depth: depth, color_value: clear };
    d.color[0] = target;
    begin_pass_desc(d)
}
// Into the screen and its depth, cleared to `clear`.
fn begin_screen(clear: float4) -> bool { begin_pass(screen(), screen_depth(), clear) }

extern "gs_gfx_bind_pipeline" fn bind(p: Pipeline);
// Vertex buffer 0 per vertex, 1 per instance (PipelineDesc.instance_location).
extern "gs_gfx_bind_vertex_buffer" fn bind_vertex_buffer(slot: i64, b: Buffer, offset: i64);
fn bind_vertex_buffer(b: Buffer) { bind_vertex_buffer(0, b, 0) }
// Indices of index_size bytes: 2 for u16, 4 for u32.
extern "gs_gfx_bind_index_buffer" fn bind_index_buffer(b: Buffer, index_size: i64, offset: i64);
fn bind_index_buffer(b: Buffer, index_size: i64) { bind_index_buffer(b, index_size, 0) }
// A sampler binding of `stage`, in a render pass, or a compute pass for
// COMPUTE.
extern "gs_gfx_bind_texture" fn bind_texture(stage: i64, slot: i64, t: Texture, s: Sampler);
// Read-only storage resources of `stage`. A compute shader's read-write
// ones are given to begin_compute.
extern "gs_gfx_bind_storage_texture" fn bind_storage_texture(stage: i64, slot: i64, t: Texture);
extern "gs_gfx_bind_storage_buffer" fn bind_storage_buffer(stage: i64, slot: i64, b: Buffer);
// A uniform block's contents, for the draws or dispatches that follow, in
// or out of a pass.
extern "gs_gfx_push_uniforms" fn push_uniforms(stage: i64, slot: i64, data: const u8[:]);
// Any flat struct as the contents of a uniform block.
fn uniforms<T>(stage: i64, slot: i64, u: T) {
    let one: T[1] = [u];
    push_uniforms(stage, slot, bytes_of(one));
}
extern "gs_gfx_viewport" fn viewport(x: f32, y: f32, w: f32, h: f32);
// w == 0: the whole target.
extern "gs_gfx_scissor" fn scissor(x: i64, y: i64, w: i64, h: i64);

extern "gs_gfx_draw" fn draw(vertices: i64, instances: i64, first_vertex: i64,
                             first_instance: i64);
fn draw(vertices: i64) { draw(vertices, 1, 0, 0) }
fn draw_instanced(vertices: i64, instances: i64) { draw(vertices, instances, 0, 0) }
extern "gs_gfx_draw_indexed" fn draw_indexed(indices: i64, instances: i64, first_index: i64,
                                             vertex_offset: i64, first_instance: i64);
fn draw_indexed(indices: i64) { draw_indexed(indices, 1, 0, 0, 0) }
fn draw_indexed_instanced(indices: i64, instances: i64) { draw_indexed(indices, instances, 0, 0, 0) }

// --- compute -------------------------------------------------------------------

// A compute pass writing these buffers and textures: the shader's
// read-write storage resources, in binding order.
extern "gs_gfx_begin_compute" fn begin_compute(rw_buffers: const Buffer[:],
                                               rw_textures: const Texture[:]) -> bool;
fn begin_compute(rw_buffers: const Buffer[:]) -> bool {
    let none: Texture[>..] = [];
    begin_compute(rw_buffers, none)
}
extern "gs_gfx_end_compute" fn end_compute();
extern "gs_gfx_bind_compute_pipeline" fn bind(p: ComputePipeline);
// Runs x * y * z workgroups of the shader's local size.
extern "gs_gfx_dispatch" fn dispatch(x: i64, y: i64, z: i64);

// --- matrices ------------------------------------------------------------------

// A column-major 4x4 matrix, as GLSL's mat4 is laid out in a uniform block.
struct mat4 { c0: float4, c1: float4, c2: float4, c3: float4 }

let mat4_identity = mat4 { c0: float4 { 1.0, 0.0, 0.0, 0.0 }, c1: float4 { 0.0, 1.0, 0.0, 0.0 },
                           c2: float4 { 0.0, 0.0, 1.0, 0.0 }, c3: float4 { 0.0, 0.0, 0.0, 1.0 } };

fn row(m: mat4, i: i64) -> float4 {
    match i {
        0 => float4 { m.c0.x, m.c1.x, m.c2.x, m.c3.x },
        1 => float4 { m.c0.y, m.c1.y, m.c2.y, m.c3.y },
        2 => float4 { m.c0.z, m.c1.z, m.c2.z, m.c3.z },
        _ => float4 { m.c0.w, m.c1.w, m.c2.w, m.c3.w },
    }
}

fn mul(m: mat4, v: float4) -> float4 {
    float4 { dot(row(m, 0), v), dot(row(m, 1), v), dot(row(m, 2), v), dot(row(m, 3), v) }
}

fn mul(a: mat4, b: mat4) -> mat4 {
    mat4 { c0: mul(a, b.c0), c1: mul(a, b.c1), c2: mul(a, b.c2), c3: mul(a, b.c3) }
}

fn translate(t: float3) -> mat4 {
    var m = mat4_identity;
    m.c3 = float4 { t.x, t.y, t.z, 1.0 };
    m
}

fn scale(s: float3) -> mat4 {
    mat4 { c0: float4 { s.x, 0.0, 0.0, 0.0 }, c1: float4 { 0.0, s.y, 0.0, 0.0 },
           c2: float4 { 0.0, 0.0, s.z, 0.0 }, c3: float4 { 0.0, 0.0, 0.0, 1.0 } }
}

// A rotation of `angle` radians about `axis`, counterclockwise looking down
// the axis.
fn rotate(axis: float3, angle: f32) -> mat4 {
    let a = normalize(axis);
    let c = cos(angle);
    let s = sin(angle);
    let k = 1.0 - c;
    mat4 { c0: float4 { c + a.x * a.x * k, a.y * a.x * k + a.z * s, a.z * a.x * k - a.y * s, 0.0 },
           c1: float4 { a.x * a.y * k - a.z * s, c + a.y * a.y * k, a.z * a.y * k + a.x * s, 0.0 },
           c2: float4 { a.x * a.z * k + a.y * s, a.y * a.z * k - a.x * s, c + a.z * a.z * k, 0.0 },
           c3: float4 { 0.0, 0.0, 0.0, 1.0 } }
}

// Right-handed, looking down -z, to SDL_GPU's clip space with depth 0 at
// `near` and 1 at `far`.
fn perspective(fovy: f32, aspect: f32, near: f32, far: f32) -> mat4 {
    let f = 1.0 / tan(fovy * 0.5);
    mat4 { c0: float4 { f / aspect, 0.0, 0.0, 0.0 }, c1: float4 { 0.0, f, 0.0, 0.0 },
           c2: float4 { 0.0, 0.0, far / (near - far), -1.0 },
           c3: float4 { 0.0, 0.0, near * far / (near - far), 0.0 } }
}

fn ortho(left: f32, right: f32, bottom: f32, top: f32, near: f32, far: f32) -> mat4 {
    mat4 { c0: float4 { 2.0 / (right - left), 0.0, 0.0, 0.0 },
           c1: float4 { 0.0, 2.0 / (top - bottom), 0.0, 0.0 },
           c2: float4 { 0.0, 0.0, 1.0 / (near - far), 0.0 },
           c3: float4 { (left + right) / (left - right), (top + bottom) / (bottom - top),
                        near / (near - far), 1.0 } }
}

fn look_at(eye: float3, target: float3, up: float3) -> mat4 {
    let f = normalize(target - eye);
    let s = normalize(cross(f, up));
    let u = cross(s, f);
    mat4 { c0: float4 { s.x, u.x, -f.x, 0.0 }, c1: float4 { s.y, u.y, -f.y, 0.0 },
           c2: float4 { s.z, u.z, -f.z, 0.0 },
           c3: float4 { -dot(s, eye), -dot(u, eye), dot(f, eye), 1.0 } }
}

// --- helpers -------------------------------------------------------------------

// The bytes an element of `xs` takes: what bytes_of gives for one.
fn elem_size<T>(xs: T[>..]&) -> i64 {
    let one: T[1] = [default<T>()];
    bytes_of(one).len
}

// Appends bytes read back from the GPU to `out` as elements of its flat T,
// through from_bytes' image format: a LEB128 byte count, then the elements.
fn append_as<T>(raw: const u8[:], out: T[>..]&) {
    var image: u8[>..] = [];
    var n = raw.len as u64;
    loop {
        let low = (n & 127) as u8;
        n >>= 7;
        if n == 0 {
            image.push(low);
            break;
        }
        image.push(low | 128);
    }
    image.append(raw);
    var elems, ok = from_bytes<T[>..]>(image);
    assert(ok);
    out.append(elems);
}
