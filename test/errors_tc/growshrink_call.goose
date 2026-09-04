// A call that shrinks the array through a reference, while a slice of it is
// in scope (§5.2).
fn shrink(q: i64[>..<]&) { q.pop(); }
fn main() {
    var gs: i64[>..<] = [1, 2, 3];
    let s = gs[0..2];
    shrink(&gs);
    print(s[0]);
}
