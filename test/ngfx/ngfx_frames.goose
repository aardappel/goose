// Frames and input with the ngfx module, headless: frame() runs without a
// window, input injected as if from the keyboard and mouse is seen at the
// next frame (down, pressed, released), and quit() ends the loop.
import std;
import vec;
import os;
import ngfx;

fn open_or_skip() -> bool {
    if ngfx::open_headless(32, 24) { return true; }
    write_stderr(str("ngfx: ", ngfx::error(), "\n"));
    return false;
}

fn keys(name: const u8[:]) -> u8[>..] {
    str(name, " down ", ngfx::key_down(name), " pressed ", ngfx::key_pressed(name), " released ",
        ngfx::key_released(name))
}

fn main() {
    guard open_or_skip() else { return; }
    let size = ngfx::screen_size();
    print("screen ", size.x, " x ", size.y, ", driver named ", ngfx::driver().len > 0);
    print("first frame ", ngfx::frame(), ", count ", ngfx::frame_count());
    ngfx::inject_key("Space", true);
    ngfx::frame();
    print(keys("Space"), "; A down ", ngfx::key_down("A"));
    ngfx::frame();
    print(keys("Space"));
    ngfx::inject_key("Space", false);
    ngfx::frame();
    print(keys("Space"));
    ngfx::inject_mouse(10.0, 20.0, ngfx::MOUSE_LEFT, true);
    ngfx::frame();
    let p = ngfx::mouse_pos();
    print("mouse at ", p.x, ",", p.y, " left down ", ngfx::mouse_down(ngfx::MOUSE_LEFT), " pressed ",
          ngfx::mouse_pressed(ngfx::MOUSE_LEFT), " right down ", ngfx::mouse_down(ngfx::MOUSE_RIGHT));
    ngfx::inject_mouse(15.0, 18.0, ngfx::MOUSE_LEFT, false);
    ngfx::frame();
    let d = ngfx::mouse_delta();
    print("moved by ", d.x, ",", d.y, " left released ", ngfx::mouse_released(ngfx::MOUSE_LEFT));
    var frames = 0;
    while ngfx::frame() {
        frames++;
        if frames == 3 { ngfx::quit(); }
    }
    print("quit after ", frames, " more frames, count ", ngfx::frame_count(), ", time runs ",
          ngfx::time() > 0.0, ", last frame took ", ngfx::delta_time() >= 0.0);
    ngfx::close();
}
