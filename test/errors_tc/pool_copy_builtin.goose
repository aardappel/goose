// error: thread_pool cannot be copied; pass it by reference (thread_pool&) instead
// copy() would make a second handle to the pool, which close() through either
// variable would leave dangling in the other.
fn main() {
    var p = thread_pool(2);
    defer p.close();
    var q = copy(p);
    print(1);
}
