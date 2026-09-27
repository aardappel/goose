// What the ui module draws, and the input widgets read. Each canvas shape,
// image and text becomes a command, listed with its points and text, and
// the scissor clips them; the frame converts to vertices, indices and draw
// calls, which change with the conversion's settings; and the input queries
// answer for the mouse, clicks in rectangles, hovering, hovering for a
// while (counted in the program's timers), and scrolling.
import std;
import vec;
import math;
import ui;

fn col(c: ui::Color) -> u8[>..] { str(c.r, " ", c.g, " ", c.b, " ", c.a) }

fn pt(p: int2) -> u8[>..] { str(p.x, ",", p.y) }

fn describe(c: ui::Command, points: const int2[:], text: const u8[:]) -> u8[>..] {
    let box = str(c.x, " ", c.y, " ", c.w, " x ", c.h);
    let k = c.kind;
    if k == ui::COMMAND_SCISSOR { return str("scissor ", box); }
    if k == ui::COMMAND_LINE {
        return str("line ", pt(c.points[0]), " to ", pt(c.points[1]), " thickness ",
                   c.line_thickness, " color ", col(c.color));
    }
    if k == ui::COMMAND_CURVE {
        return str("curve ", pt(c.points[0]), " ", pt(c.points[1]), " ", pt(c.points[2]), " ",
                   pt(c.points[3]), " thickness ", c.line_thickness, " color ", col(c.color));
    }
    if k == ui::COMMAND_RECT {
        return str("rect ", box, " rounding ", c.rounding, " thickness ", c.line_thickness,
                   " color ", col(c.color));
    }
    if k == ui::COMMAND_RECT_FILLED {
        return str("filled rect ", box, " rounding ", c.rounding, " color ", col(c.color));
    }
    if k == ui::COMMAND_RECT_MULTI_COLOR {
        return str("multi-color rect ", box, " left ", col(c.left), " top ", col(c.top),
                   " right ", col(c.right), " bottom ", col(c.bottom));
    }
    if k == ui::COMMAND_CIRCLE {
        return str("circle ", box, " thickness ", c.line_thickness, " color ", col(c.color));
    }
    if k == ui::COMMAND_CIRCLE_FILLED { return str("filled circle ", box, " color ", col(c.color)); }
    if k == ui::COMMAND_ARC || k == ui::COMMAND_ARC_FILLED {
        return str(if k == ui::COMMAND_ARC { "arc" } else { "filled arc" }, " at ", c.x, ",", c.y,
                   " radius ", c.w, " from ", round(c.a_min * 100.0) as! i64, " to ",
                   round(c.a_max * 100.0) as! i64, " hundredths",
                   if k == ui::COMMAND_ARC { str(" thickness ", c.line_thickness) } else { str("") },
                   " color ", col(c.color));
    }
    if k == ui::COMMAND_TRIANGLE || k == ui::COMMAND_TRIANGLE_FILLED {
        return str(if k == ui::COMMAND_TRIANGLE { "triangle " } else { "filled triangle " },
                   pt(c.points[0]), " ", pt(c.points[1]), " ", pt(c.points[2]),
                   if k == ui::COMMAND_TRIANGLE { str(" thickness ", c.line_thickness) } else { str("") },
                   " color ", col(c.color));
    }
    if k == ui::COMMAND_POLYGON || k == ui::COMMAND_POLYGON_FILLED || k == ui::COMMAND_POLYLINE {
        var out = str(if k == ui::COMMAND_POLYGON { "polygon" }
                      else if k == ui::COMMAND_POLYLINE { "polyline" } else { "filled polygon" });
        for i in c.count { out.append(str(" ", pt(points[c.first + i]))); }
        if k != ui::COMMAND_POLYGON_FILLED { out.append(str(" thickness ", c.line_thickness)); }
        out.append(str(" color ", col(c.color)));
        return out;
    }
    if k == ui::COMMAND_TEXT {
        return str("text '", text[c.first..c.first + c.count], "' ", box, " height ", c.height,
                   " on ", col(c.background), " in ", col(c.color));
    }
    if k == ui::COMMAND_IMAGE {
        return str("image ", c.image.texture, " ", box, " part ", c.image.w, " x ", c.image.h,
                   " at ", c.image.region[0], ",", c.image.region[1], " tint ", col(c.color));
    }
    str("command ", k)
}

