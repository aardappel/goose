// A worker a contained one spawns with thread_spawn is not contained: its
// abort ends the program (§11.2). Expected to abort.
thread_fn child() { assert(false); }
thread_fn parent() { thread_wait(thread_spawn(child)); }
fn main() {
    print("spawning");
    thread_wait(thread_spawn_contained(parent));
    print("not reached");
}
