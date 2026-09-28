// error: cannot call drop, which shrinks a: a[0], evaluated earlier in the statement
// error: may still refer into a
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn use(r: i64&, n: i64) -> i64 { return r + n; }
fn main() {
    var a: i64[>..] = [1, 2];
    print(use(a[0], loop { break drop(a); }));
}
