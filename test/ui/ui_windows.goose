// Windows, layout, groups, trees and list views in the ui module, driven
// by injected input: a window's geometry and state read back and changed by
// name (moved, resized, focused, scrolled, collapsed, hidden, closed), every
// kind of row layout with where its widgets land, groups with their own
// scroll offsets, tree nodes and tabs opened and closed by clicks, a list
// view of a thousand rows building only the ones in view, and the queries
// about the next widget's place.
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
fn r(b: ui::Rect) -> u8[>..] { str(n(b.x), ",", n(b.y), " ", n(b.w), "x", n(b.h)) }
fn v(p: float2) -> u8[>..] { str(n(p.x), ",", n(p.y)) }

// A frame's input: the mouse at x, y, the left button as given.
fn input(ctx: ui::Context, x: i64, y: i64, down: bool) {
    ui::input_begin(ctx);
    ui::input_motion(ctx, x, y);
    ui::input_button(ctx, ui::BUTTON_LEFT, x, y, down);
    ui::input_end(ctx);
}

let FLAGS: i32 = 95;    // BORDER | MOVABLE | SCALABLE | CLOSABLE | MINIMIZABLE | TITLE

// Alpha, with more lines than it shows, so it scrolls: whether it opened.
fn alpha(ctx: ui::Context) -> bool {
    let open = ui::begin(ctx, "Alpha", ui::rect(10, 20, 240, 300), FLAGS);
    if open {
        ui::layout_row_dynamic(ctx, 20.0, 1);
        for i in 30 { ui::label(ctx, str("line ", i), ui::TEXT_LEFT); }
    }
    ui::end(ctx);
    open
}

// Beta, begun every frame: a window not begun in a frame is gone after it.
fn beta(ctx: ui::Context) -> bool {
    let open = ui::begin_titled(ctx, "Beta", "The second window", ui::rect(300, 20, 200, 150),
                                ui::WINDOW_TITLE | ui::WINDOW_MOVABLE);
    ui::end(ctx);
    open
}

// Where the frame drew `label`, from its command list.
fn text_at(ctx: ui::Context, label: const u8[:]) -> int2 {
    let text = ui::command_text(ctx);
    for c in ui::commands(ctx) {
        if c.kind == ui::COMMAND_TEXT &&
           text[(c.first as i64)..((c.first + c.count) as i64)] == label {
            return int2 { c.x, c.y };
        }
    }
    return int2 { -1, -1 };
}

