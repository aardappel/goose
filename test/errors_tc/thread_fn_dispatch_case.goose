// error: thread_fn handle is spawned with thread_spawn, not called
// A dispatched call calls every case (§8.2), a thread_fn among them.
enum E { A { x: i64 }, B { y: i64 } }
thread_fn handle(a: E.A) { print(a.x); }
fn handle(b: E.B) { print(b.y); }
fn main() {
    let e = E.A { x: 1 };
    e.handle();
}
