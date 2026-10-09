// The ngfx layer and SDL_GPU are main-thread only.
import ngfx;

thread_fn render() {
    ngfx::frame();
}

fn main() {
    thread_wait(thread_spawn(render));
}
// error: ngfx runs on the main thread only
