// error: thread_pool cannot be copied; pass it by reference (thread_pool&) instead
fn main() {
    var p1 = thread_pool(2);
    var p2 = p1;
    print(1);
}
