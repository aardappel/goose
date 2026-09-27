// The ui module's widgets, used through injected input the way a user
// would: text in every form and what Nuklear's value widgets write, links
// and buttons of every kind clicked (styled ones, and one repeating while
// held), check boxes, radio buttons and selectables toggled, sliders and
// knobs dragged, a progress bar clicked, properties stepped with their
// arrows and dragged, and a color picker's hue changed.
import std;
import vec;
import math;
import ui;

fn n(x: f32) -> u8[>..] {
    let k = round(x as f64 * 100.0) as i64;
    if k % 100 == 0 { return str(k / 100); }
    var s: u8[>..] = [];
    if k < 0 { s.push('-'); }
    format_int(s, abs(k) / 100, 10, 0, ' ');
    s.push('.');
    format_int(s, abs(k) % 100, 10, 2, '0');
    return s;
}

fn input(ctx: ui::Context, x: f32, y: f32, down: bool) {
    ui::input_begin(ctx);
    ui::input_motion(ctx, x as! i64, y as! i64);
    ui::input_button(ctx, ui::BUTTON_LEFT, x as! i64, y as! i64, down);
    ui::input_end(ctx);
}

// One frame of one window, the mouse at x, y with the left button as
// given, its contents made by the block.
fn frame<F>(ctx: ui::Context, x: f32, y: f32, down: bool) {
    input(ctx, x, y, down);
    if ui::begin(ctx, "Widgets", ui::rect(0, 0, 400, 700), ui::WINDOW_NO_SCROLLBAR) { F(); }
    ui::end(ctx);
}

fn mid(r: ui::Rect) -> float2 { float2 { r.x + r.w / 2.0, r.y + r.h / 2.0 } }

// The texts the frame drew, in order.
fn texts(ctx: ui::Context) -> u8[>..] {
    let text = ui::command_text(ctx);
    var out: u8[>..] = [];
    for c in ui::commands(ctx) {
        if c.kind == ui::COMMAND_TEXT {
            if out.len > 0 { out.append(" | "); }
            out.append(text[(c.first as i64)..((c.first + c.count) as i64)]);
        }
    }
    return out;
}

