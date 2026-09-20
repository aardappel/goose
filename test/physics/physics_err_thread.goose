// The physics layer keeps one state for the process: physics is main-thread
// only, as gfx is.
import physics;

thread_fn simulate() {
    let world = physics::create_world();
    physics::step(world, 1.0 / 60.0, 4);
}

fn main() {
    thread_wait(thread_spawn(simulate));
}
// error: physics runs on the main thread only
