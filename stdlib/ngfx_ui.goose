// The Goose standard library: `ngfx_ui` (docs/stdlib.md), the ui module's
// renderer and input over ngfx, as ui's own render() and input_from_gfx()
// are over gfx. ui is unchanged: this draws the triangles ui::convert makes
// and feeds ui the frame's ngfx events. A program shows its ui with
//
//   ngfx_ui::input(ctx);    // after ngfx::frame(), before any window
//   ...windows...
//   ngfx_ui::render(ctx);   // after the frame's own passes
//
// Images are textures by their ngfx handle's id: ui::image_id(t.id).

namespace ngfx_ui;

import std;
import vec;
import math;
import ngfx;
import ui;

// What drawing through ngfx keeps between frames, shared by every context:
// made the first time, and again after ngfx is opened again (its handles
// died with ngfx::close).
struct Renderer {
    opened: i64,                    // ngfx::open_count() when made; 0: never made
    pipeline: ngfx::Pipeline,
    sampler: ngfx::Sampler,
    text_input: bool,               // ngfx's text input turned on for input
    // Scancodes of the keys input maps, by the names ngfx has for them.
    keys: i64[16],
}

var renderer = Renderer { 0, ngfx::Pipeline { 0 }, ngfx::Sampler { 0 }, false, [0; 16] };

// One draw command's root, as the shader declares it: the frame's vertices
// and indices, where its indices start, its texture and the sampler, and
// the target's size.
struct Root { vertices: u64, indices: u64, first: u32, texture: u32, sampler: u32, size: float2 }

// The vertex shader reads its own index, then its vertex: ui's triangles are
// indexed, and an index buffer for draw_indexed would have to be a Buffer,
// where this frame's data is memory with an address.
let shader = """
    #include <NoGraphicsAPI/shader.slang>

    struct Vertex { float2 pos; float2 uv; uint color; };
    struct Root { Vertex* vertices; uint* indices; uint first; uint texture; uint sampler; float2 size; };
    GPU_ROOT(Root, root);

    struct Varyings {
        float4 position : SV_Position;
        float2 uv : TEXCOORD0;
        float4 color : TEXCOORD1;
    };

    // Pixels with y down, as NoGraphicsAPI's clip space has it.
    [shader("vertex")]
    Varyings uiVertex(uint vertex_id : SV_VertexID) {
        Vertex v = root.vertices[root.indices[root.first + vertex_id]];
        Varyings o;
        o.uv = v.uv;
        o.color = unpackUnorm4x8ToFloat(v.color);
        float2 p = v.pos / root.size;
        o.position = float4(p.x * 2.0 - 1.0, p.y * 2.0 - 1.0, 0.0, 1.0);
        return o;
    }

    [shader("fragment")]
    float4 uiFragment(Varyings i) : SV_Target0 {
        Texture2D<float4> texture = gpu_texture<Texture2D<float4>>(root.texture);
        return i.color * texture.Sample(gpu_sampler(root.sampler), i.uv);
    }
    """;

fn make_renderer() {
    renderer.pipeline = ngfx::pipeline(embed_slang(shader), ngfx::PipelineDesc { blend: ngfx::BLEND_ALPHA });
    renderer.sampler = ngfx::sampler(ngfx::LINEAR, ngfx::CLAMP);
    renderer.text_input = false;
    renderer.opened = ngfx::open_count();
    // The atlases' textures died with the ngfx they were made in.
    ui::forget_textures();
    for i in renderer_textures.len { renderer_textures[i] = 0; }
}

// The ngfx textures upload_atlases made, which are released with their
// atlases; a 0 is a free slot.
var renderer_textures: u32[>..] = [];

// Uploads the image of every baked atlas no texture holds yet, and releases
// the textures of those destroyed.
fn upload_atlases() {
    for t in ui::released_textures() {
        let i = position(renderer_textures, t);
        if i >= 0 {
            ngfx::release(ngfx::Texture { t });
            renderer_textures[i] = 0;
        }
    }
    loop {
        let a = ui::atlas_without_texture();
        if a.id == 0 { break; }
        let size = ui::atlas_size(a);
        let t = ngfx::texture2d(size.x, size.y);
        ngfx::update_texture(t, ui::atlas_pixels(a));
        ui::set_texture(a, t.id);
        let free = position(renderer_textures, 0);
        if free >= 0 { renderer_textures[free] = t.id; } else { renderer_textures.push(t.id); }
    }
}

