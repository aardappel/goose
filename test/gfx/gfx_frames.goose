// Frames and input with the gfx module, headless: frame() runs without a
// window, input injected as if from the keyboard and mouse is seen at the
// next frame (down, pressed, released), and quit() ends the loop.
import std;
import vec;
import os;
import gfx;

fn open_or_skip() -> bool {
    if gfx::open_headless(32, 24) { return true; }
    write_stderr(str("gfx: ", gfx::error(), "\n"));
    return false;
}

fn keys(name: const u8[:]) -> u8[>..] {
    str(name, " down ", gfx::key_down(name), " pressed ", gfx::key_pressed(name), " released ",
        gfx::key_released(name))
}

fn main() {
    guard open_or_skip() else { return; }
    let size = gfx::screen_size();
    print("screen ", size.x, " x ", size.y, ", driver named ", gfx::driver().len > 0);
    print("first frame ", gfx::frame(), ", count ", gfx::frame_count());
    gfx::inject_key("Space", true);
    gfx::frame();
    print(keys("Space"), "; A down ", gfx::key_down("A"));
    gfx::frame();
    print(keys("Space"));
    gfx::inject_key("Space", false);
    gfx::frame();
    print(keys("Space"));
    gfx::inject_mouse(10.0, 20.0, gfx::MOUSE_LEFT, true);
    gfx::frame();
    let p = gfx::mouse_pos();
    print("mouse at ", p.x, ",", p.y, " left down ", gfx::mouse_down(gfx::MOUSE_LEFT), " pressed ",
          gfx::mouse_pressed(gfx::MOUSE_LEFT), " right down ", gfx::mouse_down(gfx::MOUSE_RIGHT));
    gfx::inject_mouse(15.0, 18.0, gfx::MOUSE_LEFT, false);
    gfx::frame();
    let d = gfx::mouse_delta();
    print("moved by ", d.x, ",", d.y, " left released ", gfx::mouse_released(gfx::MOUSE_LEFT));
    var frames = 0;
    while gfx::frame() {
        frames++;
        if frames == 3 { gfx::quit(); }
    }
    print("quit after ", frames, " more frames, count ", gfx::frame_count(), ", time runs ",
          gfx::time() > 0.0, ", last frame took ", gfx::delta_time() >= 0.0);
    gfx::close();
}
