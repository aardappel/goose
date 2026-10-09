// The Goose standard library: `ngfx` (docs/stdlib.md), graphics on
// NoGraphicsAPI (third_party/NoGraphicsAPI): gfx's window, input, time,
// screen, textures and read-back, with NoGraphicsAPI's way of drawing. The
// functions are C, the layer in src/ngfx/, declared here as extern fns
// against the list in src/ngfx/ngfx_api.h; the test suite checks the two
// agree. docs/design/ngfx.md says why it is shaped as it is.
//
// Drawing passes data to shaders as memory, not bindings. A draw takes the
// GPU address of a root: a struct the program fills, declared again in the
// shader, which reads it with GPU_ROOT. The root holds what the shader
// needs: GPU addresses of buffers (address(b)), which it reads as pointers,
// fetching its own vertices; indices of textures and samplers (index(t),
// index(s)); and constants. root(value) copies a root into this frame's
// memory and gives its address; frame_data(bytes) does the same for any
// other data that lives one frame.
//
//   struct Root { vertices: u64, transform: ngfx::float4x4 }
//   let root = ngfx::root(Root { vertices: ngfx::address(vertex_buffer), transform: m });
//   ngfx::draw(root, vertex_count);
//
// A Goose struct is packed and a Slang one has C's layout, so pad the Goose
// struct where C would: a u64 (a pointer) on an 8-byte boundary.
//
// Shaders are Slang modules, compiled into the program by the builtin
// embed_slang when it is compiled, for both backends: a metallib for Metal
// and SPIR-V for Vulkan. One module holds every entry point, each marked
// [shader("vertex")], [shader("fragment")] or [shader("compute")]; it is
// written in the program, `embed_slang("""...""")`, or read from a file,
// `embed_slang("file.slang")`, and starts with
// `#include <NoGraphicsAPI/shader.slang>`.
//
// Clip space is NoGraphicsAPI's on both backends: y down, depth from 0 to 1,
// and texture coordinates with (0, 0) at the top left. perspective(),
// ortho() and look_at() below produce matrices for it, as row-major
// float4x4s, which Slang reads as its float4x4: mul(root.transform, v).
//
// Synchronization is split by who can see both sides of a hazard. The
// layer orders the work it records itself: uploads (NoGraphicsAPI's
// UploadQueue, which reaches the GPU before the frame's work, so
// update_buffer takes effect for the whole frame it is called in), texture
// creation, read-back, the copy of the screen to the window, each frame's
// first pass after the last frame, and a pass drawing into a target an
// earlier pass drew into. What shaders read and write through roots, the
// layer cannot see, so the program states it, between passes and
// dispatches:
//
//   ngfx::dispatch(...);                                       // writes a buffer
//   ngfx::barrier(ngfx::COMPUTE_WRITE, ngfx::VERTEX_READ);     // which the pass reads
//   ngfx::begin_screen(...);
//
// and nothing else is recorded: dispatches writing separate memory, and
// passes drawing into separate targets, wait for nothing. A missing barrier
// is a race, as in NoGraphicsAPI; GOOSE_NGFX_SYNC=full in the environment
// puts a full barrier around every pass and dispatch, so a program whose
// output changes under it is missing one.
//
// Errors: a call that can fail for reasons outside the program returns
// false or a zero handle, and error() says why. A call the program should
// not have made is reported as it happens and aborts the program at the
// next frame(), check() or read back.

namespace ngfx;

import std;
import vec;
import math;

// --- constants: GS_NGFX_CONSTANTS in src/ngfx/ngfx_api.h ---------------------

// open() and open_headless() flags.
let WINDOW_RESIZABLE: i32 = 1;
let WINDOW_HIDDEN: i32 = 2;
let WINDOW_FULLSCREEN: i32 = 4;

// Texture kinds.
let TEXTURE_2D: i32 = 0;
let TEXTURE_2D_ARRAY: i32 = 1;
let TEXTURE_3D: i32 = 2;
let TEXTURE_CUBE: i32 = 3;

// Texture usage, combined with |. Every texture can also be updated and
// read back.
let SAMPLED: i32 = 1;
let COLOR_TARGET: i32 = 2;
let DEPTH_TARGET: i32 = 4;
let STORAGE: i32 = 8;                 // read and written by shaders, through storage_index

// Texture formats. DEPTH is DEPTH32F.
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

// Pipelines. NoGraphicsAPI draws triangle lists only.
let CULL_NONE: i32 = 0;
let CULL_FRONT: i32 = 1;
let CULL_BACK: i32 = 2;
let BLEND_NONE: i32 = 0;
let BLEND_ALPHA: i32 = 1;
let BLEND_ADD: i32 = 2;
let BLEND_PREMULTIPLIED: i32 = 3;
let BLEND_MULTIPLY: i32 = 4;

