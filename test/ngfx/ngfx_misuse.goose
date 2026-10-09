// A draw the program should not have made: with no pipeline bound. The ngfx
// layer reports it as it happens and skips it, and the next frame() aborts
// the program with the reason.
import std;
import vec;
import os;
import ngfx;

fn main() {
    guard ngfx::open_headless(16, 16) else {
        write_stderr(str("ngfx: ", ngfx::error(), "\n"));
        return;
    }
    print("drawing with no pipeline bound");
    ngfx::begin_screen(float4 { 0.0, 0.0, 0.0, 1.0 });
    ngfx::draw(0, 3);
    ngfx::end_pass();
    ngfx::frame();
    print("not reached");
}
