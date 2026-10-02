// Whether input is the ui's, through headless gfx and injected input.
// ui::wants_keyboard is true while a text field, or a property being typed
// into, has the keyboard in a window that takes input: from the click that
// focuses it, wherever the mouse goes, until a click outside it,
// edit_unfocus, or its window closing, minimizing or being left out of a
// frame. A window keeps its field focused while it is behind another,
// under a popup of its own or hidden, and the field takes the keys again
// once the window is in front, the popup closed or the window shown. It is
// false while a property is dragged, after edit_focus with no text field
// to focus, and while the mouse is only over a window, which
// item_is_any_active answers true to. ui::wants_mouse is true over a window
// or a popup, and while a button that went down over one is held off it,
// but not while one that went down outside is held over one, nor over a
// window left out of the frame. A window begun with WINDOW_NO_INPUT lets
// the mouse through to the program, and its widgets take no clicks, until
// it is begun without the flag. Typed text is printed where it lands.
import std;
import vec;
import os;
import gfx;
import ui;

let W = 320;
let H = 300;

var name: u8[..16] = [];
var note: u8[..16] = [];
var rename: u8[..16] = [];
var count: i64 = 5;
var name_at = ui::Rect { 0.0, 0.0, 0.0, 0.0 };
var note_at = ui::Rect { 0.0, 0.0, 0.0, 0.0 };
var rename_at = ui::Rect { 0.0, 0.0, 0.0, 0.0 };
var count_at = ui::Rect { 0.0, 0.0, 0.0, 0.0 };
var rename_open = false;
var close_rename = false;
var unfocus = false;
var focus_nothing = false;
var show_right = true;
var show_info = false;
var info_flags = ui::WINDOW_BORDER | ui::WINDOW_NO_INPUT;
var info_at = ui::Rect { 0.0, 0.0, 0.0, 0.0 };
var info_clicks = 0;

// A frame: gfx's input into the context, then the windows.
fn frame(ctx: ui::Context) {
    gfx::frame();
    ui::input_from_gfx(ctx);
    if ui::begin(ctx, "Left", ui::rect(10, 10, 140, 150), ui::WINDOW_BORDER | ui::WINDOW_TITLE) {
        ui::layout_row_dynamic(ctx, 25.0, 1);
        name_at = ui::widget_bounds(ctx);
        ui::edit_string(ctx, ui::EDIT_FIELD, name, ui::FILTER_DEFAULT);
        if unfocus {
            ui::edit_unfocus(ctx);
            unfocus = false;
        }
        if rename_open {
            if ui::popup_begin(ctx, ui::POPUP_STATIC, "Rename", 0, ui::rect(5, 125, 120, 40)) {
                ui::layout_row_dynamic(ctx, 25.0, 1);
                rename_at = ui::widget_bounds(ctx);
                ui::edit_string(ctx, ui::EDIT_FIELD, rename, ui::FILTER_DEFAULT);
                if close_rename {
                    ui::popup_close(ctx);
                    rename_open = false;
                    close_rename = false;
                }
                ui::popup_end(ctx);
            } else {
                rename_open = false;
            }
        }
    }
    ui::end(ctx);
    if show_right {
        if ui::begin(ctx, "Right", ui::rect(170, 10, 140, 150),
                     ui::WINDOW_BORDER | ui::WINDOW_TITLE) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            note_at = ui::widget_bounds(ctx);
            ui::edit_string(ctx, ui::EDIT_FIELD, note, ui::FILTER_DEFAULT);
        }
        ui::end(ctx);
    }
    // The property has a window of its own: in one with a text field,
    // Nuklear gives the field the keys meant for the property too.
    if ui::begin(ctx, "Numbers", ui::rect(10, 215, 300, 40), ui::WINDOW_BORDER) {
        ui::layout_row_dynamic(ctx, 25.0, 1);
        count_at = ui::widget_bounds(ctx);
        ui::property_int(ctx, "Count", 0, count, 1000, 1, 1.0);
        if focus_nothing {
            ui::edit_focus(ctx, 0);
            focus_nothing = false;
        }
    }
    ui::end(ctx);
    if show_info {
        if ui::begin(ctx, "Info", ui::rect(170, 165, 140, 45), info_flags) {
            ui::layout_row_dynamic(ctx, 25.0, 1);
            info_at = ui::widget_bounds(ctx);
            if ui::button_label(ctx, "Info") { info_clicks++; }
        }
        ui::end(ctx);
    }
}

