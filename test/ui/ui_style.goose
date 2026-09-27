// The ui module's style and its helpers: the style read, changed and set
// whole, pushed and popped, reset, and built from a color table, each seen
// in what a window then draws; style items of each kind; the style's fonts;
// software cursors drawn at the mouse; every color conversion; the image,
// nine-slice and rectangle helpers; and Nuklear's hash, triangle, pattern
// and fuzzy-match utilities.
import std;
import vec;
import math;
import gfx;
import ui;

fn col(c: ui::Color) -> u8[>..] { str(c.r, " ", c.g, " ", c.b, " ", c.a) }

fn same(a: ui::Color, b: ui::Color) -> bool {
    a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a
}

// A frame of one window holding a button and a label, the mouse at x, y.
fn frame(ctx: ui::Context, x: i64, y: i64) {
    ui::input_begin(ctx);
    ui::input_motion(ctx, x, y);
    ui::input_end(ctx);
    if ui::begin(ctx, "Style", ui::rect(0, 0, 200, 200), ui::WINDOW_TITLE) {
        ui::layout_row_dynamic(ctx, 30.0, 1);
        ui::button_label(ctx, "Press");
        ui::label(ctx, "text", ui::TEXT_LEFT);
    }
    ui::end(ctx);
}

fn filled_with(ctx: ui::Context, color: ui::Color) -> bool {
    for cmd in ui::commands(ctx) {
        if cmd.kind == ui::COMMAND_RECT_FILLED && same(cmd.color, color) { return true; }
    }
    false
}

fn images_of(ctx: ui::Context, texture: u32) -> i64 {
    var n = 0;
    for cmd in ui::commands(ctx) {
        if cmd.kind == ui::COMMAND_IMAGE && cmd.image.texture == texture { n += 1; }
    }
    n
}

// The image drawn at the mouse, as texture @ x, y w x h, or "none".
fn cursor_drawn(ctx: ui::Context) -> u8[>..] {
    var found: u8[>..] = str("none");
    for cmd in ui::commands(ctx) {
        if cmd.kind == ui::COMMAND_IMAGE && cmd.image.texture >= 100 {
            found = str(cmd.image.texture, " @ ", cmd.x, ", ", cmd.y, " ", cmd.w, " x ", cmd.h);
        }
    }
    found
}

