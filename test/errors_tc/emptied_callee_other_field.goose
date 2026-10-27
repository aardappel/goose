// error: cannot call run, which shrinks pool while s is still used
// A callee emptying one field of its struct parameter before shrinking the
// pool another field refers to does not cover the rest (§5.1): run leaves h,
// which may hold a view into pool, so every store into s still counts, the
// construction that takes pool included.
struct H { r: (i64&)? }
struct S { pool: i64[>..]&, h: H, refs: (i64&)[>..] }
fn run(s: S&) {
    s.refs = [];
    s.pool = [];
}
fn main() {
    var pool: i64[>..] = [1];
    var s = S { pool: pool, h: H {}, refs: [] };
    s.h = H { r: pool[0] };
    run(s);
    print(s.refs.len);
}
