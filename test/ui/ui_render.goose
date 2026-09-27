// The ui module through gfx, headless: frames drawn over the screen and
// over a render target, their solid parts read back; clicks, typing,
// editing keys, shortcuts and the clipboard coming in through
// input_from_gfx from injected input; the ui drawn and clicked at twice
// the scale; and the renderer made again after gfx is closed and opened.
// Solid parts cover whole pixels, so every backend draws them alike.
import std;
import vec;
import math;
import os;
import gfx;
import ui;

let W = 200;
let H = 150;

var clicks = 0;
var checked = false;
var name: u8[..16] = [];
var button_at = ui::Rect { 0.0, 0.0, 0.0, 0.0 };
var field_at = ui::Rect { 0.0, 0.0, 0.0, 0.0 };
var check_at = ui::Rect { 0.0, 0.0, 0.0, 0.0 };

fn open_or_skip() -> bool {
    if gfx::open_headless(W, H) { return true; }
    write_stderr(str("gfx: ", gfx::error(), "\n"));
    return false;
}

fn clear_to(t: gfx::Texture) {
    var pass = gfx::PassDesc { color_value: float4 { 0.2, 0.4, 0.6, 1.0 } };
    pass.color[0] = t;
    gfx::begin_pass(pass);
    gfx::end_pass();
}

// A frame: gfx's input into the context, the window, drawn over the screen.
fn frame(ctx: ui::Context) {
    gfx::frame();
    ui::input_from_gfx(ctx);
    if ui::begin(ctx, "Render", ui::rect(10, 10, 180, 130),
                 ui::WINDOW_BORDER | ui::WINDOW_TITLE | ui::WINDOW_NO_SCROLLBAR) {
        ui::layout_row_static(ctx, 30.0, 80, 1);
        button_at = ui::widget_bounds(ctx);
        if ui::button_label(ctx, "Press") { clicks++; }
        ui::layout_row_dynamic(ctx, 25.0, 1);
        field_at = ui::widget_bounds(ctx);
        ui::edit_string(ctx, ui::EDIT_FIELD, name, ui::FILTER_DEFAULT);
        check_at = ui::widget_bounds(ctx);
        ui::checkbox_label(ctx, "Check", checked);
    }
    ui::end(ctx);
    clear_to(gfx::screen());
    ui::render(ctx);
}

fn px(p: const u8[:], x: f32, y: f32) -> u8[>..] {
    let i = ((y as! i64) * W + (x as! i64)) * 4;
    str(p[i], " ", p[i + 1], " ", p[i + 2])
}

// Whether any pixel of r is as bright as text.
fn has_text(p: const u8[:], r: ui::Rect) -> bool {
    for y in (r.y as! i64)..((r.y + r.h) as! i64) {
        for x in (r.x as! i64)..((r.x + r.w) as! i64) {
            if p[(y * W + x) * 4] >= 150 { return true; }
        }
    }
    false
}

fn centre(r: ui::Rect) -> float2 { float2 { r.x + r.w / 2.0, r.y + r.h / 2.0 } }

fn click(ctx: ui::Context, r: ui::Rect) {
    let at = centre(r);
    gfx::inject_mouse(at.x, at.y, gfx::MOUSE_LEFT, true);
    frame(ctx);
    gfx::inject_mouse(at.x, at.y, gfx::MOUSE_LEFT, false);
    frame(ctx);
}

fn tap(key: const u8[:]) {
    gfx::inject_key(key, true);
    gfx::inject_key(key, false);
}

fn shortcut(key: const u8[:]) {
    gfx::inject_key("Left Ctrl", true);
    tap(key);
    gfx::inject_key("Left Ctrl", false);
}

fn show(ctx: ui::Context, what: const u8[:]) {
    let p = gfx::read_pixels(gfx::screen());
    print(what, ": outside ", px(p, 195.0, 145.0), ", title bar ", px(p, 150.0, 14.0), ", window ",
          px(p, 150.0, button_at.y + 5.0), ", button ", px(p, centre(button_at).x, button_at.y + 3.0),
          ", its text ", has_text(p, button_at));
}