fn main() {
    // --- colors ---
    print("rgb ", col(ui::rgb(255, 128, 0)), "; clamped ", col(ui::rgb(300, -5, 128)));
    print("rgba ", col(ui::rgba(1, 2, 3, 4)), "; u32 0x80FF4020 ", col(ui::rgba_u32(0x80FF4020)));
    print("rgb_f ", col(ui::rgb_f(1.0, 0.5, 0.0)), "; rgba_f ", col(ui::rgba_f(0.0, 0.25, 1.0, 0.5)));
    let cf = ui::Colorf { 0.2, 0.4, 0.6, 0.8 };
    print("rgb_cf ", col(ui::rgb_cf(cf)), "; rgba_cf ", col(ui::rgba_cf(cf)));
    print("rgb_hex ", col(ui::rgb_hex("#FF8000")), "; rgba_hex ", col(ui::rgba_hex("11223344")));
    print("rgb_factor 0.5 of 200 100 50: ", col(ui::rgb_factor(ui::rgb(200, 100, 50), 0.5)));
    print("hsv red ", col(ui::hsv(0, 255, 255)), "; hsva ", col(ui::hsva(85, 255, 255, 128)));
    print("hsv_f green ", col(ui::hsv_f(1.0 / 3.0, 1.0, 1.0)), "; hsva_f blue ",
          col(ui::hsva_f(2.0 / 3.0, 1.0, 1.0, 1.0)));
    let hf = ui::hsva_colorf(0.5, 1.0, 1.0, 1.0);
    print("hsva_colorf cyan ", hf.r, " ", hf.g, " ", hf.b, " ", hf.a);
    let back = ui::colorf_hsva_f(hf);
    print("colorf_hsva_f ", back.x, " ", back.y, " ", back.z, " ", back.w);
    let orange = ui::rgb(255, 128, 0);
    let ocf = ui::color_cf(orange);
    print("color_cf ", ocf.r, " ", ocf.g, " ", ocf.b, " ", ocf.a, "; color_u32 ",
          ui::color_u32(orange));
    print("hex ", ui::color_hex_rgba(orange), " ", ui::color_hex_rgb(orange));
    let hsv = ui::color_hsv_f(orange);
    let hsva = ui::color_hsva_f(ui::rgba(0, 0, 255, 51));
    print("color_hsv_f ", hsv.x, " ", hsv.y, " ", hsv.z, "; color_hsva_f blue ", hsva.x, " ",
          hsva.y, " ", hsva.z, " ", hsva.w);

    // --- the style, whole ---
    let ctx = ui::create(13.0);
    let s0 = ui::style(ctx);
    print("default style: window rounding ", s0.window.rounding, ", button rounding ",
          s0.button.rounding, ", text ", col(s0.text.color), ", header buttons ",
          if s0.window.header.align == ui::HEADER_RIGHT { "right" } else { "left" },
          ", tooltip delay ", s0.window.tooltip_delay);
    print("button background kind ", s0.button.normal.kind, ", ", col(s0.button.normal.color));
    frame(ctx, 900, 900);
    print("drawn in the default button color: ", filled_with(ctx, s0.button.normal.color));

    var s = s0;
    s.button.normal = ui::style_item_color(ui::rgb(200, 10, 20));
    s.window.rounding = 6.0;
    ui::set_style(ctx, s);
    let set = ui::style(ctx);
    print("set: window rounding ", set.window.rounding, ", button ", col(set.button.normal.color));
    frame(ctx, 900, 900);
    print("drawn in the set color: ", filled_with(ctx, ui::rgb(200, 10, 20)));

    var pushed = set;
    pushed.button.normal = ui::style_item_color(ui::rgb(1, 2, 3));
    print("pushed ", ui::push_style(ctx, pushed), ": ", col(ui::style(ctx).button.normal.color));
    frame(ctx, 900, 900);
    print("drawn in the pushed color: ", filled_with(ctx, ui::rgb(1, 2, 3)));
    print("popped ", ui::pop_style(ctx), ": ", col(ui::style(ctx).button.normal.color));

    ui::style_default(ctx);
    print("default again: window rounding ", ui::style(ctx).window.rounding, ", button ",
          col(ui::style(ctx).button.normal.color));

    var table = ui::default_color_table();
    print("color table: ", table.len, " colors, text ", col(table[ui::COLOR_TEXT]), ", window ",
          col(table[ui::COLOR_WINDOW]), ", button ", col(table[ui::COLOR_BUTTON]));
    for i in 8 {
        var names: u8[>..] = [];
        for j in 4 {
            names.append(" ");
            names.append(ui::style_get_color_by_name(i * 4 + j));
        }
        print("names:", names);
    }
    table[ui::COLOR_BUTTON] = ui::rgb(0, 128, 255);
    ui::style_from_table(ctx, table);
    print("from a table: button ", col(ui::style(ctx).button.normal.color));
    frame(ctx, 900, 900);
    print("drawn in the table's color: ", filled_with(ctx, ui::rgb(0, 128, 255)));

    // --- style items ---
    let items: ui::StyleItem[4] = [ui::style_item_color(ui::rgb(1, 2, 3)),
                                   ui::style_item_image(ui::image_id(5)),
                                   ui::style_item_nine_slice(ui::nine_slice_id(7, 1, 2, 3, 4)),
                                   ui::style_item_hide()];
    for it in items {
        print("item kind ", it.kind, ", color ", col(it.color), ", texture ", it.img.texture,
              ", borders ", it.l, " ", it.t, " ", it.r, " ", it.b);
    }
    var imaged = ui::style(ctx);
    imaged.window.fixed_background = items[1];
    imaged.button.normal = items[2];
    ui::set_style(ctx, imaged);
    frame(ctx, 900, 900);
    print("window background images ", images_of(ctx, 5), ", button nine-slice parts ",
          images_of(ctx, 7));
    ui::style_default(ctx);

    // --- the style's fonts ---
    let atlas = ui::create_font_atlas();
    let small = ui::add_default_font(atlas, 10.0);
    let big = ui::add_default_font(atlas, 20.0);
    ui::bake(atlas);
    let fctx = ui::create_context(small);
    print("font: small ", ui::font(fctx).id == small.id);
    ui::style_set_font(fctx, big);
    print("set: big ", ui::font(fctx).id == big.id);
    print("pushed small ", ui::style_push_font(fctx, small), ": small ", ui::font(fctx).id == small.id);
    print("popped ", ui::style_pop_font(fctx), ": big ", ui::font(fctx).id == big.id);
    ui::input_begin(fctx);
    ui::input_end(fctx);
    if ui::begin(fctx, "Fonts", ui::rect(0, 0, 200, 200), 0) {
        ui::layout_row_dynamic(fctx, 30.0, 1);
        ui::label(fctx, "big", ui::TEXT_LEFT);
        ui::style_push_font(fctx, small);
        ui::label(fctx, "small", ui::TEXT_LEFT);
        ui::style_pop_font(fctx);
    }
    ui::end(fctx);
    let text = ui::command_text(fctx);
    for cmd in ui::commands(fctx) {
        if cmd.kind == ui::COMMAND_TEXT {
            print("text '", text[cmd.first..cmd.first + cmd.count], "' in a font ", cmd.height,
                  " high: ", if cmd.font.id == big.id { "big" } else { "small" });
        }
    }
    ui::destroy(fctx);
    ui::destroy(atlas);

    // --- software cursors ---
    frame(ctx, 50, 45);
    print("no cursors loaded: ", cursor_drawn(ctx));
    var cursors: ui::Cursor[7] = [ui::Cursor { img: ui::image_id(0), size: float2_0, offset: float2_0 }; 7];
    for i in 7 {
        cursors[i] = ui::Cursor { img: ui::image_id((100 + i) as u32),
                                  size: float2 { 16.0, 20.0 }, offset: float2 { 2.0, 3.0 } };
    }
    ui::style_load_all_cursors(ctx, cursors);
    frame(ctx, 50, 45);
    print("all loaded, the arrow: ", cursor_drawn(ctx));
    // A text field being edited under the mouse picks the text cursor; the
    // program the move cursor.
    var field: u8[8] = [0; 8];
    var field_len = 0;
    for step in 2 {
        ui::input_begin(ctx);
        ui::input_motion(ctx, 50, 45);
        ui::input_button(ctx, ui::BUTTON_LEFT, 50, 45, step == 0);
        ui::input_end(ctx);
        if ui::begin(ctx, "Style", ui::rect(0, 0, 200, 200), ui::WINDOW_TITLE) {
            ui::layout_row_dynamic(ctx, 30.0, 1);
            if step == 0 {
                ui::edit_string(ctx, ui::EDIT_FIELD, field, field_len, ui::FILTER_DEFAULT);
            } else {
                print("set the move cursor: ", ui::style_set_cursor(ctx, ui::CURSOR_MOVE));
            }
        }
        ui::end(ctx);
        print(if step == 0 { "over a text field clicked: " } else { "set: " }, cursor_drawn(ctx));
    }
    ui::style_hide_cursor(ctx);
    frame(ctx, 50, 45);
    print("hidden: ", cursor_drawn(ctx));
    ui::style_show_cursor(ctx);
    ui::style_load_cursor(ctx, ui::CURSOR_ARROW,
                          ui::Cursor { img: ui::image_id(200), size: float2 { 8.0, 8.0 },
                                       offset: float2_0 });
    frame(ctx, 50, 45);
    print("shown, another arrow: ", cursor_drawn(ctx));
    ui::destroy(ctx);

    // --- images and rectangles ---
    let whole = ui::image_id(3);
    let sub = ui::subimage_id(3, 64, 32, ui::rect(8, 0, 16, 16));
    print("image ", whole.texture, " subimage ", ui::image_is_subimage(whole), "; part of ",
          sub.w, " x ", sub.h, " at ", sub.region[0], " ", sub.region[1], " ", sub.region[2], " ",
          sub.region[3], " subimage ", ui::image_is_subimage(sub));
    let ns = ui::nine_slice_id(4, 1, 2, 3, 4);
    let sns = ui::sub9slice_id(4, 64, 64, ui::rect(0, 0, 32, 32), 5, 6, 7, 8);
    print("nine-slice ", ns.img.texture, " borders ", ns.l, " ", ns.t, " ", ns.r, " ", ns.b,
          " sub ", ui::nine_slice_is_sub9slice(ns), "; of a part: ", sns.l, " ", sns.t, " ",
          sns.r, " ", sns.b, " sub ", ui::nine_slice_is_sub9slice(sns));
    print("texture image ", ui::texture_image(gfx::Texture { id: 42 }).texture);
    let r = ui::rect(1.5, 2.5, 3.0, 4.0);
    let ri = ui::rect(1, 2, 3, 4);
    let ra = ui::recta(float2 { 5.0, 6.0 }, float2 { 7.0, 8.0 });
    print("rects ", r.x, " ", r.y, " ", r.w, " ", r.h, "; ", ri.x, " ", ri.h, "; ", ra.x, " ",
          ra.y, " ", ra.w, " ", ra.h, "; pos ", ui::rect_pos(ra).x, " ", ui::rect_pos(ra).y,
          " size ", ui::rect_size(ra).x, " ", ui::rect_size(ra).y);
    let anywhere = ui::get_null_rect();
    print("null rect ", anywhere.x, " ", anywhere.y, " ", anywhere.w, " ", anywhere.h);

    // --- utilities ---
    print("murmur ", ui::murmur_hash("goose", 0), " ", ui::murmur_hash("goose", 1), " ",
          ui::murmur_hash("", 0));
    let dirs: const u8[:][4] = ["up", "right", "down", "left"];
    for d in 4 {
        let t = ui::triangle_from_direction(ui::rect(0, 0, 10, 20), 1.0, 2.0, d);
        print("triangle ", dirs[d], ": ", t[0].x, ",", t[0].y, " ", t[1].x, ",", t[1].y, " ",
              t[2].x, ",", t[2].y);
    }
    let patterns: const u8[:][7] = ["world$", "^hello", "^world", "h.llo", "hel*o", "xyz", "^h.*d$"];
    for p in patterns { print("strfilter hello world ", p, ": ", ui::strfilter("hello world", p)); }
    let searches: const u8[:][4] = ["hw", "wh", "hello world", "lo w"];
    for q in searches {
        let hit, score = ui::strmatch_fuzzy_text("Hello World", q);
        print("fuzzy '", q, "': ", hit, if hit { str(" score ", score) } else { str("") });
    }
    print("misuses ", ui::misuse_count());
}