// load_texture() flags.
let LOAD_SRGB: i32 = 2;

// Barrier points, for barrier(): a STAGE_* | ACCESS_* combination, each with
// NoGraphicsAPI's Stage and Access meaning, combined with |. A stage stands
// for itself and, on the before side, the stages logically before it, on
// the after side those after it (NoGraphicsAPI.hpp's Stage).
let STAGE_INDIRECT: i32 = 1;
let STAGE_INDEX_INPUT: i32 = 2;
let STAGE_VERTEX: i32 = 4;
let STAGE_TASK: i32 = 8;
let STAGE_MESH: i32 = 16;
let STAGE_DEPTH: i32 = 32;            // depth and stencil tests
let STAGE_FRAGMENT: i32 = 64;
let STAGE_COLOR_OUTPUT: i32 = 128;
let STAGE_COMPUTE: i32 = 256;
let STAGE_TRANSFER: i32 = 512;
let STAGE_ALL: i32 = 2048;
let ACCESS_TRANSFER_READ: i32 = 65536;
let ACCESS_TRANSFER_WRITE: i32 = 131072;
let ACCESS_SHADER_READ: i32 = 262144;
let ACCESS_SHADER_WRITE: i32 = 524288;
let ACCESS_COLOR_READ: i32 = 1048576;
let ACCESS_COLOR_WRITE: i32 = 2097152;
let ACCESS_DEPTH_READ: i32 = 4194304;
let ACCESS_DEPTH_WRITE: i32 = 8388608;
let ACCESS_INDIRECT_READ: i32 = 16777216;
let ACCESS_INDEX_READ: i32 = 33554432;
let ACCESS_DESCRIPTOR_READ: i32 = 134217728;
// The usual ones, named.
let COMPUTE_WRITE: i32 = 524544;      // STAGE_COMPUTE | ACCESS_SHADER_WRITE
let COMPUTE_READ: i32 = 262400;       // STAGE_COMPUTE | ACCESS_SHADER_READ
let VERTEX_READ: i32 = 262148;        // STAGE_VERTEX | ACCESS_SHADER_READ
let FRAGMENT_READ: i32 = 262208;      // STAGE_FRAGMENT | ACCESS_SHADER_READ
let TARGET_WRITE: i32 = 10485920;     // a pass's color and depth writes
let INDEX_READ: i32 = 33554434;       // STAGE_INDEX_INPUT | ACCESS_INDEX_READ

// Mouse buttons.
let MOUSE_LEFT: i32 = 1;
let MOUSE_MIDDLE: i32 = 2;
let MOUSE_RIGHT: i32 = 3;
let MOUSE_X1: i32 = 4;
let MOUSE_X2: i32 = 5;

// Input events (Event.kind), and the modifier keys held with a key.
let EVENT_KEY: i32 = 1;
let EVENT_TEXT: i32 = 2;
let EVENT_MOUSE_MOTION: i32 = 3;
let EVENT_MOUSE_BUTTON: i32 = 4;
let EVENT_MOUSE_WHEEL: i32 = 5;
let EVENT_FOCUS_GAINED: i32 = 6;
let EVENT_FOCUS_LOST: i32 = 7;
let MOD_SHIFT: i32 = 1;
let MOD_CTRL: i32 = 2;
let MOD_ALT: i32 = 4;
let MOD_GUI: i32 = 8;

// --- handles and descriptions: the structs of src/ngfx/ngfx_api.h -----------

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
    max_anisotropy: f32 = 1.0,      // more than 1: NoGraphicsAPI's fixed 4x
    compare: i32 = 0,       // a COMPARE_*, for a shadow sampler
}

// What a pipeline adds to its shaders. It is not tied to the formats it
// draws into: it is made for the targets of each pass it is bound in, the
// first time.
struct PipelineDesc {
    cull: i32 = 0,          // CULL_NONE
    clockwise: bool = false,        // front faces wind clockwise on the screen
    depth_test: bool = false,
    depth_write: bool = false,
    depth_compare: i32 = 2, // COMPARE_LESS
    blend: i32 = 0,         // BLEND_NONE
    depth_bias: f32 = 0.0,
    depth_bias_slope: f32 = 0.0,
}

