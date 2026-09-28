// error: cannot call drop, which shrinks a: a[1], which the assignment writes after it
// error: may be in a
// The location is evaluated before the right-hand side, which frees it
// (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn main() {
    var a: i64[>..] = [1, 2];
    a[1] = drop(a);
    print(a.len);
}
