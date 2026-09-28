// error: cannot call drop, which shrinks a: a[..], evaluated earlier in the statement
// error: may still refer into a
// The same, where the literal is an argument rather than bound (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
struct T { s: const i64[:], n: i64 }
fn show(t: T) { print(t.s, " ", t.n); }
fn main() {
    var a: i64[>..] = [1, 2];
    show(T { s: a[..], n: drop(a) });
}
