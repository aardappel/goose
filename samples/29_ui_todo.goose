// A small to-do list, and a color mixer for what lies behind it, built with
// the ui module (docs/stdlib.md): Nuklear's immediate-mode widgets, drawn
// through gfx.
//
// Immediate mode means each frame the program says which windows and
// widgets there are, in order, and learns what the user did to them from
// the same calls: a button's returns true on the frame it is clicked, a
// check box flips the bool it is given, a text field edits the text it is
// given. The program keeps its own data as it likes; the ui keeps only
// where the windows are and what is open or active.
//
// Type a task and press Enter or Add, tick tasks off, pick which to show,
// and mix the backdrop. Escape or closing the window quits. `-- --frames N`
// stops after N frames. Either way the last frame is saved as ui_todo.png.
//
//   goose 29_ui_todo.goose
//
// Built from the generated C, the program links the ui layer and gfx's,
// whose paths `goose --ui-link` and `goose --gfx-link` print:
//   goose -o ui_todo.c 29_ui_todo.goose
//   cl ui_todo.c @<goose --ui-link msvc> @<goose --gfx-link msvc>
//   cc ui_todo.c -o ui_todo @<goose --ui-link cc> @<goose --gfx-link cc>

import std;
import vec;
import math;
import os;
import gfx;
import ui;

struct Task { text: u8[..40], done: bool }

fn task(text: const u8[:], done: bool) -> Task {
    var t = Task { text: [], done: done };
    t.text.append(text);
    t
}

let SHOW_ALL = 0;
let SHOW_TO_DO = 1;
let SHOW_DONE = 2;