// One input event of the last frame. The fields a kind has are the ones
// that mean something for it.
struct Event {
    kind: i32,              // EVENT_*
    scancode: i32,          // key: where it is on the keyboard (key_name names it)
    keycode: i32,           // key: what it means in the keyboard's layout: a character, for most
    mods: i32,              // key: the MOD_* held down with it
    down: bool,             // key, button: pressed rather than released
    repeat: bool,           // key: repeated by being held down
    button: i32,            // button: MOUSE_*
    clicks: i32,            // button: 1, 2 for a double click, ...
    codepoint: u32,         // text: one character typed
    x: f32,                 // motion, button: where the pointer is, in pixels; wheel: how far
    y: f32,
}

struct PassDesc {
    color: Texture[4] = [Texture { 0 }; 4],     // targets, up to the first zero handle
    depth: Texture = Texture { 0 },
    layer: i32 = 0,                             // of the targets: a face, a slice
    mip: i32 = 0,
    clear_color: bool = true,
    clear_depth: bool = true,
    color_value: float4 = float4 { 0.0, 0.0, 0.0, 1.0 },
    depth_value: f32 = 1.0,
}

// --- device, window and frames -----------------------------------------------

// Whether the ngfx layer is linked in. A compiler without it refuses to run
// an ngfx program in-process, so this is true wherever it runs.
extern "gs_ngfx_available" fn available() -> bool;

// A window of width x height in the display's own coordinates, and the GPU
// device, or false with error() set. The screen has every pixel of the
// window, which a high-density display has more of: screen_size() says how
// many.
extern "gs_ngfx_open" fn open(title: const u8[:], width: i64, height: i64, flags: i64) -> bool;
fn open(title: const u8[:], width: i64, height: i64) -> bool { open(title, width, height, 0) }
// The device alone, drawing into a screen of width x height that no window
// shows: for tests, and for tools that only compute or render to files.
extern "gs_ngfx_open_headless" fn open_headless(width: i64, height: i64, flags: i64) -> bool;
fn open_headless(width: i64, height: i64) -> bool { open_headless(width, height, 0) }
extern "gs_ngfx_close" fn close_raw();
fn close() {
    check();
    close_raw();
}

extern "gs_ngfx_error" fn error_into(out: u8[:]) -> i64;
extern "gs_ngfx_misuse_count" fn misuse_count() -> i64;
// Why the last call that failed did, as text.
fn error() -> u8[>..] {
    var text: u8[>..] = [];
    text.resize(1024, 0);
    let n = error_into(text);
    text.resize(min(n, 1024));
    return text;
}
// Aborts, saying why, if the program has used ngfx wrongly.
fn check() {
    if misuse_count() > 0 { abort(str("ngfx: ", error())); }
}

extern "gs_ngfx_driver" fn driver_into(out: u8[:]) -> i64;
// NoGraphicsAPI's backend: "metal" or "vulkan".
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
extern "gs_ngfx_frame" fn frame_raw() -> bool;
fn frame() -> bool {
    let running = frame_raw();
    check();
    running
}
extern "gs_ngfx_quit" fn quit();
// Submits everything drawn so far and waits until the GPU has done it.
extern "gs_ngfx_flush" fn flush_raw();
fn flush() {
    flush_raw();
    check();
}
extern "gs_ngfx_set_title" fn set_title(title: const u8[:]);

// The screen: what a frame draws into, an RGBA8 texture of the window's
// size in pixels, with a depth texture to go with it. Both keep their
// handles when the window resizes.
extern "gs_ngfx_screen" fn screen() -> Texture;
extern "gs_ngfx_screen_depth" fn screen_depth() -> Texture;
extern "gs_ngfx_screen_size" fn screen_size_into(out: int2&);
fn screen_size() -> int2 {
    var size = int2_0;
    screen_size_into(size);
    size
}

extern "gs_ngfx_time" fn time() -> f64;               // seconds since open
extern "gs_ngfx_delta_time" fn delta_time() -> f64;   // seconds the last frame took
extern "gs_ngfx_frame_count" fn frame_count() -> i64;
// How many times ngfx has been opened in this process: handles made before it
// last changed were released by close().
extern "gs_ngfx_open_count" fn open_count() -> i64;
// --- input -------------------------------------------------------------------

