// error: cannot call drop, which shrinks a: a[0], evaluated earlier in the statement
// error: may still refer into a
// A shrink inside an if's value is inside the enclosing call's arguments
// (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn use(r: i64&, n: i64) -> i64 { return r + n; }
fn main() {
    var a: i64[>..] = [1, 2];
    let c = a.len > 1;
    print(use(a[0], if c { drop(a) } else { 0 }));
}
