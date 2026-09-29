// What opens over a window in the ui module, driven by injected clicks:
// popups opened and closed by the program and scrolled, combo boxes of
// every kind opened and an item picked, a contextual menu opened by a right
// click, tooltips of text and of widgets, shown at once and after a delay,
// and a menu bar whose menus open and whose items are clicked.
import std;
import vec;
import math;
import ui;

fn input(ctx: ui::Context, x: f32, y: f32, button: i64, down: bool) {
    ui::input_begin(ctx);
    ui::input_motion(ctx, x as! i64, y as! i64);
    ui::input_button(ctx, button, x as! i64, y as! i64, down);
    ui::input_end(ctx);
}

// A frame of one window, the mouse at `at` with the button as given.
fn frame<F>(ctx: ui::Context, name: const u8[:], at: float2, button: i64, down: bool) {
    input(ctx, at.x, at.y, button, down);
    if ui::begin(ctx, name, ui::rect(0, 0, 500, 500), ui::WINDOW_NO_SCROLLBAR) { F(); }
    ui::end(ctx);
}

fn mid(r: ui::Rect) -> float2 { float2 { r.x + r.w / 2.0, r.y + r.h / 2.0 } }

// Where the frame last drew `label`, from its command list: the middle of
// its text's box. Popups are drawn after the window under them, so an item
// in an open list is found rather than a closed combo box showing the same.
fn text_at(ctx: ui::Context, label: const u8[:]) -> float2 {
    let text = ui::command_text(ctx);
    var at = float2 { -100.0, -100.0 };
    for c in ui::commands(ctx) {
        if c.kind == ui::COMMAND_TEXT &&
           text[(c.first as i64)..((c.first + c.count) as i64)] == label {
            at = float2 { c.x + 4.0, c.y + c.h / 2.0 };
        }
    }
    return at;
}

fn drew(ctx: ui::Context, label: const u8[:]) -> bool { text_at(ctx, label).x >= 0.0 }

let LEFT: i64 = 0;
let RIGHT: i64 = 2;

