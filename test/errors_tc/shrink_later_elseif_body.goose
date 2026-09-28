// error: cannot call drop, which shrinks a while w is still used: it may hold a reference or slice into it
// The same for an else-if's condition and body (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn main() {
    var a: i64[>..] = [1, 2];
    let w = a[..];
    let c = a.len > 5;
    if c { print(0); } else if drop(a) == 0 { print(w); }
    print(a.len);
}
