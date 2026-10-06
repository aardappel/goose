import audio;

thread_fn sound() {
    audio::open();
}

fn main() {
    thread_wait(thread_spawn(sound));
}
// error: audio runs on the main thread only
