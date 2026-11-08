// error: thread_pool cannot be copied; pass it by reference (thread_pool&) instead
// An if's value is a copy of the branch it takes, which here is a pool
// variable.
fn main() {
    var a = thread_pool(2);
    defer a.close();
    var b = thread_pool(2);
    defer b.close();
    let first = true;
    var q = if first { a } else { b };
    print(1);
}
