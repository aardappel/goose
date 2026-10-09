// The ngfx module's input events, headless: injected keys, text and mouse
// input come back from events() in the order they came, keys with their
// scancode, keycode and the modifiers held; scancodes and key names map
// both ways, and the names SDL gives keys after a platform's labels work on
// every platform; the mouse is in the window while injected on the screen;
// the clipboard, ngfx's own without a window, holds text; and open_count
// tells a program ngfx was closed and opened again.
import std;
import vec;
import os;
import ngfx;

fn open_or_skip() -> bool {
    if ngfx::open_headless(32, 24) { return true; }
    write_stderr(str("ngfx: ", ngfx::error(), "\n"));
    return false;
}

// A key by the name it was injected by: key_name gives the platform's
// own, "Left Option" on a Mac for "Left Alt".
fn name_of(scancode: i64) -> u8[>..] {
    let injected: const u8[:][6] = ["Left Shift", "A", "Left Ctrl", "Left Alt", "Delete", "Return"];
    for n in injected {
        if ngfx::scancode(n) == scancode { return str(n); }
    }
    ngfx::key_name(scancode)
}

fn describe(e: ngfx::Event) -> u8[>..] {
    if e.kind == ngfx::EVENT_KEY {
        var mods: u8[>..] = [];
        if (e.mods & ngfx::MOD_SHIFT) != 0 { mods.append(" shift"); }
        if (e.mods & ngfx::MOD_CTRL) != 0 { mods.append(" ctrl"); }
        if (e.mods & ngfx::MOD_ALT) != 0 { mods.append(" alt"); }
        if (e.mods & ngfx::MOD_GUI) != 0 { mods.append(" gui"); }
        return str("key ", name_of(e.scancode), if e.down { " down" } else { " up" },
                   ", keycode ", e.keycode, if mods.len > 0 { str(", with", mods) } else { str("") },
                   if e.repeat { ", repeated" } else { "" });
    }
    if e.kind == ngfx::EVENT_TEXT { return str("text ", e.codepoint); }
    if e.kind == ngfx::EVENT_MOUSE_MOTION { return str("motion to ", e.x, ",", e.y); }
    if e.kind == ngfx::EVENT_MOUSE_BUTTON {
        return str("button ", e.button, if e.down { " down" } else { " up" }, " at ", e.x, ",", e.y,
                   ", clicks ", e.clicks);
    }
    str("event ", e.kind)
}

fn frame_events(what: const u8[:]) {
    ngfx::frame();
    let events = ngfx::events();
    print(what, ": ", events.len, " events");
    for e in events { print("  ", describe(e)); }
}

fn main() {
    guard open_or_skip() else { return; }
    let opened = ngfx::open_count();
    print("opened at least once: ", opened >= 1);
    ngfx::text_input(true);
    ngfx::frame();
    frame_events("nothing");
    print("mouse in the window ", ngfx::mouse_in_window());

    ngfx::inject_key("Left Shift", true);
    ngfx::inject_key("A", true);
    ngfx::inject_key("A", false);
    ngfx::inject_key("Left Shift", false);
    ngfx::inject_key("Left Ctrl", true);
    ngfx::inject_key("Left Alt", true);
    ngfx::inject_key("Delete", true);
    ngfx::inject_key("Left Alt", false);
    ngfx::inject_key("Left Ctrl", false);
    ngfx::inject_key("Delete", false);
    frame_events("keys");

    ngfx::inject_text("hi\xc3\xa9");
    ngfx::inject_key("Return", true);
    ngfx::inject_text("!");
    ngfx::inject_key("Return", false);
    frame_events("text among keys");

    ngfx::inject_mouse(5.0, 6.0, 0, false);
    ngfx::inject_mouse(7.0, 8.0, ngfx::MOUSE_LEFT, true);
    ngfx::inject_mouse(7.0, 8.0, ngfx::MOUSE_LEFT, false);
    ngfx::inject_mouse(9.0, 10.0, ngfx::MOUSE_X2, true);
    frame_events("the mouse");
    print("X2 down ", ngfx::mouse_down(ngfx::MOUSE_X2), ", X1 down ", ngfx::mouse_down(ngfx::MOUSE_X1));
    ngfx::inject_mouse(9.0, 10.0, ngfx::MOUSE_X2, false);
    frame_events("released");
    print("mouse in the window ", ngfx::mouse_in_window());
    // Off the screen's left and bottom edges, then on its last pixel.
    ngfx::inject_mouse(-1.0, 10.0, 0, false);
    ngfx::frame();
    print("off the left: in the window ", ngfx::mouse_in_window(), " at ", ngfx::mouse_pos());
    ngfx::inject_mouse(4.0, 24.0, 0, false);
    ngfx::frame();
    print("off the bottom: in the window ", ngfx::mouse_in_window());
    ngfx::inject_mouse(31.5, 23.5, 0, false);
    ngfx::frame();
    print("on the last pixel: in the window ", ngfx::mouse_in_window());

    let names: const u8[:][5] = ["A", "Return", "Left Shift", "F1", "Keypad Enter"];
    for n in names {
        let sc = ngfx::scancode(n);
        print("scancode of ", n, " ", sc > 0, ", named back ", ngfx::key_name(sc));
    }
    print("no such key: ", ngfx::scancode("No Such Key"));
    print("one key by each platform's name: Left Alt ",
          ngfx::scancode("Left Option") == ngfx::scancode("Left Alt"), ", Left GUI ",
          ngfx::scancode("Left Command") == ngfx::scancode("Left GUI") &&
          ngfx::scancode("Left Windows") == ngfx::scancode("Left GUI"),
          ", Right GUI ", ngfx::scancode("right command") == ngfx::scancode("Right Windows"),
          ", Application ", ngfx::scancode("Application") > 0);

    print("clipboard at first '", ngfx::clipboard(), "'");
    print("set ", ngfx::set_clipboard("copied text"), ": '", ngfx::clipboard(), "'");
    print("set empty ", ngfx::set_clipboard(""), ": '", ngfx::clipboard(), "'");
    ngfx::text_input(false);

    ngfx::close();
    guard open_or_skip() else { return; }
    print("opened again: count up by one ", ngfx::open_count() == opened + 1,
          ", clipboard '", ngfx::clipboard(), "'");
    ngfx::close();
}
