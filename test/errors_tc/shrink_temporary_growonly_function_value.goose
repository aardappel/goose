// error: cannot call run, which shrinks a: a[0], evaluated earlier in the statement
// error: may still refer into a
// The block run calls clears the caller's array, whose element the enclosing
// call still holds: the shrink is run's, at its call (§5.1).
fn drop(a: i64[>..]&) -> i64 { a.clear(); return 0; }
fn use(r: i64&, n: i64) -> i64 { return r + n; }
fn run<F>() -> i64 { return F(); }
fn main() {
    var a: i64[>..] = [1, 2];
    print(use(a[0], run() { drop(a) }));
}