fn main() {
    let ctx = ui::create(13.0);
    let far = float2 { 900.0, 900.0 };

    // --- text: labels, values and links, as drawn ---
    frame(ctx, far.x, far.y, false) {
        ui::layout_row_dynamic(ctx, 20.0, 1);
        ui::label(ctx, "left", ui::TEXT_LEFT);
        ui::label(ctx, "centered", ui::TEXT_CENTERED);
        ui::label(ctx, "right", ui::TEXT_RIGHT);
        ui::label_colored(ctx, "red", ui::TEXT_LEFT, ui::rgb(255, 0, 0));
        ui::layout_row_dynamic(ctx, 40.0, 1);
        ui::label_wrap(ctx, "a long line that has to wrap onto the next one in this window");
        ui::label_colored_wrap(ctx, "wrapped and green", ui::rgb(0, 255, 0));
        ui::layout_row_dynamic(ctx, 20.0, 2);
        ui::image(ctx, ui::image_id(5));
        ui::image_color(ctx, ui::image_id(5), ui::rgba(255, 255, 255, 128));
        ui::layout_row_dynamic(ctx, 20.0, 1);
        ui::value_bool(ctx, "flag", true);
        ui::value_int(ctx, "count", -42);
        ui::value_uint(ctx, "big", 4000000000);
        ui::value_float(ctx, "ratio", 0.125);
        ui::value_color_byte(ctx, "bytes", ui::rgba(1, 2, 3, 4));
        ui::value_color_float(ctx, "floats", ui::rgba(255, 0, 51, 255));
        ui::value_color_hex(ctx, "hex", ui::rgba(18, 52, 86, 120));
        ui::link_label(ctx, "a link", ui::TEXT_LEFT);
    };
    print("texts: ", texts(ctx));
    var images = 0;
    for c in ui::commands(ctx) {
        if c.kind == ui::COMMAND_IMAGE {
            images++;
            print("image ", c.image.texture, " at ", c.x, ",", c.y, " tinted ", c.color.a);
        }
    }

    // --- links: each a button, clicked in turn ---
    var links: ui::Rect[5] = [ui::rect(0, 0, 0, 0); 5];
    var link_style = ui::style(ctx).link;
    link_style.text_normal = ui::rgb(0, 0, 255);
    var clicked: bool[5] = [false; 5];
    for step in 11 {
        // A link is clicked on its text, which starts at the row's left.
        let at = if step == 0 { far } else {
            float2 { links[(step - 1) / 2].x + 8.0, mid(links[(step - 1) / 2]).y } };
        let down = step > 0 && step % 2 == 1;
        frame(ctx, at.x, at.y, down) {
            ui::layout_row_dynamic(ctx, 20.0, 1);
            links[0] = ui::widget_bounds(ctx);
            if ui::link_label(ctx, "plain", ui::TEXT_LEFT) { clicked[0] = true; }
            links[1] = ui::widget_bounds(ctx);
            if ui::link_label_colored(ctx, "colored", ui::TEXT_LEFT, ui::rgb(0, 128, 0)) {
                clicked[1] = true;
            }
            links[2] = ui::widget_bounds(ctx);
            if ui::link_label_hover_underline(ctx, "hover underline", ui::TEXT_LEFT) {
                clicked[2] = true;
            }
            links[3] = ui::widget_bounds(ctx);
            if ui::link_label_no_underline(ctx, "no underline", ui::TEXT_LEFT) { clicked[3] = true; }
            links[4] = ui::widget_bounds(ctx);
            if ui::link_label_styled(ctx, link_style, "styled", ui::TEXT_LEFT) { clicked[4] = true; }
            ui::link_label_underline(ctx, "always underlined", ui::TEXT_LEFT,
                                     ui::LINK_UNDERLINE_ALWAYS);
        };
    }
    print("links clicked: ", clicked[0], " ", clicked[1], " ", clicked[2], " ", clicked[3], " ",
          clicked[4]);
    var lines = 0;
    for c in ui::commands(ctx) { if c.kind == ui::COMMAND_LINE { lines++; } }
    print("underlines drawn ", lines);

    // --- buttons of every kind, each clicked once ---
    var buttons: ui::Rect[11] = [ui::rect(0, 0, 0, 0); 11];
    var hits: i64[11] = [0; 11];
    var styled = ui::style(ctx).button;
    styled.normal = ui::style_item_color(ui::rgb(200, 30, 30));
    styled.rounding = 6.0;
    for step in 23 {
        let at = if step == 0 { far } else { mid(buttons[(step - 1) / 2]) };
        let down = step > 0 && step % 2 == 1;
        frame(ctx, at.x, at.y, down) {
            ui::layout_row_dynamic(ctx, 26.0, 2);
            buttons[0] = ui::widget_bounds(ctx);
            if ui::button_label(ctx, "label") { hits[0]++; }
            buttons[1] = ui::widget_bounds(ctx);
            if ui::button_color(ctx, ui::rgb(0, 200, 0)) { hits[1]++; }
            buttons[2] = ui::widget_bounds(ctx);
            if ui::button_symbol(ctx, ui::SYMBOL_PLUS) { hits[2]++; }
            buttons[3] = ui::widget_bounds(ctx);
            if ui::button_image(ctx, ui::image_id(5)) { hits[3]++; }
            buttons[4] = ui::widget_bounds(ctx);
            if ui::button_symbol_label(ctx, ui::SYMBOL_TRIANGLE_RIGHT, "symbol", ui::TEXT_RIGHT) {
                hits[4]++;
            }
            buttons[5] = ui::widget_bounds(ctx);
            if ui::button_image_label(ctx, ui::image_id(5), "image", ui::TEXT_RIGHT) { hits[5]++; }
            buttons[6] = ui::widget_bounds(ctx);
            if ui::button_label_styled(ctx, styled, "styled") { hits[6]++; }
            buttons[7] = ui::widget_bounds(ctx);
            if ui::button_symbol_styled(ctx, styled, ui::SYMBOL_X) { hits[7]++; }
            buttons[8] = ui::widget_bounds(ctx);
            if ui::button_image_styled(ctx, styled, ui::image_id(5)) { hits[8]++; }
            buttons[9] = ui::widget_bounds(ctx);
            if ui::button_symbol_label_styled(ctx, styled, ui::SYMBOL_MINUS, "both",
                                              ui::TEXT_LEFT) { hits[9]++; }
            buttons[10] = ui::widget_bounds(ctx);
            if ui::button_image_label_styled(ctx, styled, ui::image_id(5), "both",
                                             ui::TEXT_LEFT) { hits[10]++; }
        };
    }
    print("button clicks: ", hits[0], hits[1], hits[2], hits[3], hits[4], hits[5], hits[6],
          hits[7], hits[8], hits[9], hits[10]);
    var red = 0;
    for c in ui::commands(ctx) {
        if c.kind == ui::COMMAND_RECT_FILLED && c.color == ui::rgb(200, 30, 30) &&
           c.rounding == 6 { red++; }
    }
    print("styled backgrounds drawn ", red);

    // A repeater fires every frame it is held; pushed and popped around it.
    var repeats = 0;
    var held = ui::rect(0, 0, 0, 0);
    for step in 5 {
        let at = if step == 0 { far } else { mid(held) };
        frame(ctx, at.x, at.y, step >= 1 && step <= 3) {
            ui::layout_row_dynamic(ctx, 26.0, 1);
            print("push ", ui::button_push_behavior(ctx, ui::BUTTON_REPEATER));
            held = ui::widget_bounds(ctx);
            if ui::button_label(ctx, "repeat") { repeats++; }
            print("pop ", ui::button_pop_behavior(ctx));
        };
    }
    print("held for 3 frames: ", repeats, " repeats");
    ui::button_set_behavior(ctx, ui::BUTTON_REPEATER);
    ui::button_set_behavior(ctx, ui::BUTTON_DEFAULT);

    // --- check boxes, radio buttons and selectables, each clicked ---
    var toggles: ui::Rect[16] = [ui::rect(0, 0, 0, 0); 16];
    var check = false;
    var check_align = false;
    var flags = 1;
    var box = false;
    var box_align = false;
    var box_flags = 4;
    var radio = false;
    var radio_align = false;
    var option = false;
    var option_align = false;
    var sel = false;
    var sel_image = false;
    var sel_symbol = false;
    var select = false;
    var select_image = false;
    var select_symbol = false;
    var changed = 0;
    for step in 33 {
        let at = if step == 0 { far } else { mid(toggles[(step - 1) / 2]) };
        frame(ctx, at.x, at.y, step > 0 && step % 2 == 1) {
            ui::layout_row_dynamic(ctx, 22.0, 2);
            toggles[0] = ui::widget_bounds(ctx);
            check = ui::check_label(ctx, "check", check);
            toggles[1] = ui::widget_bounds(ctx);
            check_align = ui::check_label_align(ctx, "right", check_align, ui::WIDGET_RIGHT,
                                                ui::TEXT_RIGHT);
            toggles[2] = ui::widget_bounds(ctx);
            flags = ui::check_flags_label(ctx, "flag 2", flags, 2);
            toggles[3] = ui::widget_bounds(ctx);
            if ui::checkbox_label(ctx, "box", box) { changed++; }
            toggles[4] = ui::widget_bounds(ctx);
            if ui::checkbox_label_align(ctx, "box right", box_align, ui::WIDGET_RIGHT,
                                        ui::TEXT_RIGHT) { changed++; }
            toggles[5] = ui::widget_bounds(ctx);
            if ui::checkbox_flags_label(ctx, "flag 8", box_flags, 8) { changed++; }
            toggles[6] = ui::widget_bounds(ctx);
            if ui::radio_label(ctx, "radio", radio) { changed++; }
            toggles[7] = ui::widget_bounds(ctx);
            if ui::radio_label_align(ctx, "radio right", radio_align, ui::WIDGET_RIGHT,
                                     ui::TEXT_RIGHT) { changed++; }
            toggles[8] = ui::widget_bounds(ctx);
            option = ui::option_label(ctx, "option", option);
            toggles[9] = ui::widget_bounds(ctx);
            option_align = ui::option_label_align(ctx, "option right", option_align,
                                                  ui::WIDGET_RIGHT, ui::TEXT_RIGHT);
            toggles[10] = ui::widget_bounds(ctx);
            if ui::selectable_label(ctx, "selectable", ui::TEXT_LEFT, sel) { changed++; }
            toggles[11] = ui::widget_bounds(ctx);
            if ui::selectable_image_label(ctx, ui::image_id(5), "with image", ui::TEXT_LEFT,
                                          sel_image) { changed++; }
            toggles[12] = ui::widget_bounds(ctx);
            if ui::selectable_symbol_label(ctx, ui::SYMBOL_CIRCLE_SOLID, "with symbol",
                                           ui::TEXT_LEFT, sel_symbol) { changed++; }
            toggles[13] = ui::widget_bounds(ctx);
            select = ui::select_label(ctx, "select", ui::TEXT_LEFT, select);
            toggles[14] = ui::widget_bounds(ctx);
            select_image = ui::select_image_label(ctx, ui::image_id(5), "select image",
                                                  ui::TEXT_LEFT, select_image);
            toggles[15] = ui::widget_bounds(ctx);
            select_symbol = ui::select_symbol_label(ctx, ui::SYMBOL_RECT_SOLID, "select symbol",
                                                    ui::TEXT_LEFT, select_symbol);
        };
    }
    print("check ", check, " ", check_align, "; flags ", flags, "; box ", box, " ", box_align,
          "; box flags ", box_flags, "; radio ", radio, " ", radio_align, "; option ", option, " ",
          option_align);
    print("selectable ", sel, " ", sel_image, " ", sel_symbol, "; select ", select, " ",
          select_image, " ", select_symbol, "; changes reported ", changed);

    // --- sliders and knobs dragged, a progress bar clicked ---
    var slider_f: f32 = 0.5;
    var slider_i = 5;
    var slide_f: f32 = 2.0;
    var slide_i = 10;
    var knob_f: f32 = 0.0;
    var knob_i = 0;
    var cur = 20;
    var prog_value = 30;
    var rects: ui::Rect[6] = [ui::rect(0, 0, 0, 0); 6];
    var moved = 0;
    // Each is pressed at its middle, dragged three quarters of the way to its
    // right end, and let go.
    for step in 25 {
        let which = (step - 1) / 4;
        let phase = (step - 1) % 4;
        let r = if step == 0 { ui::rect(900, 900, 0, 0) } else { rects[which] };
        let x = if step == 0 { far.x } else if phase == 0 || phase == 1 { mid(r).x }
                else { r.x + r.w * 0.875 };
        frame(ctx, x, if step == 0 { far.y } else { mid(r).y }, step > 0 && phase >= 1 && phase <= 2) {
            ui::layout_row_dynamic(ctx, 30.0, 1);
            rects[0] = ui::widget_bounds(ctx);
            if ui::slider_float(ctx, 0.0, slider_f, 1.0, 0.1) { moved++; }
            rects[1] = ui::widget_bounds(ctx);
            if ui::slider_int(ctx, 0, slider_i, 10, 1) { moved++; }
            rects[2] = ui::widget_bounds(ctx);
            slide_f = ui::slide_float(ctx, 0.0, slide_f, 4.0, 0.5);
            rects[3] = ui::widget_bounds(ctx);
            slide_i = ui::slide_int(ctx, 0, slide_i, 20, 2);
            ui::layout_row_dynamic(ctx, 60.0, 2);
            rects[4] = ui::widget_bounds(ctx);
            if ui::knob_float(ctx, 0.0, knob_f, 1.0, 0.01, ui::DOWN, 30.0) { moved++; }
            rects[5] = ui::widget_bounds(ctx);
            if ui::knob_int(ctx, 0, knob_i, 100, 1, ui::DOWN, 30.0) { moved++; }
            ui::layout_row_dynamic(ctx, 20.0, 1);
            ui::progress(ctx, cur, 100, false);
            prog_value = ui::prog(ctx, prog_value, 100, false);
        };
    }
    print("slider ", n(slider_f), ", int ", slider_i, "; slide ", n(slide_f), ", int ", slide_i,
          "; knob ", n(knob_f), ", int ", knob_i, "; changes ", moved);
    var bar = ui::rect(0, 0, 0, 0);
    for step in 3 {
        let at = if step == 0 { far } else { float2 { bar.x + bar.w * 0.75, bar.y + bar.h / 2.0 } };
        frame(ctx, at.x, at.y, step == 1) {
            ui::layout_row_dynamic(ctx, 20.0, 1);
            bar = ui::widget_bounds(ctx);
            if ui::progress(ctx, cur, 100, true) { print("progress moved to ", cur); }
            prog_value = ui::prog(ctx, prog_value, 100, false);
        };
    }
    print("progress ", cur, " (clicked at three quarters), fixed one ", prog_value);
    // A value past the end is held at it, which counts as a change.
    var over = 250;
    var held = false;
    var prog_over = 0;
    frame(ctx, far.x, far.y, false) {
        ui::layout_row_dynamic(ctx, 20.0, 1);
        held = ui::progress(ctx, over, 100, false);
        prog_over = ui::prog(ctx, 250, 100, false);
    };
    print("progress of 250 of 100 held at ", over, ", changed ", held, "; fixed one ", prog_over);

    // --- properties: the right arrow steps up, a drag moves ---
    var pi = 10;
    var pf: f32 = 1.5;
    var pd = 100.0;
    var ri = 3;
    var rf: f32 = 0.25;
    var rd = 2.5;
    var props: ui::Rect[6] = [ui::rect(0, 0, 0, 0); 6];
    for step in 13 {
        let which = (step - 1) / 2;
        // Near the right end: the arrow that steps up.
        let at = if step == 0 { far } else {
            float2 { props[which].x + props[which].w - 6.0, mid(props[which]).y } };
        frame(ctx, at.x, at.y, step > 0 && step % 2 == 1) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            props[0] = ui::widget_bounds(ctx);
            ui::property_int(ctx, "int", 0, pi, 100, 5, 1.0);
            props[1] = ui::widget_bounds(ctx);
            ui::property_float(ctx, "float", 0.0, pf, 10.0, 0.5, 0.1);
            props[2] = ui::widget_bounds(ctx);
            ui::property_double(ctx, "#double", 0.0, pd, 1000.0, 25.0, 1.0);
            props[3] = ui::widget_bounds(ctx);
            ri = ui::propertyi(ctx, "i", 0, ri, 10, 1, 1.0);
            props[4] = ui::widget_bounds(ctx);
            rf = ui::propertyf(ctx, "f", 0.0, rf, 1.0, 0.25, 0.01);
            props[5] = ui::widget_bounds(ctx);
            rd = ui::propertyd(ctx, "d", 0.0, rd, 5.0, 0.5, 0.1);
        };
    }
    print("stepped: int ", pi, ", float ", n(pf), ", double ", n(pd as f32), "; i ", ri, ", f ",
          n(rf), ", d ", n(rd as f32));
    print("property texts: ", texts(ctx));

    // --- a color picker: its hue bar, a font height wide left of the space an
    // RGBA picker's alpha bar takes, clicked halfway down ---
    var color = ui::Colorf { 1.0, 0.0, 0.0, 1.0 };
    var picked = ui::Colorf { 0.0, 0.0, 1.0, 0.5 };
    var area = ui::rect(0, 0, 0, 0);
    for step in 3 {
        let at = if step == 0 { far } else { float2 { area.x + area.w - 20.0, area.y + area.h / 2.0 } };
        frame(ctx, at.x, at.y, step == 1) {
            ui::layout_row_dynamic(ctx, 120.0, 1);
            area = ui::widget_bounds(ctx);
            if ui::color_pick(ctx, color, ui::RGB) { print("color picked"); }
            picked = ui::color_picker(ctx, picked, ui::RGBA);
        };
    }
    let hsva = ui::colorf_hsva_f(color);
    print("hue now ", hsva.x > 0.3 && hsva.x < 0.7, ", still saturated ", n(hsva.y), ", alpha ",
          n(color.a), "; the other unchanged ", n(picked.b), " ", n(picked.a));

    input(ctx, far.x, far.y, false);
    ui::destroy(ctx);
    print("misuses ", ui::misuse_count());
}
