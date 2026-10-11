// error: cannot call reset, which shrinks g while v is still used: it may hold a reference or slice into it (§5.1)
// Assigning a grow-only array whole through a reference to it shrinks the
// caller's array (§4.4, §5.1).
fn reset(a: i64[>..]&) { a = [1]; }
fn main() {
    var g: i64[>..] = [1, 2];
    let v = g[..];
    reset(g);
    print(v[0]);
}
