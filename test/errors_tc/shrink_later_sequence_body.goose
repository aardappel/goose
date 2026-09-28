// error: cannot call drop, which shrinks a while w is still used: it may hold a reference or slice into it
// A for's body runs after its sequence, here a range's lower bound (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn main() {
    var a: i64[>..] = [1, 2];
    let w = a[..];
    for i in drop(a)..2 { print(w[i]); }
}