fn state(ctx: ui::Context, what: const u8[:]) {
    print(what, ": keyboard ", ui::wants_keyboard(ctx), ", mouse ", ui::wants_mouse(ctx));
}

fn texts() {
    print("  name '", name, "', note '", note, "', rename '", rename, "', count ", count);
}

fn centre(r: ui::Rect) -> float2 { float2 { r.x + r.w / 2.0, r.y + r.h / 2.0 } }

fn move_to(ctx: ui::Context, at: float2) {
    gfx::inject_mouse(at.x, at.y, 0, false);
    frame(ctx);
}

fn press(ctx: ui::Context, at: float2, down: bool) {
    gfx::inject_mouse(at.x, at.y, gfx::MOUSE_LEFT, down);
    frame(ctx);
}

fn click(ctx: ui::Context, at: float2) {
    press(ctx, at, true);
    press(ctx, at, false);
}

fn type_in(ctx: ui::Context, text: const u8[:]) {
    gfx::inject_text(text);
    frame(ctx);
}

fn tap(ctx: ui::Context, key: const u8[:]) {
    gfx::inject_key(key, true);
    gfx::inject_key(key, false);
    frame(ctx);
}

fn main() {
    guard gfx::open_headless(W, H) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    let ctx = ui::create(13.0);
    let outside = float2 { 160.0, 285.0 };
    let in_left = float2 { 80.0, 150.0 };
    frame(ctx);
    state(ctx, "first frame");

    move_to(ctx, in_left);
    state(ctx, "over a window");
    print("  window_is_any_hovered ", ui::window_is_any_hovered(ctx), ", item_is_any_active ",
          ui::item_is_any_active(ctx));

    // --- a text field ---
    click(ctx, centre(name_at));
    state(ctx, "text field clicked");
    move_to(ctx, outside);
    state(ctx, "mouse off the windows");
    type_in(ctx, "goose");
    state(ctx, "typed");
    texts();

    // --- a press outside takes the keyboard from the field, and the mouse
    // stays the program's over a window until it is released ---
    press(ctx, outside, true);
    state(ctx, "pressed outside");
    move_to(ctx, in_left);
    state(ctx, "held over a window");
    press(ctx, in_left, false);
    state(ctx, "released there");
    type_in(ctx, "!");
    texts();

    // --- a press on a window: the mouse stays the ui's off the windows ---
    press(ctx, in_left, true);
    state(ctx, "pressed on a window");
    move_to(ctx, outside);
    state(ctx, "held off it");
    press(ctx, outside, false);
    state(ctx, "released there");

    // --- a property: typed into, then dragged ---
    let value = float2 { count_at.x + count_at.w - 24.0, centre(count_at).y };
    click(ctx, value);
    state(ctx, "property clicked");
    gfx::inject_key("Backspace", true);
    gfx::inject_key("Backspace", false);
    type_in(ctx, "42");
    state(ctx, "typing into it");
    tap(ctx, "Return");
    state(ctx, "entered");
    texts();
    let label = float2 { count_at.x + 25.0, centre(count_at).y };
    press(ctx, label, true);
    state(ctx, "property pressed on its name");
    move_to(ctx, float2 { label.x + 10.0, label.y });
    state(ctx, "dragged");
    press(ctx, float2 { label.x + 10.0, label.y }, false);
    state(ctx, "released");
    texts();
    // The window of the property has no text field for edit_focus to give
    // the keyboard to.
    focus_nothing = true;
    frame(ctx);
    state(ctx, "edit_focus after the last text field");

    // --- another window in front ---
    click(ctx, centre(note_at));
    state(ctx, "the right window's field clicked");
    click(ctx, in_left);
    state(ctx, "the left window clicked");
    type_in(ctx, "x");
    state(ctx, "typed");
    texts();
    click(ctx, centre(note_at));
    type_in(ctx, "hi");
    state(ctx, "the field clicked again and typed into");
    texts();

    // --- a popup: the window under it takes no keys while it is open ---
    click(ctx, centre(name_at));
    state(ctx, "the left window's field clicked");
    rename_open = true;
    frame(ctx);
    state(ctx, "a popup opened over it");
    type_in(ctx, "?");
    // The popup reaches below its window.
    move_to(ctx, centre(rename_at));
    state(ctx, "mouse over the popup");
    print("  below the window ", centre(rename_at).y > 160.0);
    click(ctx, centre(rename_at));
    type_in(ctx, "new");
    state(ctx, "the popup's field clicked and typed into");
    texts();
    close_rename = true;
    frame(ctx);
    frame(ctx);
    state(ctx, "popup closed");
    type_in(ctx, "?");
    texts();

    // --- edit_unfocus ---
    unfocus = true;
    frame(ctx);
    state(ctx, "edit_unfocus");
    type_in(ctx, "*");
    texts();

    // --- a window left out of a frame, one closed and one hidden, each with
    // its field focused ---
    click(ctx, centre(note_at));
    state(ctx, "the right window's field clicked");
    show_right = false;
    frame(ctx);
    state(ctx, "the right window left out");
    show_right = true;
    frame(ctx);
    state(ctx, "and begun again");
    type_in(ctx, ".");
    texts();
    click(ctx, centre(note_at));
    state(ctx, "its field clicked");
    ui::window_close(ctx, "Right");
    state(ctx, "its window closed");
    show_right = false;
    frame(ctx);
    state(ctx, "a frame later");
    click(ctx, centre(name_at));
    state(ctx, "the left window's field clicked");
    ui::window_show(ctx, "Left", ui::HIDDEN);
    state(ctx, "its window hidden");
    type_in(ctx, "-");
    state(ctx, "typed");
    ui::window_show(ctx, "Left", ui::SHOWN);
    type_in(ctx, "+");
    state(ctx, "shown again and typed");
    texts();
    // Minimized, a window builds no field, and Nuklear drops the focus.
    ui::window_collapse(ctx, "Left", ui::MINIMIZED);
    state(ctx, "its window minimized");
    type_in(ctx, "=");
    ui::window_collapse(ctx, "Left", ui::MAXIMIZED);
    type_in(ctx, "=");
    state(ctx, "maximized and typed");
    texts();

    // --- a window taking no input, the frame's last ---
    show_info = true;
    frame(ctx);
    move_to(ctx, centre(info_at));
    state(ctx, "over a window taking no input");
    click(ctx, centre(info_at));
    state(ctx, "its button clicked");
    print("  info clicks ", info_clicks);
    press(ctx, centre(info_at), true);
    move_to(ctx, in_left);
    state(ctx, "pressed on it and held over a window");
    press(ctx, in_left, false);
    info_flags = ui::WINDOW_BORDER;
    click(ctx, centre(info_at));
    state(ctx, "begun taking input and clicked");
    print("  info clicks ", info_clicks);
    info_flags = ui::WINDOW_BORDER | ui::WINDOW_NO_INPUT;
    click(ctx, centre(info_at));
    state(ctx, "taking none again and clicked");
    print("  info clicks ", info_clicks);

    ui::destroy(ctx);
    gfx::close();
    print("misuses ", ui::misuse_count());
}
