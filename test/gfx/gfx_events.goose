// The gfx module's input events, headless: injected keys, text and mouse
// input come back from events() in the order they came, keys with their
// scancode, keycode and the modifiers held; scancodes and key names map
// both ways, and the names SDL gives keys after a platform's labels work on
// every platform; the clipboard, gfx's own without a window, holds text; and
// open_count tells a program gfx was closed and opened again.
import std;
import vec;
import os;
import gfx;

fn open_or_skip() -> bool {
    if gfx::open_headless(32, 24) { return true; }
    write_stderr(str("gfx: ", gfx::error(), "\n"));
    return false;
}

// A key by the name it was injected by: key_name gives the platform's
// own, "Left Option" on a Mac for "Left Alt".
fn name_of(scancode: i64) -> u8[>..] {
    let injected: const u8[:][6] = ["Left Shift", "A", "Left Ctrl", "Left Alt", "Delete", "Return"];
    for n in injected {
        if gfx::scancode(n) == scancode { return str(n); }
    }
    gfx::key_name(scancode)
}

fn describe(e: gfx::Event) -> u8[>..] {
    if e.kind == gfx::EVENT_KEY {
        var mods: u8[>..] = [];
        if (e.mods & gfx::MOD_SHIFT) != 0 { mods.append(" shift"); }
        if (e.mods & gfx::MOD_CTRL) != 0 { mods.append(" ctrl"); }
        if (e.mods & gfx::MOD_ALT) != 0 { mods.append(" alt"); }
        if (e.mods & gfx::MOD_GUI) != 0 { mods.append(" gui"); }
        return str("key ", name_of(e.scancode), if e.down { " down" } else { " up" },
                   ", keycode ", e.keycode, if mods.len > 0 { str(", with", mods) } else { str("") },
                   if e.repeat { ", repeated" } else { "" });
    }
    if e.kind == gfx::EVENT_TEXT { return str("text ", e.codepoint); }
    if e.kind == gfx::EVENT_MOUSE_MOTION { return str("motion to ", e.x, ",", e.y); }
    if e.kind == gfx::EVENT_MOUSE_BUTTON {
        return str("button ", e.button, if e.down { " down" } else { " up" }, " at ", e.x, ",", e.y,
                   ", clicks ", e.clicks);
    }
    str("event ", e.kind)
}

fn frame_events(what: const u8[:]) {
    gfx::frame();
    let events = gfx::events();
    print(what, ": ", events.len, " events");
    for e in events { print("  ", describe(e)); }
}

fn main() {
    guard open_or_skip() else { return; }
    let opened = gfx::open_count();
    print("opened at least once: ", opened >= 1);
    gfx::text_input(true);
    gfx::frame();
    frame_events("nothing");

    gfx::inject_key("Left Shift", true);
    gfx::inject_key("A", true);
    gfx::inject_key("A", false);
    gfx::inject_key("Left Shift", false);
    gfx::inject_key("Left Ctrl", true);
    gfx::inject_key("Left Alt", true);
    gfx::inject_key("Delete", true);
    gfx::inject_key("Left Alt", false);
    gfx::inject_key("Left Ctrl", false);
    gfx::inject_key("Delete", false);
    frame_events("keys");

    gfx::inject_text("hi\xc3\xa9");
    gfx::inject_key("Return", true);
    gfx::inject_text("!");
    gfx::inject_key("Return", false);
    frame_events("text among keys");

    gfx::inject_mouse(5.0, 6.0, 0, false);
    gfx::inject_mouse(7.0, 8.0, gfx::MOUSE_LEFT, true);
    gfx::inject_mouse(7.0, 8.0, gfx::MOUSE_LEFT, false);
    gfx::inject_mouse(9.0, 10.0, gfx::MOUSE_X2, true);
    frame_events("the mouse");
    print("X2 down ", gfx::mouse_down(gfx::MOUSE_X2), ", X1 down ", gfx::mouse_down(gfx::MOUSE_X1));
    gfx::inject_mouse(9.0, 10.0, gfx::MOUSE_X2, false);
    frame_events("released");

    let names: const u8[:][5] = ["A", "Return", "Left Shift", "F1", "Keypad Enter"];
    for n in names {
        let sc = gfx::scancode(n);
        print("scancode of ", n, " ", sc > 0, ", named back ", gfx::key_name(sc));
    }
    print("no such key: ", gfx::scancode("No Such Key"));
    print("one key by each platform's name: Left Alt ",
          gfx::scancode("Left Option") == gfx::scancode("Left Alt"), ", Left GUI ",
          gfx::scancode("Left Command") == gfx::scancode("Left GUI") &&
          gfx::scancode("Left Windows") == gfx::scancode("Left GUI"),
          ", Right GUI ", gfx::scancode("right command") == gfx::scancode("Right Windows"),
          ", Application ", gfx::scancode("Application") > 0);

    print("clipboard at first '", gfx::clipboard(), "'");
    print("set ", gfx::set_clipboard("copied text"), ": '", gfx::clipboard(), "'");
    print("set empty ", gfx::set_clipboard(""), ": '", gfx::clipboard(), "'");
    gfx::text_input(false);

    gfx::close();
    guard open_or_skip() else { return; }
    print("opened again: count up by one ", gfx::open_count() == opened + 1,
          ", clipboard '", gfx::clipboard(), "'");
    gfx::close();
}
