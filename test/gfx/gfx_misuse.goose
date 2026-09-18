// A draw the program should not have made: with no pipeline bound. The gfx
// layer reports it as it happens and skips it, and the next frame() aborts
// the program with the reason.
import std;
import vec;
import os;
import gfx;

fn main() {
    guard gfx::open_headless(16, 16) else {
        write_stderr(str("gfx: ", gfx::error(), "\n"));
        return;
    }
    print("drawing with no pipeline bound");
    gfx::begin_screen(float4 { 0.0, 0.0, 0.0, 1.0 });
    gfx::draw(3);
    gfx::end_pass();
    gfx::frame();
    print("not reached");
}