fn main() {
    let ctx = ui::create(13.0);
    let far = float2 { 900.0, 900.0 };

    // --- a popup the program opens, closed by a button in it ---
    var show = true;
    var close_at = far;
    for step in 4 {
        let at = if step == 2 { close_at } else { far };
        frame(ctx, "Popup", at, LEFT, step == 2) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            ui::label(ctx, "under the popup", ui::TEXT_LEFT);
            if show {
                let open = ui::popup_begin(ctx, ui::POPUP_STATIC, "Static popup",
                                           ui::WINDOW_TITLE | ui::WINDOW_CLOSABLE,
                                           ui::rect(20, 40, 200, 150));
                if open {
                    ui::layout_row_dynamic(ctx, 25.0, 1);
                    if ui::button_label(ctx, "Close") {
                        show = false;
                        ui::popup_close(ctx);
                    }
                    ui::popup_set_scroll(ctx, 0, 0);
                    let s = ui::popup_get_scroll(ctx);
                    print("step ", step, ": popup open, scroll ", s.x, ",", s.y);
                    ui::popup_end(ctx);
                } else {
                    print("step ", step, ": popup not open");
                }
            }
        };
        if step == 1 { close_at = text_at(ctx, "Close"); }
    }
    print("closed by its button: show ", show);
    // A dynamic popup, as high as what is in it.
    frame(ctx, "Dynamic popup", far, LEFT, false) {
        ui::layout_row_dynamic(ctx, 25.0, 1);
        if ui::popup_begin(ctx, ui::POPUP_DYNAMIC, "Dynamic", 0, ui::rect(10, 10, 150, 300)) {
            ui::layout_row_dynamic(ctx, 20.0, 1);
            ui::label(ctx, "one line", ui::TEXT_LEFT);
            print("dynamic popup open");
            ui::popup_close(ctx);
            ui::popup_end(ctx);
        }
    };

    // --- combo boxes: each opened by a click, and an item clicked ---
    let items: const u8[:][3] = ["Red", "Green", "Blue"];
    var sel_combo = 0;
    var sel_box = 0;
    var sel_string = 0;
    var sel_boxstring = 0;
    var sel_sep = 0;
    var sel_boxsep = 0;
    var heads: ui::Rect[6] = [ui::rect(0, 0, 0, 0); 6];
    var box_changed = 0;
    var target = far;
    for step in 25 {
        let which = (step - 1) / 4;
        let phase = (step - 1) % 4;
        // Each combo: its box clicked (press, release), then "Blue" (press,
        // release) in the list it opened.
        let at = if step == 0 { far } else if phase < 2 { mid(heads[which]) } else { target };
        frame(ctx, "Combos", at, LEFT, step > 0 && phase % 2 == 0) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            heads[0] = ui::widget_bounds(ctx);
            sel_combo = ui::combo(ctx, items, sel_combo, 20, float2 { 200.0, 200.0 });
            heads[1] = ui::widget_bounds(ctx);
            if ui::combobox(ctx, items, sel_box, 20, float2 { 200.0, 200.0 }) { box_changed++; }
            heads[2] = ui::widget_bounds(ctx);
            let zeros: u8[14] = ['R', 'e', 'd', 0, 'G', 'r', 'e', 'e', 'n', 0, 'B', 'l', 'u', 'e'];
            sel_string = ui::combo_string(ctx, zeros, sel_string, 3, 20, float2 { 200.0, 200.0 });
            heads[3] = ui::widget_bounds(ctx);
            if ui::combobox_string(ctx, zeros, sel_boxstring, 3, 20, float2 { 200.0, 200.0 }) {
                box_changed++;
            }
            heads[4] = ui::widget_bounds(ctx);
            sel_sep = ui::combo_separator(ctx, "Red|Green|Blue", '|', sel_sep, 3, 20,
                                          float2 { 200.0, 200.0 });
            heads[5] = ui::widget_bounds(ctx);
            if ui::combobox_separator(ctx, "Red;Green;Blue", ';', sel_boxsep, 3, 20,
                                      float2 { 200.0, 200.0 }) { box_changed++; }
        };
        if phase == 1 { target = text_at(ctx, "Blue"); }
    }
    print("combos picked Blue: ", sel_combo, " ", sel_box, " ", sel_string, " ", sel_boxstring,
          " ", sel_sep, " ", sel_boxsep, "; boxes changed ", box_changed);

    // Combo boxes of the program's own: every kind of box, and every kind of
    // item, the last one picked.
    var own: ui::Rect[6] = [ui::rect(0, 0, 0, 0); 6];
    var picked: i64[6] = [-1; 6];
    for step in 25 {
        let which = (step - 1) / 4;
        let phase = (step - 1) % 4;
        let at = if step == 0 { far } else if phase < 2 { mid(own[which]) } else { target };
        frame(ctx, "Own combos", at, LEFT, step > 0 && phase % 2 == 0) {
            ui::layout_row_dynamic(ctx, 25.0, 2);
            for k in 6 {
                own[k] = ui::widget_bounds(ctx);
                let size = float2 { 180.0, 120.0 };
                let open = match k {
                    0 => ui::combo_begin_label(ctx, "label", size),
                    1 => ui::combo_begin_color(ctx, ui::rgb(0, 0, 200), size),
                    2 => ui::combo_begin_symbol(ctx, ui::SYMBOL_CIRCLE_SOLID, size),
                    3 => ui::combo_begin_symbol_label(ctx, "symbol", ui::SYMBOL_PLUS, size),
                    4 => ui::combo_begin_image(ctx, ui::image_id(9), size),
                    _ => ui::combo_begin_image_label(ctx, "image", ui::image_id(9), size),
                };
                if open {
                    ui::layout_row_dynamic(ctx, 20.0, 1);
                    if ui::combo_item_label(ctx, "first", ui::TEXT_LEFT) { picked[k] = 0; }
                    if ui::combo_item_image_label(ctx, ui::image_id(9), "second", ui::TEXT_LEFT) {
                        picked[k] = 1;
                    }
                    if ui::combo_item_symbol_label(ctx, ui::SYMBOL_X, "third", ui::TEXT_LEFT) {
                        picked[k] = 2;
                    }
                    ui::combo_end(ctx);
                }
            }
        };
        if phase == 1 { target = text_at(ctx, "third"); }
    }
    print("own combos picked: ", picked[0], picked[1], picked[2], picked[3], picked[4], picked[5]);
    // combo_close shuts one from inside.
    var closer = ui::rect(0, 0, 0, 0);
    for step in 5 {
        frame(ctx, "Own combos", if step == 1 || step == 2 { mid(closer) } else { far }, LEFT,
              step == 1) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            closer = ui::widget_bounds(ctx);
            if ui::combo_begin_label(ctx, "closes itself", float2 { 180.0, 100.0 }) {
                print("step ", step, ": open, closing it");
                ui::combo_close(ctx);
                ui::combo_end(ctx);
            }
        };
    }

    // --- a contextual menu: a right click on the area opens it ---
    var area = ui::rect(0, 0, 0, 0);
    var chosen = "";
    for step in 7 {
        let at = if step == 1 || step == 2 { mid(area) } else if step >= 4 { target } else { far };
        let button = if step == 1 || step == 2 { RIGHT } else { LEFT };
        frame(ctx, "Contextual", at, button, step == 1 || step == 5) {
            ui::layout_row_dynamic(ctx, 100.0, 1);
            area = ui::widget_bounds(ctx);
            ui::label(ctx, "right-click me", ui::TEXT_CENTERED);
            if ui::contextual_begin(ctx, 0, float2 { 150.0, 200.0 }, area) {
                ui::layout_row_dynamic(ctx, 20.0, 1);
                if ui::contextual_item_label(ctx, "Cut", ui::TEXT_LEFT) { chosen = "Cut"; }
                if ui::contextual_item_image_label(ctx, ui::image_id(9), "Copy", ui::TEXT_LEFT) {
                    chosen = "Copy";
                }
                if ui::contextual_item_symbol_label(ctx, ui::SYMBOL_PLUS, "Paste", ui::TEXT_LEFT) {
                    chosen = "Paste";
                }
                if step == 6 { ui::contextual_close(ctx); }
                print("step ", step, ": contextual open");
                ui::contextual_end(ctx);
            }
        };
        if step == 3 { target = text_at(ctx, "Copy"); }
    }
    print("contextual menu chose ", chosen);

    // --- tooltips ---
    var hot = ui::rect(0, 0, 0, 0);
    var timer: f32 = 0.0;
    var clicked = false;
    for step in 6 {
        frame(ctx, "Tooltips", if step == 0 { far } else { mid(hot) }, LEFT, false) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            hot = ui::widget_bounds(ctx);
            ui::label(ctx, "hover me", ui::TEXT_LEFT);
            if step == 1 && ui::input_is_mouse_hovering_rect(ctx, hot) {
                ui::tooltip(ctx, "a plain tooltip");
            }
            if step == 2 {
                ui::tooltip_offset(ctx, "an offset tooltip", ui::BOTTOM_RIGHT, float2 { 5.0, 5.0 });
            }
            if step == 3 && ui::tooltip_begin(ctx, 150.0) {
                ui::layout_row_dynamic(ctx, 20.0, 1);
                ui::label(ctx, "a widget tooltip", ui::TEXT_LEFT);
                ui::tooltip_end(ctx);
            }
            if step == 4 && ui::tooltip_begin_offset(ctx, 150.0, ui::TOP_CENTER,
                                                     float2 { 0.0, -10.0 }) {
                ui::layout_row_dynamic(ctx, 20.0, 1);
                ui::label(ctx, "above the pointer", ui::TEXT_LEFT);
                ui::tooltip_end(ctx);
            }
            if step == 5 { ui::do_tooltip(ctx, "while over it", hot); }
        };
        let names: const u8[:][6] = ["", "a plain tooltip", "an offset tooltip", "a widget tooltip",
                                     "above the pointer", "while over it"];
        if step > 0 { print("tooltip '", names[step], "' drawn: ", drew(ctx, names[step])); }
    }
    // After the style's delay of still hovering: 0.2 seconds a frame.
    for step in 5 {
        ui::set_delta_time(ctx, 0.2);
        frame(ctx, "Tooltips", mid(hot), LEFT, false) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            hot = ui::widget_bounds(ctx);
            ui::label(ctx, "hover me", ui::TEXT_LEFT);
            ui::do_tooltip_delay(ctx, "after a while", hot, timer);
        };
        print("delayed tooltip at ", step, ": ", drew(ctx, "after a while"));
    }
    // The program says when its widget was clicked: no tooltip then until the
    // mouse moves, which starts the wait again.
    timer = 0.0;
    for step in 7 {
        ui::set_delta_time(ctx, 0.2);
        if step == 3 { clicked = true; }
        let at = if step >= 4 { float2 { mid(hot).x + 1.0, mid(hot).y } } else { mid(hot) };
        frame(ctx, "Tooltips", at, LEFT, false) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            hot = ui::widget_bounds(ctx);
            ui::label(ctx, "hover me", ui::TEXT_LEFT);
            ui::do_tooltip_delay_clicked(ctx, "not after a click", hot, timer, clicked);
        };
        print("clicked-delay tooltip at ", step, ": ", drew(ctx, "not after a click"),
              ", clicked ", clicked);
    }
    ui::set_delta_time(ctx, 0.0);

    // --- a menu bar: each menu opened, its last item clicked ---
    var menus: ui::Rect[5] = [ui::rect(0, 0, 0, 0); 5];
    var items_hit: i64[5] = [-1; 5];
    for step in 21 {
        let which = (step - 1) / 4;
        let phase = (step - 1) % 4;
        let at = if step == 0 { far } else if phase < 2 { mid(menus[which]) } else { target };
        frame(ctx, "Menus", at, LEFT, step > 0 && phase % 2 == 0) {
            ui::menubar_begin(ctx);
            ui::layout_row_static(ctx, 25.0, 80, 5);
            for k in 5 {
                menus[k] = ui::widget_bounds(ctx);
                let size = float2 { 160.0, 120.0 };
                let open = match k {
                    0 => ui::menu_begin_label(ctx, "File", ui::TEXT_LEFT, size),
                    1 => ui::menu_begin_image(ctx, "image-menu", ui::image_id(9), size),
                    2 => ui::menu_begin_image_label(ctx, "Image", ui::TEXT_LEFT, ui::image_id(9),
                                                    size),
                    3 => ui::menu_begin_symbol(ctx, "symbol-menu", ui::SYMBOL_HAMBURGER, size),
                    _ => ui::menu_begin_symbol_label(ctx, "More", ui::TEXT_LEFT,
                                                     ui::SYMBOL_CHEVRON_DOWN, size),
                };
                if open {
                    ui::layout_row_dynamic(ctx, 20.0, 1);
                    if ui::menu_item_label(ctx, "Open", ui::TEXT_LEFT) { items_hit[k] = 0; }
                    if ui::menu_item_image_label(ctx, ui::image_id(9), "Save", ui::TEXT_LEFT) {
                        items_hit[k] = 1;
                    }
                    if ui::menu_item_symbol_label(ctx, ui::SYMBOL_X, "Quit", ui::TEXT_LEFT) {
                        items_hit[k] = 2;
                    }
                    ui::menu_end(ctx);
                }
            }
            ui::menubar_end(ctx);
            ui::layout_row_dynamic(ctx, 25.0, 1);
            ui::label(ctx, "under the menu bar", ui::TEXT_LEFT);
        };
        if phase == 1 { target = text_at(ctx, "Quit"); }
    }
    print("menus' items clicked: ", items_hit[0], items_hit[1], items_hit[2], items_hit[3],
          items_hit[4]);
    // menu_close shuts one from inside.
    for step in 4 {
        frame(ctx, "Menus", if step == 1 || step == 2 { mid(menus[0]) } else { far }, LEFT,
              step == 1) {
            ui::menubar_begin(ctx);
            ui::layout_row_static(ctx, 25.0, 80, 1);
            menus[0] = ui::widget_bounds(ctx);
            if ui::menu_begin_label(ctx, "Closes", ui::TEXT_LEFT, float2 { 120.0, 80.0 }) {
                print("step ", step, ": menu open, closing it");
                ui::menu_close(ctx);
                ui::menu_end(ctx);
            }
            ui::menubar_end(ctx);
        };
    }

    input(ctx, far.x, far.y, LEFT, false);
    ui::destroy(ctx);
    print("misuses ", ui::misuse_count());
}
