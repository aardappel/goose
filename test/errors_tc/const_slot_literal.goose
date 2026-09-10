// A string literal is a `const u8[:]`: it lands only in a slot declared
// const, so no read of the slot can write it (§9.5).
struct Box { s: u8[:] }
thread_fn worker() {
    var box = Box { s: "abc" };
    box.s[0] = 88;
}
fn main() {
    print("abc");
    thread_wait(thread_spawn(worker));
    print("abc");
}
