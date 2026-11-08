// error: thread_pool can only be owned by a local or global variable; return a reference instead
// An inferred result owns a pool no more than a declared one does: this one
// would be a copy of the pool its parameter points at (pool_chosen.goose
// returns the reference instead).
fn identity(p: thread_pool&) {
    p
}

fn main() {
    var p = thread_pool(2);
    defer p.close();
    var q = identity(p);
    print(1);
}