// Keys by SDL's scancode names: "A", "1", "Space", "Return", "Escape",
// "Left", "Left Shift", "F1". The ones SDL names after a platform's labels
// go by any of them everywhere: "Left Alt" or "Left Option", "Left GUI",
// "Left Command" or "Left Windows", "Application". Down now; pressed or
// released by the events the latest frame() collected. A press and release
// between frames makes both true, even though down is false. Repeated events
// that leave the held state unchanged do not count as presses or releases.
// These queries do not consume input; the next frame() clears the edges.
// The mouse is in the screen's pixels, which on a high-density display
// outnumber the window's own coordinates.
extern "gs_ngfx_key_down" fn key_down(name: const u8[:]) -> bool;
extern "gs_ngfx_key_pressed" fn key_pressed(name: const u8[:]) -> bool;
extern "gs_ngfx_key_released" fn key_released(name: const u8[:]) -> bool;
extern "gs_ngfx_mouse_down" fn mouse_down(button: i64) -> bool;
extern "gs_ngfx_mouse_pressed" fn mouse_pressed(button: i64) -> bool;
extern "gs_ngfx_mouse_released" fn mouse_released(button: i64) -> bool;
extern "gs_ngfx_mouse_pos" fn mouse_pos_into(out: float2&);
extern "gs_ngfx_mouse_delta" fn mouse_delta_into(out: float2&);
extern "gs_ngfx_mouse_wheel" fn mouse_wheel() -> f32;
// Whether the mouse is over the window, or holds a button pressed in it,
// as SDL's mouse focus says: for a window opened under the mouse, from its
// first move at the latest. mouse_pos is the mouse's place from open on,
// and where it left the window while it is outside. Headless, whether the last
// position injected is on the screen; an injected move decides it in a
// window too.
extern "gs_ngfx_mouse_in_window" fn mouse_in_window() -> bool;
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

// Keyboard focus, updated at frame(). Losing focus clears held keys and
// buttons and motion/wheel deltas, releases relative mode, and restores a
// visible cursor. Input while unfocused is ignored. Focus changes are also
// EVENT_FOCUS_GAINED / EVENT_FOCUS_LOST in events(), with no payload fields.
// Held keys/buttons get released edges and up events before EVENT_FOCUS_LOST.
// Headless, focus starts true and inject_focus can change it for tests.
extern "gs_ngfx_focused" fn focused() -> bool;

// First-person mouse look: hide the cursor, confine it to the window, and
// keep mouse_delta() moving past the window edges. Use deltas, not mouse_pos,
// for turning. False on failure (including no focus); error() says why.
// Changing modes discards pending motion and this frame's delta, to avoid
// a camera jump. Setting the current mode again leaves motion alone.
// Focus gain does not recapture: call again when the player resumes.
// Headless, these keep the same state without touching the system mouse.
extern "gs_ngfx_set_mouse_relative" fn set_mouse_relative(on: bool) -> bool;
extern "gs_ngfx_mouse_relative" fn mouse_relative() -> bool;

// Visibility outside relative mode; useful when drawing your own cursor.
// Relative mode always hides it, then restores this preference when disabled.
// Focus loss resets the preference to visible. Hiding without focus fails.
// cursor_visible reports the effective visibility within the ngfx window;
// headless it reports the simulated state. close() restores the system cursor.
extern "gs_ngfx_set_cursor_visible" fn set_cursor_visible(visible: bool) -> bool;
extern "gs_ngfx_cursor_visible" fn cursor_visible() -> bool;

// Input as if from the keyboard or mouse, seen at the next frame(): what a
// test drives a program with. The mouse goes to pixel x, y; a button of 0
// only moves it.
extern "gs_ngfx_inject_key" fn inject_key(name: const u8[:], down: bool) -> bool;
extern "gs_ngfx_inject_mouse" fn inject_mouse(x: f32, y: f32, button: i64, down: bool);
// Text as if typed: an EVENT_TEXT per character at the next frame().
extern "gs_ngfx_inject_text" fn inject_text(text: const u8[:]) -> bool;
// A focus change at the next frame(), in order with other injected input.
// Does not move operating-system focus; intended for headless tests.
extern "gs_ngfx_inject_focus" fn inject_focus(focused: bool) -> bool;

// The last frame's input in the order it came, key repeats and typed text
// included: what text editing needs, where the state above loses what
// happened between two frames.
extern "gs_ngfx_events" fn events_into(out: Event[:]) -> i64;
fn events() -> Event[>..] {
    var out: Event[>..] = [];
    let n = events_into(out);
    out.resize(n, default<Event>());
    events_into(out);
    return out;
}
// Typed text comes as EVENT_TEXT only while text input is on: off by
// default, since on some platforms it shows an on-screen keyboard.
extern "gs_ngfx_text_input" fn text_input(on: bool);
// The scancode of the key of that name, as key_down takes it, 0 for none;
// and the name the platform has for a key, as a label to show.
extern "gs_ngfx_scancode" fn scancode(name: const u8[:]) -> i64;
extern "gs_ngfx_key_name" fn key_name_into(scancode: i64, out: u8[:]) -> i64;
fn key_name(scancode: i64) -> u8[>..] {
    var name: u8[>..] = [];
    let n = key_name_into(scancode, name);
    name.resize(n, 0);
    key_name_into(scancode, name);
    return name;
}

