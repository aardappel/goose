// error: thread programs may access only flat globals
fn make() -> SharedOwner { let o, ok = shared_alloc(8); assert(ok); o }
let owner = make();
let view = shared_view(owner, 0, 8);
thread_fn worker() { print(shared_size(view)); }
fn main() { thread_spawn(worker); }
