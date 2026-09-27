// The ui layer, like the gfx it draws through, is main-thread only.
import ui;

thread_fn build() {
    ui::clear(ui::Context { 1 });
}

fn main() {
    thread_wait(thread_spawn(build));
}
// error: ui runs on the main thread only