// The system clipboard's text; headless, ngfx's own.
extern "gs_ngfx_clipboard" fn clipboard_into(out: u8[:]) -> i64;
fn clipboard() -> u8[>..] {
    var text: u8[>..] = [];
    let n = clipboard_into(text);
    text.resize(n, 0);
    clipboard_into(text);
    return text;
}
extern "gs_ngfx_set_clipboard" fn set_clipboard(text: const u8[:]) -> bool;
// --- buffers and this frame's data ---------------------------------------------

// A buffer of `size` bytes of GPU memory, holding `data` and zeros after it.
// Shaders reach it through its address. Data comes from bytes_of:
// ngfx::buffer(bytes_of(vertices)).
extern "gs_ngfx_create_buffer" fn buffer(size: i64, data: const u8[:]) -> Buffer;
fn buffer(data: const u8[:]) -> Buffer { buffer(data.len, data) }
// New contents from `offset`, for every draw of the frame being recorded and
// the frames after it.
extern "gs_ngfx_update_buffer" fn update_buffer(b: Buffer, offset: i64, data: const u8[:]) -> bool;
fn update_buffer(b: Buffer, data: const u8[:]) -> bool { update_buffer(b, 0, data) }
extern "gs_ngfx_read_buffer" fn read_buffer_into(b: Buffer, offset: i64, out: u8[:]) -> bool;
extern "gs_ngfx_buffer_size" fn buffer_size(b: Buffer) -> i64;
// The buffer's GPU address, for a root: a pointer in the shader.
extern "gs_ngfx_buffer_address" fn address(b: Buffer) -> u64;
extern "gs_ngfx_release_buffer" fn release(b: Buffer);

// Reads back `out.len` bytes from `offset`, once the GPU has done
// everything recorded before. Not inside a pass.
fn read_buffer(b: Buffer, offset: i64, out: u8[:]) -> bool {
    let ok = read_buffer_into(b, offset, out);
    check();
    ok
}

// Appends the buffer's first `count` elements to `out`, as the elements of
// out's type: ngfx::read_buffer(results, floats, 256) into an f32[>..].
fn read_buffer<T>(b: Buffer, out: T[>..]&, count: i64) -> bool {
    var raw: u8[>..] = [];
    raw.resize(count * elem_size(out), 0);
    let ok = read_buffer(b, 0, raw);
    append_as(raw, out);
    ok
}

// `data` copied into this frame's memory, and its GPU address, 16-byte
// aligned. It lives until the GPU has finished the frame.
extern "gs_ngfx_frame_data" fn frame_data(data: const u8[:]) -> u64;

// A root: any flat struct copied into this frame's memory, and the address
// a draw or dispatch takes.
fn root<T>(value: T) -> u64 {
    let one: T[1] = [value];
    frame_data(bytes_of(one))
}

// --- textures and samplers -----------------------------------------------------

// The extern fns taking a description by reference are wrapped by ones
// taking it by value, which a struct literal can be passed to.
extern "gs_ngfx_create_texture" fn create_texture(desc: const TextureDesc&) -> Texture;
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

// A PNG (or .bmp) as an RGBA8 texture, RGBA8_SRGB with LOAD_SRGB.
extern "gs_ngfx_load_texture" fn load_texture(path: const u8[:], flags: i64) -> Texture;
fn load_texture(path: const u8[:]) -> Texture { load_texture(path, 0) }

extern "gs_ngfx_update_texture" fn update_texture_region(t: Texture, region: const Region&,
                                                         data: const u8[:]) -> bool;
fn update_texture(t: Texture, region: Region, data: const u8[:]) -> bool {
    update_texture_region(t, region, data)
}
// The whole of mip 0 of layer 0: a 2D texture, or all of a 3D one.
fn update_texture(t: Texture, data: const u8[:]) -> bool { update_texture(t, Region {}, data) }

