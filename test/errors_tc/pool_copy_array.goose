// error: thread_pool can only be owned by a local or global variable; nested thread_pool owners are not supported
// An array literal infers its element type, which can no more be a
// thread_pool than one written out can.
fn main() {
    var pools = [thread_pool(2), thread_pool(2)];
    print(pools.len);
}
