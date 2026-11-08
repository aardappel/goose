// error: thread_pool cannot be copied; pass it by reference (thread_pool&) instead
// A block's value is a copy of its last expression, which here is a pool
// variable.
fn main() {
    var p = thread_pool(2);
    defer p.close();
    var q = { p };
    print(1);
}
