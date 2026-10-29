// The array an index type names is part of the worker's global snapshot.
var nodes: i32[>..] = [];
type Id = u32 in nodes;
thread_fn worker() { print(0 as Id); }
fn main() {
    nodes.push(1);
    thread_wait(thread_spawn(worker));
}