// Draws the context's frame over `target`, an ngfx texture of COLOR_TARGET
// usage, with alpha blending; not inside a pass. The frame's windows must
// have ended.
fn render(c: ui::Context, target: ngfx::Texture, config: ui::ConvertConfig) {
    ui::check();
    if renderer.opened != ngfx::open_count() { make_renderer(); }
    upload_atlases();
    ui::convert(c, config);
    let draws = ui::draw_commands(c);
    if draws.len == 0 { return; }
    let vertices = ngfx::frame_data(bytes_of(ui::vertices(c)));
    let indices = ngfx::frame_data(bytes_of(ui::indices(c)));
    let info = ngfx::texture_info(target);
    let size = float2 { info.width, info.height };
    let sampler = ngfx::index(renderer.sampler);
    var pass = ngfx::PassDesc { clear_color: false, clear_depth: false };
    pass.color[0] = target;
    ngfx::begin_pass(pass);
    ngfx::bind(renderer.pipeline);
    var first = 0;
    for d in draws {
        // Scissors in whole pixels, inside the target.
        let x0 = clamp(floor(d.clip_rect.x) as! i64, 0, info.width);
        let y0 = clamp(floor(d.clip_rect.y) as! i64, 0, info.height);
        let x1 = clamp(ceil(d.clip_rect.x + d.clip_rect.w) as! i64, 0, info.width);
        let y1 = clamp(ceil(d.clip_rect.y + d.clip_rect.h) as! i64, 0, info.height);
        if x1 > x0 && y1 > y0 {
            ngfx::scissor(x0, y0, x1 - x0, y1 - y0);
            let root = ngfx::root(Root { vertices: vertices, indices: indices, first: first as u32,
                                         texture: ngfx::index(ngfx::Texture { d.texture }),
                                         sampler: sampler, size: size });
            ngfx::draw(root, d.elem_count);
        }
        first += d.elem_count;
    }
    ngfx::end_pass();
}
fn render(c: ui::Context, target: ngfx::Texture) { render(c, target, ui::ConvertConfig {}) }
// Over the screen.
fn render(c: ui::Context) { render(c, ngfx::screen(), ui::ConvertConfig {}) }

// The scancode of one of the keys input maps, by the name ngfx has
// for it.
fn ngfx_key(i: i64) -> i64 {
    let names: const u8[:][16] = ["Left Shift", "Right Shift", "Left Ctrl", "Right Ctrl", "Delete",
                                  "Return", "Keypad Enter", "Tab", "Backspace", "Home", "End",
                                  "PageDown", "PageUp", "Up", "Down", "Escape"];
    if renderer.keys[i] == 0 { renderer.keys[i] = ngfx::scancode(names[i]); }
    renderer.keys[i]
}

