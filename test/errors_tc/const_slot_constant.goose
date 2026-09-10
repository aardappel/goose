// `&W` of a let global is a `const i64&`, and a slot that is not const
// cannot hold it: the constant stays unwritten by every thread (§9.5).
struct P { r: i64& }
const W = 78;
thread_fn worker() {
    var p = P { r: W };
    p.r = 5;
}
fn main() {
    thread_wait(thread_spawn(worker));
    print(W);
}
