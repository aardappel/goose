// error: thread_pool cannot be copied; pass it by reference (thread_pool&) instead
// An un-annotated variable bound to a reference to a pool takes the pool it
// points at, not the reference: a copy, as of the variable itself.
fn main() {
    var p = thread_pool(2);
    defer p.close();
    let r = &p;
    var q = r;
    print(1);
}
