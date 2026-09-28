// error: cannot call drop, which shrinks a while w is still used: it may hold a reference or slice into it
// The body of an if runs after its condition (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn main() {
    var a: i64[>..] = [1, 2];
    let w = a[..];
    if drop(a) == 0 { print(w); }
}
