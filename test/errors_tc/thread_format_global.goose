// error: thread programs may access only flat globals
// error: shared holds references (reached from thread_fn worker)
struct Show { x: i64 }
var value = 42;
let shared = &value;
fn format(out: u8[>..]&, item: Show) {
    shared = 99;
    out.append("changed");
}
thread_fn worker() { print(Show { x: 1 }); }
fn main() { thread_wait(thread_spawn(worker)); }