// Nuklear's keys for an ngfx key event: editing keys by name, shortcuts by
// the character they type, with Ctrl (or the Command key).
fn input_key_event(c: ui::Context, e: ngfx::Event) {
    let cmd = (e.mods & (ngfx::MOD_CTRL | ngfx::MOD_GUI)) != 0;
    let shift = (e.mods & ngfx::MOD_SHIFT) != 0;
    let sc = e.scancode as i64;
    if sc == ngfx_key(0) || sc == ngfx_key(1) { ui::input_key(c, ui::KEY_SHIFT, e.down); }
    else if sc == ngfx_key(2) || sc == ngfx_key(3) { ui::input_key(c, ui::KEY_CTRL, e.down); }
    else if sc == ngfx_key(4) { ui::input_key(c, ui::KEY_DEL, e.down); }
    else if sc == ngfx_key(5) || sc == ngfx_key(6) { ui::input_key(c, ui::KEY_ENTER, e.down); }
    else if sc == ngfx_key(7) { ui::input_key(c, ui::KEY_TAB, e.down); }
    else if sc == ngfx_key(8) { ui::input_key(c, ui::KEY_BACKSPACE, e.down); }
    else if sc == ngfx_key(9) {
        ui::input_key(c, ui::KEY_TEXT_START, e.down);
        ui::input_key(c, ui::KEY_SCROLL_START, e.down);
    } else if sc == ngfx_key(10) {
        ui::input_key(c, ui::KEY_TEXT_END, e.down);
        ui::input_key(c, ui::KEY_SCROLL_END, e.down);
    } else if sc == ngfx_key(11) { ui::input_key(c, ui::KEY_SCROLL_DOWN, e.down); }
    else if sc == ngfx_key(12) { ui::input_key(c, ui::KEY_SCROLL_UP, e.down); }
    else if sc == ngfx_key(13) { ui::input_key(c, ui::KEY_UP, e.down); }
    else if sc == ngfx_key(14) { ui::input_key(c, ui::KEY_DOWN, e.down); }
    else if sc == ngfx_key(15) { ui::input_key(c, ui::KEY_TEXT_RESET_MODE, e.down); }
    else if sc == ngfx::scancode("Left") {
        ui::input_key(c, if cmd { ui::KEY_TEXT_WORD_LEFT } else { ui::KEY_LEFT }, e.down);
    } else if sc == ngfx::scancode("Right") {
        ui::input_key(c, if cmd { ui::KEY_TEXT_WORD_RIGHT } else { ui::KEY_RIGHT }, e.down);
    } else if e.keycode == 'a' { ui::input_key(c, ui::KEY_TEXT_SELECT_ALL, e.down && cmd); }
    else if e.keycode == 'z' {
        ui::input_key(c, if shift { ui::KEY_TEXT_REDO } else { ui::KEY_TEXT_UNDO }, e.down && cmd);
    } else if e.keycode == 'y' { ui::input_key(c, ui::KEY_TEXT_REDO, e.down && cmd); }
    else if e.keycode == 'c' { ui::input_key(c, ui::KEY_COPY, e.down && cmd); }
    else if e.keycode == 'x' { ui::input_key(c, ui::KEY_CUT, e.down && cmd); }
    else if e.keycode == 'v' {
        // What is pasted is the system clipboard's when the keys go down.
        if e.down && cmd { ui::set_clipboard(c, ngfx::clipboard()); }
        ui::input_key(c, ui::KEY_PASTE, e.down && cmd);
    } else {
        // Nuklear has no use for the other keys: their text comes as text.
    }
}

// Nuklear's input for a frame from ngfx's: the last frame's events in order
// (keys, typed text, the mouse), its duration, and the clipboard both ways.
// Call it once a frame, after ngfx::frame() and before any window.
fn input(c: ui::Context) {
    if renderer.opened != ngfx::open_count() { make_renderer(); }
    if !renderer.text_input {
        ngfx::text_input(true);
        renderer.text_input = true;
    }
    // What a text edit copied last frame goes to the system clipboard.
    let text, copied_some = ui::copied(c);
    if copied_some { ngfx::set_clipboard(text); }
    ui::input_begin(c);
    for e in ngfx::events() {
        let x = floor(e.x) as! i64;
        let y = floor(e.y) as! i64;
        if e.kind == ngfx::EVENT_MOUSE_MOTION {
            ui::input_motion(c, x, y);
        } else if e.kind == ngfx::EVENT_MOUSE_BUTTON {
            let b = match e.button {
                1 => ui::BUTTON_LEFT,
                2 => ui::BUTTON_MIDDLE,
                3 => ui::BUTTON_RIGHT,
                4 => ui::BUTTON_X1,
                5 => ui::BUTTON_X2,
                _ => -1,
            };
            if b == ui::BUTTON_LEFT && e.clicks > 1 { ui::input_button(c, ui::BUTTON_DOUBLE, x, y, e.down); }
            if b >= 0 { ui::input_button(c, b, x, y, e.down); }
        } else if e.kind == ngfx::EVENT_MOUSE_WHEEL {
            ui::input_scroll(c, float2 { e.x, e.y });
        } else if e.kind == ngfx::EVENT_TEXT {
            ui::input_unicode(c, e.codepoint);
        } else if e.kind == ngfx::EVENT_KEY {
            input_key_event(c, e);
        }
    }
    ui::input_end(c);
    ui::set_delta_time(c, ngfx::delta_time() as f32);
}
