// error: h was consumed
// error: and holds nothing here
// Two arguments of one call each run a block that releases h: the second
// finds it consumed by the first (docs/design/ownership.md 4.4).
import slot_pool;
struct Node { v: i64 }
var pool = slot_pool<Node> {};
fn add(a: i64, b: i64) -> i64 { a + b }
fn once<F>() -> i64 { F(); return 1; }
fn main() {
    let h = pool.take(Node { v: 1 });
    print(add(once() { pool.release(h); }, once() { pool.release(h); }));
}
