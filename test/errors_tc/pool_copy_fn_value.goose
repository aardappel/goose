// error: thread_pool cannot be copied; pass it by reference (thread_pool&) instead
// A function value's result is a copy of its block's value, here a pool
// variable the block names; a block that makes a pool is fine
// (pool_chosen.goose).
fn use<F>() {
    var q = F();
    q.close();
}

fn main() {
    var p = thread_pool(2);
    use() { p };
    p.close();
}
