// error: thread_fn format is spawned with thread_spawn, not called
// error: thread_fn parameters must be flat (§11.2), not u8[>..]&
// print calls the format overload of the type it renders (§3.7), a thread_fn
// as well.
struct P { x: i64 }
thread_fn format(out: u8[>..]&, p: P) { out.append("P"); }
fn main() {
    print(P { x: 1 });
}