fn main() {
    let red = ui::rgb(255, 0, 0);
    let green = ui::rgb(0, 255, 0);
    let blue = ui::rgb(0, 0, 255);
    let white = ui::rgb(255, 255, 255);
    let clear = ui::rgba(0, 0, 0, 0);
    let ctx = ui::create(13.0);

    // --- the canvas ---
    ui::input_begin(ctx);
    ui::input_end(ctx);
    if ui::begin(ctx, "Canvas", ui::rect(10, 10, 300, 300), ui::WINDOW_NO_SCROLLBAR) {
        ui::layout_row_dynamic(ctx, 250.0, 1);
        // Read-only: the mouse is not over it.
        let state, r = ui::widget(ctx);
        let states: const u8[:][4] = ["invalid", "valid", "read-only", "disabled"];
        print("widget ", states[state], " at ", r.x, " ", r.y, " ", r.w, " x ", r.h);
        ui::stroke_line(ctx, 20.0, 30.0, 60.0, 70.0, 2.0, red);
        ui::stroke_curve(ctx, 20.0, 80.0, 40.0, 60.0, 60.0, 100.0, 80.0, 80.0, 1.0, green);
        ui::stroke_rect(ctx, ui::rect(100, 30, 40, 20), 3.0, 1.0, blue);
        ui::fill_rect(ctx, ui::rect(100, 60, 40, 20), 0.0, blue);
        ui::fill_rect_multi_color(ctx, ui::rect(150, 30, 40, 40), red, green, blue, white);
        ui::stroke_circle(ctx, ui::rect(200, 30, 30, 30), 2.0, green);
        ui::fill_circle(ctx, ui::rect(240, 30, 30, 30), green);
        ui::stroke_arc(ctx, 50.0, 150.0, 20.0, 0.0, PI as! f32, 1.0, red);
        ui::fill_arc(ctx, 100.0, 150.0, 20.0, 0.0, (PI / 2.0) as! f32, red);
        ui::stroke_triangle(ctx, 150.0, 130.0, 170.0, 170.0, 130.0, 170.0, 1.0, blue);
        ui::fill_triangle(ctx, 200.0, 130.0, 220.0, 170.0, 180.0, 170.0, blue);
        let pts: float2[4] = [float2 { 240.0, 130.0 }, float2 { 280.0, 130.0 },
                              float2 { 280.0, 170.0 }, float2 { 250.0, 160.0 }];
        ui::stroke_polyline(ctx, pts, 1.0, white);
        ui::stroke_polygon(ctx, pts, 2.0, white);
        ui::fill_polygon(ctx, pts, white);
        ui::draw_image(ctx, ui::rect(20, 190, 32, 32), ui::image_id(77), white);
        ui::draw_image(ctx, ui::rect(60, 190, 32, 32),
                       ui::subimage_id(77, 64, 64, ui::rect(16, 16, 32, 32)), red);
        ui::draw_nine_slice(ctx, ui::rect(100, 190, 60, 30), ui::nine_slice_id(78, 4, 4, 4, 4),
                            white);
        ui::draw_text(ctx, ui::rect(170, 190, 100, 20), "canvas text", ui::font(ctx), clear, white);
        ui::push_scissor(ctx, ui::rect(0, 0, 50, 50));
        ui::fill_rect(ctx, ui::rect(100, 100, 10, 10), 0.0, red);
        ui::fill_rect(ctx, ui::rect(20, 20, 10, 10), 0.0, green);
    }
    ui::end(ctx);
    let cmds = ui::commands(ctx);
    let points = ui::command_points(ctx);
    let text = ui::command_text(ctx);
    print(cmds.len, " commands, ", points.len, " polygon points, ", text.len, " bytes of text");
    for c in cmds { print("  ", describe(c, points, text)); }

    // --- converted into triangles ---
    // Shapes draw with the atlas's texture, which is none until a renderer
    // makes one.
    let null_tex = ui::null_texture(ctx);
    print("null texture ", null_tex, ", the atlas's ", null_tex == ui::texture(ui::context_atlas(ctx)));
    let configs: ui::ConvertConfig[4] = [
        ui::ConvertConfig {},
        ui::ConvertConfig { line_aa: false, shape_aa: false },
        ui::ConvertConfig { circle_segment_count: 64, arc_segment_count: 64, curve_segment_count: 64 },
        ui::ConvertConfig { global_alpha: 0.5 },
    ];
    let names: const u8[:][4] = ["default", "no anti-aliasing", "finer curves", "half alpha"];
    for i in 4 {
        let result = ui::convert(ctx, configs[i]);
        let verts = ui::vertices(ctx);
        let idx = ui::indices(ctx);
        let draws = ui::draw_commands(ctx);
        var total = 0;
        var in_range = true;
        var textures: u8[>..] = [];
        for d in draws {
            total += d.elem_count as i64;
            let t = if d.texture == null_tex { str("null") } else { str(d.texture) };
            if textures.len == 0 || !textures.ends_with(t) {
                textures.append(str(if textures.len > 0 { " " } else { "" }, t));
            }
        }
        for n in idx { if n as i64 >= verts.len { in_range = false; } }
        // The first vertex is the window background's, which global alpha
        // fades (images and multi-color rectangles it leaves as they are).
        print(names[i], ": result ", result, ", ", verts.len, " vertices, ", idx.len,
              " indices in ", draws.len, " draws (indices counted ", total == idx.len,
              ", in range ", in_range, "), textures ", textures, ", background ",
              col(verts[0].color));
    }
    let draws = ui::draw_commands(ctx);
    print("first draw clipped to ", draws[0].clip_rect.x, " ", draws[0].clip_rect.y, " ",
          draws[0].clip_rect.w, " x ", draws[0].clip_rect.h);
    ui::clear(ctx);
    print("after clear: ", ui::commands(ctx).len, " commands, ", ui::vertices(ctx).len,
          " vertices");

    // --- input queries ---
    let target = ui::rect(20, 30, 20, 20);
    let away = ui::rect(100, 100, 20, 20);
    let start = ui::rect(0, 0, 15, 15);
    var delay_timer: f32 = 0.0;
    var still_timer: f32 = 0.0;
    var clicked_timer: f32 = 0.0;
    var clicked = false;
    // The mouse: at 10,10; onto the target with the left button pressed;
    // released; held still for a few frames; clicked again; moved within. A
    // press or release zeroes the frame's motion (Nuklear's fix for touch
    // drags); a click is the release.
    let xs: i64[9] = [10, 30, 30, 30, 30, 30, 30, 30, 32];
    let ys: i64[9] = [10, 40, 40, 40, 40, 40, 40, 40, 41];
    let downs: bool[9] = [false, true, false, false, false, false, true, false, false];
    for step in 9 {
        ui::input_begin(ctx);
        ui::input_motion(ctx, xs[step], ys[step]);
        ui::input_button(ctx, ui::BUTTON_LEFT, xs[step], ys[step], downs[step]);
        if step == 2 { ui::input_scroll(ctx, float2 { 0.0, 2.0 }); }
        ui::input_end(ctx);
        ui::set_delta_time(ctx, 0.25);
        let m = ui::input_mouse(ctx);
        print("step ", step, ": mouse ", m.pos.x, ",", m.pos.y, " from ", m.prev.x, ",", m.prev.y,
              " moved ", m.delta.x, ",", m.delta.y, " scrolled ", m.scroll_delta.y, "; moved ",
              ui::input_is_mouse_moved(ctx), ", down ", ui::input_is_mouse_down(ctx, ui::BUTTON_LEFT),
              ", pressed ", ui::input_is_mouse_pressed(ctx, ui::BUTTON_LEFT), ", released ",
              ui::input_is_mouse_released(ctx, ui::BUTTON_LEFT));
        print("  clicks: any ", ui::input_has_mouse_click(ctx, ui::BUTTON_LEFT), ", in target ",
              ui::input_has_mouse_click_in_rect(ctx, ui::BUTTON_LEFT, target), ", away ",
              ui::input_has_mouse_click_in_rect(ctx, ui::BUTTON_LEFT, away), ", in its button ",
              ui::input_has_mouse_click_in_button_rect(ctx, ui::BUTTON_LEFT, target),
              ", down in it ", ui::input_has_mouse_click_down_in_rect(ctx, ui::BUTTON_LEFT, target, true),
              ", up in it ", ui::input_has_mouse_click_down_in_rect(ctx, ui::BUTTON_LEFT, target, false));
        print("  this frame: clicked in target ",
              ui::input_is_mouse_click_in_rect(ctx, ui::BUTTON_LEFT, target), ", down ",
              ui::input_is_mouse_click_down_in_rect(ctx, ui::BUTTON_LEFT, target, true),
              ", any button ", ui::input_any_mouse_click_in_rect(ctx, target), ", hovered and clicked ",
              ui::input_mouse_clicked(ctx, ui::BUTTON_LEFT, target));
        print("  hovering: target ", ui::input_is_mouse_hovering_rect(ctx, target), ", before ",
              ui::input_is_mouse_prev_hovering_rect(ctx, start), ", still ",
              ui::input_is_mouse_hovering_still_rect(ctx, target), "; for 0.6s ",
              ui::input_is_mouse_hovering_delay_rect(ctx, target, delay_timer, 0.6), " (", delay_timer,
              "), still ", ui::input_is_mouse_hovering_still_delay_rect(ctx, target, still_timer, 0.6),
              " (", still_timer, "), unless clicked ",
              ui::input_is_mouse_hovering_still_delay_clicked_rect(ctx, target, clicked_timer, 0.6,
                                                                  clicked),
              " (", clicked_timer, if clicked { ", clicked)" } else { ")" });
        if step == 6 { clicked = true; }
    }
    ui::destroy(ctx);
    print("misuses ", ui::misuse_count());
}
