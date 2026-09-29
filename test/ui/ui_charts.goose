// Charts in the ui module, seen in the commands they draw: columns under
// the mouse -- hovered, pressed and clicked -- drawn highlighted; lines;
// charts in colors of the program's own, with a second slot added in its
// own colors or the style's; and whole charts from plot, flat ones too.
import std;
import vec;
import math;
import ui;

fn same(a: ui::Color, b: ui::Color) -> bool {
    a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a
}

// The commands of `kind` drawn in `color` last frame.
fn drawn(ctx: ui::Context, kind: i64, color: ui::Color) -> i64 {
    var n = 0;
    for c in ui::commands(ctx) {
        if c.kind == kind && same(c.color, color) { n += 1; }
    }
    n
}

fn or_nothing(text: const u8[:]) -> u8[>..] {
    if text.len > 0 { str(text) } else { str(" nothing") }
}

fn input(ctx: ui::Context, x: i64, y: i64, down: bool) {
    ui::input_begin(ctx);
    ui::input_motion(ctx, x, y);
    ui::input_button(ctx, ui::BUTTON_LEFT, x, y, down);
    ui::input_end(ctx);
}

fn main() {
    // Chart colors of their own, which nothing else draws in.
    let ctx = ui::create(13.0);
    let bar = ui::rgb(10, 20, 30);
    let highlight = ui::rgb(40, 50, 60);
    var style = ui::style(ctx);
    style.chart.color = bar;
    style.chart.selected_color = highlight;
    ui::set_style(ctx, style);
    let red = ui::rgb(255, 0, 0);
    let green = ui::rgb(0, 255, 0);
    let blue = ui::rgb(0, 0, 255);
    let white = ui::rgb(255, 255, 255);
    let values: f32[4] = [2.0, 8.0, 5.0, 10.0];

    // --- columns: nothing under the mouse, then column 2 hovered, pressed
    // and released: clicked ---
    var chart = ui::rect(0, 0, 0, 0);
    for step in 4 {
        let x = if step == 0 { 900 } else { (chart.x + chart.w * 2.5 / 4.0) as! i64 };
        let y = if step == 0 { 900 } else { (chart.y + chart.h - 6.0) as! i64 };
        input(ctx, x, y, step == 2);
        var events: u8[>..] = [];
        if ui::begin(ctx, "Charts", ui::rect(0, 0, 300, 300), 0) {
            ui::layout_row_dynamic(ctx, 100.0, 1);
            chart = ui::widget_bounds(ctx);
            if ui::chart_begin(ctx, ui::CHART_COLUMN, 4, 0.0, 10.0) {
                for i in 4 {
                    let r = ui::chart_push(ctx, values[i]);
                    if (r & ui::CHART_HOVERING) != 0 { events.append(str(" hovering ", i)); }
                    if (r & ui::CHART_CLICKED) != 0 { events.append(str(" clicked ", i)); }
                }
                ui::chart_end(ctx);
            }
        }
        ui::end(ctx);
        print("columns, step ", step, ":", or_nothing(events),
              "; bars ", drawn(ctx, ui::COMMAND_RECT_FILLED, bar), ", highlighted ",
              drawn(ctx, ui::COMMAND_RECT_FILLED, highlight));
    }

    // --- lines, and charts in the program's colors with a second slot ---
    input(ctx, 900, 900, false);
    if ui::begin(ctx, "Charts", ui::rect(0, 0, 300, 300), 0) {
        ui::layout_row_dynamic(ctx, 100.0, 1);
        if ui::chart_begin(ctx, ui::CHART_LINES, 4, 0.0, 10.0) {
            for v in values { ui::chart_push(ctx, v); }
            ui::chart_end(ctx);
        }
    }
    ui::end(ctx);
    print("lines: ", drawn(ctx, ui::COMMAND_LINE, bar), " segments");

    input(ctx, 900, 900, false);
    if ui::begin(ctx, "Charts", ui::rect(0, 0, 300, 300), 0) {
        ui::layout_row_dynamic(ctx, 100.0, 1);
        if ui::chart_begin_colored(ctx, ui::CHART_COLUMN, red, green, 3, 0.0, 10.0) {
            ui::chart_add_slot_colored(ctx, ui::CHART_LINES, blue, white, 3, 0.0, 10.0);
            ui::chart_add_slot(ctx, ui::CHART_LINES, 3, 0.0, 10.0);
            for i in 3 {
                ui::chart_push_slot(ctx, values[i], 0);
                ui::chart_push_slot(ctx, values[i + 1], 1);
                ui::chart_push_slot(ctx, 10.0 - values[i], 2);
            }
            ui::chart_end(ctx);
        }
    }
    ui::end(ctx);
    print("three slots: red columns ", drawn(ctx, ui::COMMAND_RECT_FILLED, red), ", blue lines ",
          drawn(ctx, ui::COMMAND_LINE, blue), ", lines in the style's color ",
          drawn(ctx, ui::COMMAND_LINE, bar));

    // --- plots ---
    // A line chart marks its points with small rectangles too.
    let flat: f32[3] = [3.0, 3.0, 3.0];
    var flat_at = ui::rect(0, 0, 0, 0);
    input(ctx, 900, 900, false);
    if ui::begin(ctx, "Charts", ui::rect(0, 0, 300, 300), 0) {
        ui::layout_row_dynamic(ctx, 50.0, 1);
        ui::plot(ctx, ui::CHART_COLUMN, values);
        ui::plot(ctx, ui::CHART_LINES, values);
        flat_at = ui::widget_bounds(ctx);
        ui::plot(ctx, ui::CHART_LINES, flat);
    }
    ui::end(ctx);
    print("plots: ", drawn(ctx, ui::COMMAND_RECT_FILLED, bar), " columns and point marks, ",
          drawn(ctx, ui::COMMAND_LINE, bar), " line segments");
    var level: u8[>..] = [];
    for c in ui::commands(ctx) {
        if c.kind == ui::COMMAND_LINE && c.points[0].y >= flat_at.y {
            level.append(str(" ", c.points[0].y == c.points[1].y));
        }
    }
    print("the flat plot's segments level:", level);
    ui::destroy(ctx);
    print("misuses ", ui::misuse_count());
}
