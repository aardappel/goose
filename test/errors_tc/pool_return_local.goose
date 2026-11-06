// error: thread_pool can only be owned by a local or global variable; return a reference instead
// A function's own pool, returned by value, would leave with the caller while
// the function's defer closes it on the way out.
fn make() {
    var p = thread_pool(2);
    defer p.close();
    p
}

fn main() {
    var q = make();
    print(1);
}
