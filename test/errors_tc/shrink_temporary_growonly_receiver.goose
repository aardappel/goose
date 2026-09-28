// error: cannot call drop, which shrinks a: a[0], evaluated earlier in the statement
// error: may still refer into a
// A method call's receiver is its first argument (§7.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn plus(r: i64&, n: i64) -> i64 { return r + n; }
fn main() {
    var a: i64[>..] = [1, 2];
    print(a[0].plus(drop(a)));
}
