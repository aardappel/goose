// error: thread_pool cannot be copied; bind it by reference (.=) instead
// A multi-value binding takes a reference result's pointee, as a single one
// does: a copy of the pool, unless it binds by reference (.=, as
// pool_chosen.goose does).
fn both(p: thread_pool&) -> thread_pool&, i64 {
    return p, 1;
}

fn main() {
    var p = thread_pool(2);
    defer p.close();
    var q, n = both(p);
    print(n);
}
