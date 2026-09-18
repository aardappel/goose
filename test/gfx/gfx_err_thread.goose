// The gfx layer and SDL_GPU are main-thread only.
import gfx;

thread_fn render() {
    gfx::frame();
}

fn main() {
    thread_wait(thread_spawn(render));
}
// error: gfx runs on the main thread only
