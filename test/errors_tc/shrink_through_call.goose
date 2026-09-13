// error: cannot call drop_last, which shrinks xs while view is still used: it may hold a reference or slice into it
// A callee shrinks the array through a reference while the caller holds a
// slice into it (§5.1): reported at the call.
fn drop_last(n: i64[>..]&) { n.pop(); }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    let view = xs[..2];
    drop_last(xs);
    print(view);
}