extern "gs_ngfx_read_texture" fn read_texture_into(t: Texture, region: const Region&,
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
extern "gs_ngfx_save_png" fn save_png_raw(t: Texture, path: const u8[:]) -> bool;
fn save_png(t: Texture, path: const u8[:]) -> bool {
    let ok = save_png_raw(t, path);
    check();
    ok
}
fn screenshot(path: const u8[:]) -> bool { save_png(screen(), path) }

extern "gs_ngfx_texture_info" fn texture_info_into(t: Texture, out: TextureDesc&) -> bool;
fn texture_info(t: Texture) -> TextureDesc {
    var d = TextureDesc { width: 0, height: 0 };
    texture_info_into(t, d);
    d
}
// The texture's slot in the sampled-texture heap, for a root: the shader's
// gpu_texture<Texture2D<float4>>(index). A SAMPLED texture has one.
extern "gs_ngfx_texture_index" fn index(t: Texture) -> u32;
// Its slot as a storage texture, gpu_texture<RWTexture2D<float4>>(index),
// for a STORAGE texture; or one mip level's, which is how a shader writes a
// level other than the first.
extern "gs_ngfx_storage_index" fn storage_index(t: Texture) -> u32;
extern "gs_ngfx_storage_mip_index" fn storage_index(t: Texture, mip: i64) -> u32;
extern "gs_ngfx_release_texture" fn release(t: Texture);

// The bytes one texel of `format` takes.
fn texel_size(format: i32) -> i64 {
    if format == R8 { return 1; }
    if format == RG8 || format == R16F || format == DEPTH16 { return 2; }
    if format == RGBA16F || format == RG32F { return 8; }
    if format == RGBA32F { return 16; }
    return 4;
}

extern "gs_ngfx_create_sampler" fn create_sampler(desc: const SamplerDesc&) -> Sampler;
fn sampler(desc: SamplerDesc) -> Sampler { create_sampler(desc) }
fn sampler(filter: i32, wrap: i32) -> Sampler {
    sampler(SamplerDesc { min_filter: filter, mag_filter: filter, mip_filter: filter,
                          wrap_u: wrap, wrap_v: wrap, wrap_w: wrap })
}
// For a SamplerComparisonState: a hardware depth comparison, filtered.
fn shadow_sampler() -> Sampler {
    sampler(SamplerDesc { wrap_u: CLAMP, wrap_v: CLAMP, wrap_w: CLAMP,
                          compare: COMPARE_LESS_EQUAL })
}
// The sampler's slot in the sampler heap, for a root: gpu_sampler(index).
extern "gs_ngfx_sampler_index" fn index(s: Sampler) -> u32;
extern "gs_ngfx_release_sampler" fn release(s: Sampler);

// --- pipelines ---------------------------------------------------------------

// `shader` is an embed_slang() result. The vertex and fragment entry points
// are its only ones of their stage, or the ones named.
extern "gs_ngfx_create_pipeline" fn create_pipeline(shader: const u8[:], vertex: const u8[:],
                                                    fragment: const u8[:],
                                                    desc: const PipelineDesc&) -> Pipeline;
fn pipeline(shader: const u8[:], vertex: const u8[:], fragment: const u8[:],
            desc: PipelineDesc) -> Pipeline {
    create_pipeline(shader, vertex, fragment, desc)
}
fn pipeline(shader: const u8[:], desc: PipelineDesc) -> Pipeline {
    create_pipeline(shader, "", "", desc)
}
fn pipeline(shader: const u8[:]) -> Pipeline { pipeline(shader, PipelineDesc {}) }
extern "gs_ngfx_release_pipeline" fn release(p: Pipeline);

// The compute entry point `entry` of `shader`, or its only one; the thread
// group size is the one its numthreads declares.
extern "gs_ngfx_create_compute_pipeline" fn compute_pipeline(shader: const u8[:],
                                                             entry: const u8[:]) -> ComputePipeline;
fn compute_pipeline(shader: const u8[:]) -> ComputePipeline { compute_pipeline(shader, "") }
extern "gs_ngfx_release_compute_pipeline" fn release(p: ComputePipeline);

// --- passes and drawing ----------------------------------------------------------

// Draws go between begin_pass and end_pass, into the targets the pass
// names; the bound pipeline lasts until the pass ends.
extern "gs_ngfx_begin_pass" fn begin_pass_desc(desc: const PassDesc&) -> bool;
fn begin_pass(desc: PassDesc) -> bool { begin_pass_desc(desc) }
extern "gs_ngfx_end_pass" fn end_pass();
// Into one target, with a depth target or Texture { 0 }, cleared.
fn begin_pass(target: Texture, depth: Texture, clear: float4) -> bool {
    var d = PassDesc { depth: depth, color_value: clear };
    d.color[0] = target;
    begin_pass_desc(d)
}
// Into the screen and its depth, cleared to `clear`.
fn begin_screen(clear: float4) -> bool { begin_pass(screen(), screen_depth(), clear) }

extern "gs_ngfx_bind_pipeline" fn bind(p: Pipeline);
extern "gs_ngfx_viewport" fn viewport(x: f32, y: f32, w: f32, h: f32);
// w == 0: the whole target.
extern "gs_ngfx_scissor" fn scissor(x: i64, y: i64, w: i64, h: i64);

// `root` is a root's address (root(), frame_data()), or 0 for a shader
// without one.
extern "gs_ngfx_draw" fn draw(root: u64, vertices: i64, instances: i64, first_vertex: i64,
                              first_instance: i64);
fn draw(root: u64, vertices: i64) { draw(root, vertices, 1, 0, 0) }
fn draw_instanced(root: u64, vertices: i64, instances: i64) { draw(root, vertices, instances, 0, 0) }
// Indices from a buffer, of index_size bytes: 2 for u16, 4 for u32.
extern "gs_ngfx_draw_indexed" fn draw_indexed(root: u64, indices: Buffer, index_size: i64,
                                              count: i64, instances: i64, first_index: i64,
                                              vertex_offset: i64, first_instance: i64);
fn draw_indexed(root: u64, indices: Buffer, index_size: i64, count: i64) {
    draw_indexed(root, indices, index_size, count, 1, 0, 0, 0)
}
fn draw_indexed_instanced(root: u64, indices: Buffer, index_size: i64, count: i64,
                          instances: i64) {
    draw_indexed(root, indices, index_size, count, instances, 0, 0, 0)
}

// --- compute -------------------------------------------------------------------

// Dispatches go between passes. What a dispatch writes reaches a later pass
// or dispatch only after a barrier (see synchronization below).
extern "gs_ngfx_bind_compute_pipeline" fn bind(p: ComputePipeline);
// Runs x * y * z thread groups of the shader's numthreads.
extern "gs_ngfx_dispatch" fn dispatch(root: u64, x: i64, y: i64, z: i64);

// --- synchronization -------------------------------------------------------------

// What `before` did, before `after` does it: barrier(COMPUTE_WRITE,
// FRAGMENT_READ) when a pass samples what a dispatch wrote. Between passes
// and dispatches, never inside a pass. Each side is barrier points
// combined with |.
extern "gs_ngfx_barrier" fn barrier(before: i32, after: i32);

// --- mips ------------------------------------------------------------------------

// The root of the layer's downsampling shader (src/ngfx/ngfx_mips.slang).
struct MipRoot { source: u32, target: u32, level: u32, width: u32, height: u32, source_width: u32,
                 source_height: u32 }

// generate_mips' pipeline: the layer's downsampling shader
// (src/ngfx/ngfx_mips.slang), which takes a MipRoot and makes each texel of
// a level the average of the 2 x 2 above it. It is built with the layer, not
// embedded here, where every program importing ngfx would compile it.
extern "gs_ngfx_mip_pipeline" fn mip_pipeline() -> ComputePipeline;

// Fills mips 1 and up of a 2D texture from mip 0, a compute dispatch per
// level with a barrier between them. The texture needs SAMPLED | STORAGE
// usage, so a format storage can write: not an sRGB one. Not inside a pass.
// What wrote mip 0 is ordered before this by the program (an upload is
// ngfx's), and so is what reads the levels after it:
//   ngfx::generate_mips(t);
//   ngfx::barrier(ngfx::COMPUTE_WRITE, ngfx::FRAGMENT_READ);
fn generate_mips(t: Texture) {
    let info = texture_info(t);
    if info.kind != TEXTURE_2D || (info.usage & (SAMPLED | STORAGE)) != SAMPLED | STORAGE {
        abort("ngfx: generate_mips takes a 2D texture with SAMPLED | STORAGE usage");
    }
    bind(mip_pipeline());
    var width = info.width as i64;
    var height = info.height as i64;
    for level in 1..info.mips as i64 {
        let w = max(width / 2, 1);
        let h = max(height / 2, 1);
        if level > 1 { barrier(COMPUTE_WRITE, COMPUTE_READ); }
        dispatch(root(MipRoot { source: index(t), target: storage_index(t, level), level: (level - 1) as u32,
                                width: w as u32, height: h as u32, source_width: width as u32,
                                source_height: height as u32 }),
                 (w + 7) / 8, (h + 7) / 8, 1);
        width = w;
        height = h;
    }
}

// --- matrices ------------------------------------------------------------------

// A row-major 4x4 matrix, as NoGraphicsAPIUtility's float4x4 and Slang's
// (built with -matrix-layout-row-major) are laid out: m * v is
// mul(root.m, v) in a shader.
struct float4x4 { rows: float4[4] }

let float4x4_identity = float4x4 { rows: [float4 { 1.0, 0.0, 0.0, 0.0 }, float4 { 0.0, 1.0, 0.0, 0.0 },
                                          float4 { 0.0, 0.0, 1.0, 0.0 }, float4 { 0.0, 0.0, 0.0, 1.0 }] };

fn column(m: float4x4, i: i64) -> float4 {
    match i {
        0 => float4 { m.rows[0].x, m.rows[1].x, m.rows[2].x, m.rows[3].x },
        1 => float4 { m.rows[0].y, m.rows[1].y, m.rows[2].y, m.rows[3].y },
        2 => float4 { m.rows[0].z, m.rows[1].z, m.rows[2].z, m.rows[3].z },
        _ => float4 { m.rows[0].w, m.rows[1].w, m.rows[2].w, m.rows[3].w },
    }
}

fn transpose(m: float4x4) -> float4x4 {
    float4x4 { rows: [column(m, 0), column(m, 1), column(m, 2), column(m, 3)] }
}

fn mul(m: float4x4, v: float4) -> float4 {
    float4 { dot(m.rows[0], v), dot(m.rows[1], v), dot(m.rows[2], v), dot(m.rows[3], v) }
}

fn mul(a: float4x4, b: float4x4) -> float4x4 {
    let t = transpose(b);
    var m = float4x4_identity;
    for i in 4 { m.rows[i] = mul(t, a.rows[i]); }
    m
}

fn translate(t: float3) -> float4x4 {
    float4x4 { rows: [float4 { 1.0, 0.0, 0.0, t.x }, float4 { 0.0, 1.0, 0.0, t.y },
                      float4 { 0.0, 0.0, 1.0, t.z }, float4 { 0.0, 0.0, 0.0, 1.0 }] }
}

fn scale(s: float3) -> float4x4 {
    float4x4 { rows: [float4 { s.x, 0.0, 0.0, 0.0 }, float4 { 0.0, s.y, 0.0, 0.0 },
                      float4 { 0.0, 0.0, s.z, 0.0 }, float4 { 0.0, 0.0, 0.0, 1.0 }] }
}

// A rotation of `angle` radians about `axis`, counterclockwise looking down
// the axis.
fn rotate(axis: float3, angle: f32) -> float4x4 {
    let a = normalize(axis);
    let c = cos(angle);
    let s = sin(angle);
    let k = 1.0 - c;
    float4x4 { rows: [float4 { c + a.x * a.x * k, a.x * a.y * k - a.z * s, a.x * a.z * k + a.y * s, 0.0 },
                      float4 { a.y * a.x * k + a.z * s, c + a.y * a.y * k, a.y * a.z * k - a.x * s, 0.0 },
                      float4 { a.z * a.x * k - a.y * s, a.z * a.y * k + a.x * s, c + a.z * a.z * k, 0.0 },
                      float4 { 0.0, 0.0, 0.0, 1.0 }] }
}

// Right-handed, looking down -z, to NoGraphicsAPI's clip space: y down, depth
// 0 at `near` and 1 at `far`. gfx's perspective with y negated, as
// NoGraphicsAPI's examples negate perspective_rh_zo's.
fn perspective(fovy: f32, aspect: f32, near: f32, far: f32) -> float4x4 {
    let f = 1.0 / tan(fovy * 0.5);
    float4x4 { rows: [float4 { f / aspect, 0.0, 0.0, 0.0 }, float4 { 0.0, -f, 0.0, 0.0 },
                      float4 { 0.0, 0.0, far / (near - far), near * far / (near - far) },
                      float4 { 0.0, 0.0, -1.0, 0.0 }] }
}

// `bottom` and `top` are where y is at the screen's bottom and top edges:
// ortho(0, w, h, 0, ...) is pixels with y down, as it is in gfx.
fn ortho(left: f32, right: f32, bottom: f32, top: f32, near: f32, far: f32) -> float4x4 {
    float4x4 { rows: [float4 { 2.0 / (right - left), 0.0, 0.0, (left + right) / (left - right) },
                      float4 { 0.0, 2.0 / (bottom - top), 0.0, (top + bottom) / (top - bottom) },
                      float4 { 0.0, 0.0, 1.0 / (near - far), near / (near - far) },
                      float4 { 0.0, 0.0, 0.0, 1.0 }] }
}

fn look_at(eye: float3, target: float3, up: float3) -> float4x4 {
    let f = normalize(target - eye);
    let s = normalize(cross(f, up));
    let u = cross(s, f);
    float4x4 { rows: [float4 { s.x, s.y, s.z, -dot(s, eye) }, float4 { u.x, u.y, u.z, -dot(u, eye) },
                      float4 { -f.x, -f.y, -f.z, dot(f, eye) }, float4 { 0.0, 0.0, 0.0, 1.0 }] }
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
