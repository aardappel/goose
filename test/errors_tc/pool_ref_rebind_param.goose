// error: re-binding p to a reference that is not a pool reference, where it was bound to a reusable[] pool
// A `var` parameter given a pool is a pool reference in that specialization
// (§5.4), where it arrives with the pool's freelist; given a plain array, the
// same body is fine.
var plain: i64[>..] = [1];
reusable[] var pool: i64[>..] = [];
fn f(var p: i64[>..]&) {
    p .= plain;
    print(p.len);
}
fn main() {
    f(plain);
    f(pool);
}
