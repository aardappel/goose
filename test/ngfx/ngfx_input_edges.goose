// Input edges survive a whole tap between frames. Held state records the
// final event, edges record every transition, and queries do not consume them.
import std;
import os;
import ngfx;

fn inject(down: bool) {
    ngfx::inject_key("C", down);
    ngfx::inject_mouse(16.0, 16.0, ngfx::MOUSE_RIGHT, down);
}

fn report(label: const u8[:]) {
    print(label, ": key ", ngfx::key_down("C"), " ", ngfx::key_pressed("C"), " ",
          ngfx::key_released("C"), "; mouse ", ngfx::mouse_down(ngfx::MOUSE_RIGHT), " ",
          ngfx::mouse_pressed(ngfx::MOUSE_RIGHT), " ", ngfx::mouse_released(ngfx::MOUSE_RIGHT));
}

fn main() {
    guard ngfx::open_headless(32, 24) else {
        write_stderr(str("ngfx: ", ngfx::error(), "\n"));
        return;
    }
    ngfx::frame();
    report("initial");

    // Neither a frame's final held state nor a previous-state comparison can
    // represent this tap. Both edges must be visible, with the key/button up.
    inject(true);
    inject(false);
    report("queued, before frame");
    ngfx::frame();
    report("tap");
    report("queried again");
    let events = ngfx::events();
    for e in events {
        if e.kind == ngfx::EVENT_KEY { print("key event down ", e.down); }
        if e.kind == ngfx::EVENT_MOUSE_BUTTON { print("mouse event down ", e.down); }
    }
    ngfx::frame();
    report("tap cleared");

    // An ordinary hold has one press, then no edges until it is released.
    inject(true);
    ngfx::frame();
    report("press");
    ngfx::frame();
    report("hold");
    inject(true);
    inject(true);
    ngfx::frame();
    report("repeated down");
    inject(false);
    ngfx::frame();
    report("release");
    ngfx::frame();
    report("release cleared");
    inject(false);
    inject(false);
    ngfx::frame();
    report("repeated up");

    // The reverse order also has both edges, but ends held.
    inject(true);
    ngfx::frame();
    inject(false);
    inject(true);
    ngfx::frame();
    report("release then press");
    ngfx::frame();
    report("held after both edges");

    // Repeated events must not erase edges recorded earlier in the frame.
    inject(false);
    inject(false);
    inject(true);
    inject(true);
    inject(false);
    ngfx::frame();
    report("several transitions");
    ngfx::frame();
    report("several transitions cleared");
    print("other inputs untouched: ",
          !ngfx::key_down("A") && !ngfx::key_pressed("A") && !ngfx::key_released("A") &&
          !ngfx::mouse_down(ngfx::MOUSE_LEFT) && !ngfx::mouse_pressed(ngfx::MOUSE_LEFT) &&
          !ngfx::mouse_released(ngfx::MOUSE_LEFT));

    // Closing clears edge and held state too, before the next first frame.
    inject(true);
    ngfx::frame();
    ngfx::close();
    guard ngfx::open_headless(32, 24) else {
        write_stderr(str("ngfx: ", ngfx::error(), "\n"));
        return;
    }
    report("reopened");
    ngfx::frame();
    report("reopened first frame");
    ngfx::close();
}
