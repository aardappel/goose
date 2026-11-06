// A pool's tasks are not contained, as a worker a contained one spawns is
// not (thread_contained_child.goose): an abort in one ends the program
// (§11.2), whichever of the pool's workers runs it. A pool of one worker
// runs its tasks on the thread applying it, here a contained worker's,
// which does not make them contained either. Expected to abort.
fn check_empty(chunk: i64[:]) { assert(chunk.len == 0); }

thread_fn applier() {
    var pool = thread_pool(1);
    defer pool.close();
    var data: i64[>..] = [1, 2];
    pool.apply(chunks(data, 1), check_empty);
}

fn main() {
    print("applying");
    thread_wait(thread_spawn_contained(applier));
    print("not reached");
}