fn main() {
    guard open_or_skip() else { return; }
    let ctx = ui::create(13.0);
    frame(ctx);
    show(ctx, "first frame");

    // --- the mouse ---
    let b = centre(button_at);
    gfx::inject_mouse(b.x, b.y, 0, false);
    frame(ctx);
    show(ctx, "hovered");
    gfx::inject_mouse(b.x, b.y, gfx::MOUSE_LEFT, true);
    frame(ctx);
    show(ctx, "pressed");
    print("clicks ", clicks);
    gfx::inject_mouse(b.x, b.y, gfx::MOUSE_LEFT, false);
    frame(ctx);
    print("released: clicks ", clicks);
    let box_x = check_at.x + 6.0;
    let box_y = centre(check_at).y;
    let before = px(gfx::read_pixels(gfx::screen()), box_x, box_y);
    click(ctx, check_at);
    print("check box clicked: ", checked, ", its box ", before, " before, ",
          px(gfx::read_pixels(gfx::screen()), box_x, box_y), " after");

    // --- text and keys ---
    click(ctx, field_at);
    gfx::inject_text("goose");
    frame(ctx);
    print("typed: '", name, "'");
    tap("Backspace");
    frame(ctx);
    print("backspace: '", name, "'");
    tap("Home");
    gfx::inject_text("A");
    frame(ctx);
    print("home, A: '", name, "'");
    shortcut("A");
    shortcut("C");
    frame(ctx);
    frame(ctx);
    print("select all, copy: the clipboard holds '", gfx::clipboard(), "'");
    gfx::set_clipboard("!");
    tap("End");
    shortcut("V");
    frame(ctx);
    print("end, paste: '", name, "'");
    shortcut("A");
    shortcut("X");
    frame(ctx);
    frame(ctx);
    print("select all, cut: '", name, "', the clipboard holds '", gfx::clipboard(), "'");
    // A field over a byte buffer keeps no undo from frame to frame.
    shortcut("Z");
    frame(ctx);
    print("undo: '", name, "'");

    // --- a render target ---
    let screen = gfx::read_pixels(gfx::screen());
    let target = gfx::render_target(W, H, gfx::RGBA8);
    clear_to(target);
    ui::render(ctx, target);
    let t = gfx::read_pixels(target);
    var same = true;
    for y in 0..H {
        for x in 0..W {
            let i = (y * W + x) * 4;
            // Solid parts only: where the screen shows the window's colors.
            if screen[i] == 45 && t[i] != 45 { same = false; }
        }
    }
    print("render target: window ", px(t, 150.0, button_at.y + 5.0), ", as on the screen ", same);
    clear_to(target);
    ui::render(ctx, target, ui::ConvertConfig { global_alpha: 0.5 });
    let faded = gfx::read_pixels(target);
    let i = (((button_at.y + 5.0) as! i64) * W + 150) * 4;
    print("half alpha: window ", faded[i] > 45 && faded[i] < 51, " ",
          faded[i + 2] > 45 && faded[i + 2] < 153);
    gfx::release(target);

    // --- the ui at twice the scale: laid out as before, drawn twice as big
    // (the screen shows its top left quarter), clicked in pixels ---
    ui::set_scale(ctx, 2.0);
    gfx::inject_mouse(5.0, 5.0, 0, false);
    frame(ctx);
    let before = clicks;
    let bp = centre(button_at);
    gfx::inject_mouse(bp.x * 2.0, bp.y * 2.0, gfx::MOUSE_LEFT, true);
    frame(ctx);
    gfx::inject_mouse(bp.x * 2.0, bp.y * 2.0, gfx::MOUSE_LEFT, false);
    gfx::inject_mouse(5.0, 5.0, 0, false);
    frame(ctx);
    let big = gfx::read_pixels(gfx::screen());
    let doubled = ui::Rect { button_at.x * 2.0, button_at.y * 2.0, button_at.w * 2.0,
                             button_at.h * 2.0 };
    print("at scale 2: outside ", px(big, 5.0, 5.0), ", title bar ", px(big, 150.0, 30.0),
          ", button ", px(big, centre(doubled).x, doubled.y + 6.0), ", its text ",
          has_text(big, doubled), "; clicked in pixels ", clicks == before + 1);
    ui::set_scale(ctx, 1.0);

    // --- gfx closed and opened again ---
    gfx::close();
    guard open_or_skip() else { return; }
    frame(ctx);
    frame(ctx);
    show(ctx, "opened again");
    ui::destroy(ctx);
    gfx::close();
    print("misuses ", ui::misuse_count());
}
