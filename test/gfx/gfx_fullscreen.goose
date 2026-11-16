// Fullscreen controls resolve and link, but cannot change a headless window.
import std;
import os;
import gfx;

fn main() {
    guard gfx::open_headless(32, 24) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    defer gfx::close();
    print("enable fullscreen ", gfx::set_fullscreen(true));
    print("disable fullscreen ", gfx::set_fullscreen(false));
    print("fullscreen ", gfx::fullscreen());
}