fn main() {
    var frames = -1;
    let argv = args();
    if argv.len == 3 && argv[1] == "--frames" {
        let n, ok = parse_int(argv[2]);
        if ok { frames = n; }
    }
    guard gfx::open("Goose: a ui to-do list", 760, 520, gfx::WINDOW_RESIZABLE) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    // A context holds the ui's state; this one draws with Nuklear's own
    // font at its native 13 pixels. Its style is Nuklear's, but for a
    // brighter mark in ticked check boxes and chosen options.
    let ctx = ui::create(13.0);
    var colors = ui::default_color_table();
    colors[ui::COLOR_TOGGLE_CURSOR] = ui::rgb(120, 200, 140);
    ui::style_from_table(ctx, colors);

    var tasks: Task[>..] = [];
    tasks.push(task("Feed the goose", true));
    tasks.push(task("Read docs/stdlib.md", false));
    tasks.push(task("Draw a window with ui", false));
    var entry: u8[..40] = [];
    var show = SHOW_ALL;
    var backdrop = ui::Colorf { 0.18, 0.24, 0.30, 1.0 };
    let window = ui::WINDOW_BORDER | ui::WINDOW_MOVABLE | ui::WINDOW_SCALABLE |
                 ui::WINDOW_TITLE | ui::WINDOW_MINIMIZABLE;
    var drawn = 0;

    while gfx::frame() {
        if gfx::key_pressed("Escape") { break; }
        // The frame's keys, mouse and typing, from gfx.
        ui::input_from_gfx(ctx);
        var quit = false;

        if ui::begin(ctx, "To do", ui::rect(30, 30, 380, 330), window) {
            // A menu bar goes first in its window.
            ui::menubar_begin(ctx);
            ui::layout_row_static(ctx, 20.0, 60, 1);
            if ui::menu_begin_label(ctx, "Tasks", ui::TEXT_LEFT, float2 { 140.0, 100.0 }) {
                ui::layout_row_dynamic(ctx, 22.0, 1);
                if ui::menu_item_label(ctx, "Tick them all", ui::TEXT_LEFT) {
                    for i in tasks.len { tasks[i].done = true; }
                }
                if ui::menu_item_label(ctx, "Clear the done", ui::TEXT_LEFT) {
                    var i = tasks.len;
                    while i > 0 {
                        i--;
                        if tasks[i].done { remove_at(tasks, i); }
                    }
                }
                if ui::menu_item_label(ctx, "Quit", ui::TEXT_LEFT) { quit = true; }
                ui::menu_end(ctx);
            }
            ui::menubar_end(ctx);

            // The text field takes three quarters of the row, the button
            // the rest. Enter commits the field as the button does.
            let ratios: f32[2] = [0.75, 0.25];
            ui::layout_row(ctx, ui::DYNAMIC, 28.0, ratios);
            let edited = ui::edit_string(ctx, ui::EDIT_FIELD | ui::EDIT_SIG_ENTER, entry,
                                         ui::FILTER_DEFAULT);
            // A tooltip is asked for before the widget it is about.
            if ui::widget_is_hovered(ctx) { ui::tooltip(ctx, "Adds the task (Enter does too)"); }
            let add = ui::button_label(ctx, "Add");
            if (add || (edited & ui::EDIT_COMMITTED) != 0) && entry.len > 0 {
                tasks.push(Task { text: entry, done: false });
                entry.resize(0);
            }

            // Which tasks show: one of three.
            ui::layout_row_dynamic(ctx, 24.0, 3);
            if ui::option_label(ctx, "All", show == SHOW_ALL) { show = SHOW_ALL; }
            if ui::option_label(ctx, "To do", show == SHOW_TO_DO) { show = SHOW_TO_DO; }
            if ui::option_label(ctx, "Done", show == SHOW_DONE) { show = SHOW_DONE; }

            // A check box per task, with a button that removes it.
            let row: f32[2] = [0.88, 0.12];
            var remove = -1;
            for i in tasks.len {
                let done = tasks[i].done;
                if (show == SHOW_TO_DO && done) || (show == SHOW_DONE && !done) { continue; }
                ui::layout_row(ctx, ui::DYNAMIC, 24.0, row);
                let text = tasks[i].text;
                ui::checkbox_label(ctx, text, tasks[i].done);
                if ui::button_symbol(ctx, ui::SYMBOL_X) { remove = i; }
            }
            if remove >= 0 { remove_at(tasks, remove); }

            var finished = 0;
            for t in tasks { if t.done { finished++; } }
            ui::layout_row_dynamic(ctx, 22.0, 1);
            ui::label(ctx, str(finished, " of ", tasks.len, " done"), ui::TEXT_LEFT);
            ui::progress(ctx, finished, max(tasks.len, 1), false);
        }
        ui::end(ctx);

        if ui::begin(ctx, "Backdrop", ui::rect(440, 30, 290, 330), window) {
            ui::layout_row_dynamic(ctx, 20.0, 1);
            ui::label(ctx, "The color behind the windows:", ui::TEXT_LEFT);
            ui::layout_row_dynamic(ctx, 150.0, 1);
            backdrop = ui::color_picker(ctx, backdrop, ui::RGB);
            // Properties: drag them, click their arrows, or type a value.
            ui::layout_row_dynamic(ctx, 24.0, 1);
            ui::property_float(ctx, "#Red:", 0.0, backdrop.r, 1.0, 0.01, 0.005);
            ui::property_float(ctx, "#Green:", 0.0, backdrop.g, 1.0, 0.01, 0.005);
            ui::property_float(ctx, "#Blue:", 0.0, backdrop.b, 1.0, 0.01, 0.005);
        }
        ui::end(ctx);

        // The backdrop, then the windows over it.
        gfx::begin_screen(float4 { backdrop.r, backdrop.g, backdrop.b, 1.0 });
        gfx::end_pass();
        ui::render(ctx);
        drawn++;
        if quit || drawn == frames { break; }
    }

    let saved = gfx::screenshot("ui_todo.png");
    var finished = 0;
    for t in tasks { if t.done { finished++; } }
    print("drew ", drawn, " frames; ", finished, " of ", tasks.len,
          " tasks done; saved ui_todo.png: ", saved);
    ui::destroy(ctx);
    gfx::close();
}