fn main() {
    let ctx = ui::create(13.0);

    // --- a window's geometry and state ---
    input(ctx, 60, 80, false);
    print("begin ", ui::begin(ctx, "Alpha", ui::rect(10, 20, 240, 300), FLAGS));
    print("bounds ", r(ui::window_get_bounds(ctx)), "; position ", v(ui::window_get_position(ctx)),
          "; size ", v(ui::window_get_size(ctx)), "; ", n(ui::window_get_width(ctx)), " x ",
          n(ui::window_get_height(ctx)));
    print("content ", r(ui::window_get_content_region(ctx)), ", from ",
          v(ui::window_get_content_region_min(ctx)), " to ",
          v(ui::window_get_content_region_max(ctx)), ", size ",
          v(ui::window_get_content_region_size(ctx)));
    print("focus ", ui::window_has_focus(ctx), "; hovered ", ui::window_is_hovered(ctx),
          "; scroll ", ui::window_get_scroll(ctx).y);
    ui::layout_row_dynamic(ctx, 20.0, 1);
    for i in 30 { ui::label(ctx, str("line ", i), ui::TEXT_LEFT); }
    ui::end(ctx);
    print("titled begin ", beta(ctx));
    print("find Alpha ", ui::window_find(ctx, "Alpha"), ", Gamma ", ui::window_find(ctx, "Gamma"));
    print("Beta active ", ui::window_is_active(ctx, "Beta"), ", collapsed ",
          ui::window_is_collapsed(ctx, "Beta"), ", hidden ", ui::window_is_hidden(ctx, "Beta"),
          ", closed ", ui::window_is_closed(ctx, "Beta"), "; Gamma closed ",
          ui::window_is_closed(ctx, "Gamma"), ", hidden ", ui::window_is_hidden(ctx, "Gamma"));
    print("any hovered ", ui::window_is_any_hovered(ctx), ", item active ",
          ui::item_is_any_active(ctx));

    // Changed by name, and seen the next frame.
    ui::window_set_bounds(ctx, "Beta", ui::rect(320, 40, 180, 120));
    ui::window_set_focus(ctx, "Alpha");
    input(ctx, 700, 500, false);
    ui::begin(ctx, "Alpha", ui::rect(10, 20, 240, 300), FLAGS);
    ui::window_set_scroll(ctx, 0, 40);
    ui::layout_row_dynamic(ctx, 20.0, 1);
    for i in 30 { ui::label(ctx, str("line ", i), ui::TEXT_LEFT); }
    ui::end(ctx);
    ui::begin_titled(ctx, "Beta", "The second window", ui::rect(300, 20, 200, 150),
                     ui::WINDOW_TITLE | ui::WINDOW_MOVABLE);
    print("Beta set: ", r(ui::window_get_bounds(ctx)));
    ui::end(ctx);
    print("Alpha active ", ui::window_is_active(ctx, "Alpha"), "; any hovered ",
          ui::window_is_any_hovered(ctx));
    ui::window_set_position(ctx, "Beta", float2 { 330.0, 50.0 });
    ui::window_set_size(ctx, "Beta", float2 { 150.0, 100.0 });
    input(ctx, 700, 500, false);
    ui::begin(ctx, "Alpha", ui::rect(10, 20, 240, 300), FLAGS);
    print("Alpha scrolled to ", ui::window_get_scroll(ctx).y);
    ui::layout_row_dynamic(ctx, 20.0, 1);
    for i in 30 { ui::label(ctx, str("line ", i), ui::TEXT_LEFT); }
    ui::end(ctx);
    ui::begin_titled(ctx, "Beta", "The second window", ui::rect(300, 20, 200, 150),
                     ui::WINDOW_TITLE | ui::WINDOW_MOVABLE);
    print("Beta moved and sized: ", r(ui::window_get_bounds(ctx)));
    ui::end(ctx);

    // Collapsed: begin says there is nothing to fill; expanded again.
    ui::window_collapse(ctx, "Alpha", ui::MINIMIZED);
    input(ctx, 700, 500, false);
    print("collapsed: begin ", alpha(ctx), ", is collapsed ", ui::window_is_collapsed(ctx, "Alpha"));
    beta(ctx);
    ui::window_collapse_if(ctx, "Alpha", ui::MAXIMIZED, false);
    print("collapse_if false leaves it: ", ui::window_is_collapsed(ctx, "Alpha"));
    ui::window_collapse_if(ctx, "Alpha", ui::MAXIMIZED, true);
    input(ctx, 700, 500, false);
    print("expanded: begin ", alpha(ctx));
    beta(ctx);

    // Hidden and shown, then closed for good.
    ui::window_show(ctx, "Beta", ui::HIDDEN);
    input(ctx, 700, 500, false);
    alpha(ctx);
    print("hidden: begin ", beta(ctx), ", is hidden ", ui::window_is_hidden(ctx, "Beta"));
    ui::window_show_if(ctx, "Beta", ui::SHOWN, false);
    print("show_if false leaves it: ", ui::window_is_hidden(ctx, "Beta"));
    ui::window_show_if(ctx, "Beta", ui::SHOWN, true);
    input(ctx, 700, 500, false);
    alpha(ctx);
    print("shown: begin ", beta(ctx), ", is hidden ", ui::window_is_hidden(ctx, "Beta"));
    ui::window_close(ctx, "Beta");
    print("closed ", ui::window_is_closed(ctx, "Beta"));
    input(ctx, 700, 500, false);
    print("after closing, found ", ui::window_find(ctx, "Beta"));

    // --- rows: where widgets land ---
    input(ctx, 700, 500, false);
    ui::begin(ctx, "Rows", ui::rect(100, 0, 400, 600), ui::WINDOW_NO_SCROLLBAR);
    ui::layout_row_dynamic(ctx, 20.0, 3);
    print("dynamic: next ", r(ui::layout_widget_bounds(ctx)), "; 50 px is ",
          n(ui::layout_ratio_from_pixel(ctx, 50.0)), " (by the window's x, as Nuklear has it)");
    for i in 3 { let s, b = ui::widget(ctx); print("  widget ", s, " at ", r(b)); }
    ui::layout_row_static(ctx, 25.0, 80, 2);
    for i in 2 { let s, b = ui::widget(ctx); print("static: widget at ", r(b)); }
    ui::layout_row_begin(ctx, ui::DYNAMIC, 30.0, 2);
    ui::layout_row_push(ctx, 0.25);
    let s1, b1 = ui::widget(ctx);
    ui::layout_row_push(ctx, 0.75);
    let s2, b2 = ui::widget(ctx);
    ui::layout_row_end(ctx);
    print("row of ratios: ", r(b1), "; ", r(b2));
    ui::layout_row_begin(ctx, ui::STATIC, 30.0, 2);
    ui::layout_row_push(ctx, 50.0);
    let s3, b3 = ui::widget(ctx);
    ui::layout_row_push(ctx, 120.0);
    let s4, b4 = ui::widget(ctx);
    ui::layout_row_end(ctx);
    print("row of widths: ", r(b3), "; ", r(b4));
    let ratios: f32[3] = [0.2, 0.5, 0.3];
    ui::layout_row(ctx, ui::DYNAMIC, 22.0, ratios);
    for i in 3 { let s, b = ui::widget(ctx); print("layout_row: ", r(b)); }
    ui::layout_row_template_begin(ctx, 24.0);
    ui::layout_row_template_push_static(ctx, 60.0);
    ui::layout_row_template_push_variable(ctx, 40.0);
    ui::layout_row_template_push_dynamic(ctx);
    ui::layout_row_template_end(ctx);
    for i in 3 { let s, b = ui::widget(ctx); print("template: ", r(b)); }
    ui::layout_set_min_row_height(ctx, 40.0);
    ui::layout_row_dynamic(ctx, 10.0, 1);
    print("minimum height 40: ", r(ui::widget_bounds(ctx)));
    ui::layout_reset_min_row_height(ctx);
    ui::layout_row_dynamic(ctx, 10.0, 4);
    ui::spacer(ctx);
    ui::spacing(ctx, 2);
    print("after a spacer and 2 spaces: ", r(ui::widget_bounds(ctx)));
    ui::rule_horizontal(ctx, ui::rgb(200, 0, 0), true);
    ui::layout_space_begin(ctx, ui::STATIC, 100.0, 2);
    print("space bounds ", r(ui::layout_space_bounds(ctx)));
    ui::layout_space_push(ctx, ui::rect(10, 10, 50, 20));
    let s5, b5 = ui::widget(ctx);
    ui::layout_space_push(ctx, ui::rect(100, 50, 60, 30));
    let s6, b6 = ui::widget(ctx);
    print("in the space: ", r(b5), "; ", r(b6));
    let screen = ui::layout_space_to_screen(ctx, float2 { 10.0, 10.0 });
    print("local 10,10 is ", v(screen), " on screen, and back ",
          v(ui::layout_space_to_local(ctx, screen)));
    let rs = ui::layout_space_rect_to_screen(ctx, ui::rect(0, 0, 5, 5));
    print("rect to screen ", r(rs), ", back ", r(ui::layout_space_rect_to_local(ctx, rs)));
    ui::layout_space_end(ctx);
    ui::layout_space_begin(ctx, ui::DYNAMIC, 50.0, 1);
    ui::layout_space_push(ctx, ui::rect(0.5, 0.0, 0.5, 1.0));
    let s7, b7 = ui::widget(ctx);
    print("dynamic space, right half: ", r(b7));
    ui::layout_space_end(ctx);
    ui::end(ctx);

    // --- the next widget, and disabled ones ---
    input(ctx, 30, 30, false);
    ui::begin(ctx, "Widgets", ui::rect(0, 0, 300, 200), ui::WINDOW_NO_SCROLLBAR);
    ui::layout_row_dynamic(ctx, 30.0, 2);
    print("next: ", r(ui::widget_bounds(ctx)), " at ", v(ui::widget_position(ctx)), " size ",
          v(ui::widget_size(ctx)), " = ", n(ui::widget_width(ctx)), " x ",
          n(ui::widget_height(ctx)), "; hovered ", ui::widget_is_hovered(ctx), "; clicked ",
          ui::widget_is_mouse_clicked(ctx, ui::BUTTON_LEFT), "; down ",
          ui::widget_has_mouse_click_down(ctx, ui::BUTTON_LEFT, true));
    let hs, hb = ui::widget(ctx);
    print("taken: state ", hs, " (valid ", hs == ui::WIDGET_VALID, ")");
    ui::widget_disable_begin(ctx);
    let ds, db = ui::widget(ctx);
    print("disabled: state ", ds == ui::WIDGET_DISABLED, "; button ",
          ui::button_label(ctx, "off"));
    ui::widget_disable_end(ctx);
    ui::layout_row_dynamic(ctx, 30.0, 1);
    for i in 5 { ui::widget(ctx); }
    let fs, fb = ui::widget(ctx);
    print("below the window: state ", fs == ui::WIDGET_INVALID, " at ", r(fb));
    ui::end(ctx);
    // Pressed on the next frame's first widget, and released.
    input(ctx, 30, 30, true);
    ui::begin(ctx, "Widgets", ui::rect(0, 0, 300, 200), ui::WINDOW_NO_SCROLLBAR);
    ui::layout_row_dynamic(ctx, 30.0, 2);
    print("pressed: clicked ", ui::widget_is_mouse_clicked(ctx, ui::BUTTON_LEFT), "; down ",
          ui::widget_has_mouse_click_down(ctx, ui::BUTTON_LEFT, true));
    ui::end(ctx);
    input(ctx, 30, 30, false);
    ui::begin(ctx, "Widgets", ui::rect(0, 0, 300, 200), ui::WINDOW_NO_SCROLLBAR);
    ui::layout_row_dynamic(ctx, 30.0, 2);
    print("released: clicked ", ui::widget_is_mouse_clicked(ctx, ui::BUTTON_LEFT), "; down ",
          ui::widget_has_mouse_click_down(ctx, ui::BUTTON_LEFT, true));
    ui::end(ctx);

    // --- groups ---
    var offset = ui::Scroll { 0, 30 };
    for frame in 2 {
        input(ctx, 700, 500, false);
        ui::begin(ctx, "Groups", ui::rect(0, 0, 400, 400), 0);
        ui::layout_row_dynamic(ctx, 120.0, 3);
        if ui::group_begin(ctx, "First", ui::WINDOW_BORDER | ui::WINDOW_TITLE) {
            ui::layout_row_dynamic(ctx, 20.0, 1);
            for i in 10 { ui::label(ctx, str("line ", i), ui::TEXT_LEFT); }
            if frame == 0 { print("group content ", r(ui::window_get_content_region(ctx))); }
            ui::group_end(ctx);
        }
        if ui::group_begin_titled(ctx, "second-id", "Second", ui::WINDOW_BORDER) {
            ui::layout_row_dynamic(ctx, 20.0, 1);
            for i in 10 { ui::label(ctx, "x", ui::TEXT_LEFT); }
            ui::group_end(ctx);
        }
        if ui::group_scrolled_begin(ctx, offset, "Scrolled", ui::WINDOW_BORDER) {
            ui::layout_row_dynamic(ctx, 20.0, 1);
            for i in 10 { ui::label(ctx, "y", ui::TEXT_LEFT); }
            offset = ui::group_scrolled_end(ctx);
        }
        if frame == 0 {
            ui::group_set_scroll(ctx, "First", 0, 25);
        } else {
            print("First scrolled to ", ui::group_get_scroll(ctx, "First").y);
        }
        print("scrolled group keeps ", offset.x, ",", offset.y, "; Second at ",
              ui::group_get_scroll(ctx, "second-id").y);
        ui::end(ctx);
    }

    // --- trees: a click on the tab's header closes it, one on the element's
    // title selects it, and the kept node follows the program's state ---
    var state = ui::MAXIMIZED as i64;
    var selected = false;
    var click_at = int2 { 60, 17 };
    for frame in 5 {
        input(ctx, click_at.x as i64, click_at.y as i64, frame == 1 || frame == 3);
        ui::begin(ctx, "Trees", ui::rect(0, 0, 300, 500), ui::WINDOW_NO_SCROLLBAR);
        let tab_open = ui::tree_push(ctx, ui::TREE_TAB, "Tab", ui::MAXIMIZED);
        if tab_open {
            let node_open = ui::tree_push_id(ctx, ui::TREE_NODE, "Node", ui::MINIMIZED, 7);
            if node_open { ui::tree_pop(ctx); }
            let img_open = ui::tree_image_push(ctx, ui::TREE_NODE, ui::image_id(3), "Image node",
                                               ui::MAXIMIZED);
            if img_open {
                ui::layout_row_dynamic(ctx, 18.0, 1);
                ui::label(ctx, "inside", ui::TEXT_LEFT);
                ui::tree_pop(ctx);
            }
            ui::tree_pop(ctx);
        }
        let state_open = ui::tree_state_push(ctx, ui::TREE_NODE, "Kept", state);
        if state_open { ui::tree_state_pop(ctx); }
        let simg_open = ui::tree_state_image_push(ctx, ui::TREE_TAB, ui::image_id(3), "Kept image",
                                                  state);
        if simg_open { ui::tree_state_pop(ctx); }
        let el_open = ui::tree_element_push(ctx, ui::TREE_NODE, "Element", ui::MAXIMIZED, selected);
        if el_open { ui::tree_element_pop(ctx); }
        let eli_open = ui::tree_element_image_push_hashed(ctx, ui::TREE_NODE, ui::image_id(3),
                                                          "Image element", ui::MINIMIZED, selected,
                                                          "unique", 1);
        if eli_open { ui::tree_element_pop(ctx); }
        print("frame ", frame, ": tab ", tab_open, ", kept ", state_open, " (state ", state,
              "), element ", el_open, " selected ", selected, ", image element ", eli_open);
        ui::end(ctx);
        if frame == 2 { click_at = text_at(ctx, "Element"); }
    }
    state = ui::MINIMIZED;
    input(ctx, 700, 500, false);
    ui::begin(ctx, "Trees", ui::rect(0, 0, 300, 500), ui::WINDOW_NO_SCROLLBAR);
    let closed_by_state = ui::tree_state_push(ctx, ui::TREE_NODE, "Kept", state);
    if closed_by_state { ui::tree_state_pop(ctx); }
    print("state set to minimized: open ", closed_by_state);
    ui::end(ctx);

    // Nodes of the same title, told apart by their hash strings, open and
    // close on their own; their default is where they start.
    for frame in 2 {
        input(ctx, 700, 500, false);
        ui::begin(ctx, "Same titles", ui::rect(0, 0, 300, 300), 0);
        let a = ui::tree_push_hashed(ctx, ui::TREE_NODE, "Same", ui::MAXIMIZED, "first", 0);
        if a { ui::tree_pop(ctx); }
        let b = ui::tree_push_hashed(ctx, ui::TREE_NODE, "Same", ui::MINIMIZED, "second", 0);
        if b { ui::tree_pop(ctx); }
        let i = ui::tree_image_push_hashed(ctx, ui::TREE_TAB, ui::image_id(3), "Same",
                                           ui::MAXIMIZED, "third", 0);
        if i { ui::tree_pop(ctx); }
        var picked = true;
        let e = ui::tree_element_push_hashed(ctx, ui::TREE_NODE, "Same", ui::MINIMIZED, picked,
                                             "fourth", 0);
        if e { ui::tree_element_pop(ctx); }
        print("same titles, frame ", frame, ": ", a, " ", b, " ", i, " ", e);
        ui::end(ctx);
    }

    // --- a list view of 1000 rows ---
    input(ctx, 700, 500, false);
    ui::begin(ctx, "List", ui::rect(0, 0, 200, 300), 0);
    ui::layout_row_dynamic(ctx, 200.0, 1);
    let view, open = ui::list_view_begin(ctx, "rows", ui::WINDOW_BORDER, 20, 1000);
    if open {
        ui::layout_row_dynamic(ctx, 20.0, 1);
        for i in view.begin..view.end { ui::label(ctx, str("row ", i), ui::TEXT_LEFT); }
        ui::list_view_end(ctx);
    }
    print("list view open ", open, ": rows ", view.begin, " to ", view.end, ", ", view.count,
          " built of 1000");
    ui::group_set_scroll(ctx, "rows", 0, 480);
    ui::end(ctx);
    input(ctx, 700, 500, false);
    ui::begin(ctx, "List", ui::rect(0, 0, 200, 300), 0);
    ui::layout_row_dynamic(ctx, 200.0, 1);
    let later, later_open = ui::list_view_begin(ctx, "rows", ui::WINDOW_BORDER, 20, 1000);
    if later_open { ui::list_view_end(ctx); }
    print("scrolled 480 pixels: rows ", later.begin, " to ", later.end);
    ui::end(ctx);
    input(ctx, 700, 500, false);
    ui::destroy(ctx);
    print("misuses ", ui::misuse_count());
}
