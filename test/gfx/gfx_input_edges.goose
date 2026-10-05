// Input edges survive a whole tap between frames. Held state records the
// final event, edges record every transition, and queries do not consume them.
import std;
import os;
import gfx;

fn inject(down: bool) {
    gfx::inject_key("C", down);
    gfx::inject_mouse(16.0, 16.0, gfx::MOUSE_RIGHT, down);
}

fn report(label: const u8[:]) {
    print(label, ": key ", gfx::key_down("C"), " ", gfx::key_pressed("C"), " ",
          gfx::key_released("C"), "; mouse ", gfx::mouse_down(gfx::MOUSE_RIGHT), " ",
          gfx::mouse_pressed(gfx::MOUSE_RIGHT), " ", gfx::mouse_released(gfx::MOUSE_RIGHT));
}

fn main() {
    guard gfx::open_headless(32, 24) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    gfx::frame();
    report("initial");

    // Neither a frame's final held state nor a previous-state comparison can
    // represent this tap. Both edges must be visible, with the key/button up.
    inject(true);
    inject(false);
    report("queued, before frame");
    gfx::frame();
    report("tap");
    report("queried again");
    let events = gfx::events();
    for e in events {
        if e.kind == gfx::EVENT_KEY { print("key event down ", e.down); }
        if e.kind == gfx::EVENT_MOUSE_BUTTON { print("mouse event down ", e.down); }
    }
    gfx::frame();
    report("tap cleared");

    // An ordinary hold has one press, then no edges until it is released.
    inject(true);
    gfx::frame();
    report("press");
    gfx::frame();
    report("hold");
    inject(true);
    inject(true);
    gfx::frame();
    report("repeated down");
    inject(false);
    gfx::frame();
    report("release");
    gfx::frame();
    report("release cleared");
    inject(false);
    inject(false);
    gfx::frame();
    report("repeated up");

    // The reverse order also has both edges, but ends held.
    inject(true);
    gfx::frame();
    inject(false);
    inject(true);
    gfx::frame();
    report("release then press");
    gfx::frame();
    report("held after both edges");

    // Repeated events must not erase edges recorded earlier in the frame.
    inject(false);
    inject(false);
    inject(true);
    inject(true);
    inject(false);
    gfx::frame();
    report("several transitions");
    gfx::frame();
    report("several transitions cleared");
    print("other inputs untouched: ",
          !gfx::key_down("A") && !gfx::key_pressed("A") && !gfx::key_released("A") &&
          !gfx::mouse_down(gfx::MOUSE_LEFT) && !gfx::mouse_pressed(gfx::MOUSE_LEFT) &&
          !gfx::mouse_released(gfx::MOUSE_LEFT));

    // Closing clears edge and held state too, before the next first frame.
    inject(true);
    gfx::frame();
    gfx::close();
    guard gfx::open_headless(32, 24) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    report("reopened");
    gfx::frame();
    report("reopened first frame");
    gfx::close();
}
