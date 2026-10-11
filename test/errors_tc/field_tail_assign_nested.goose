// error: cannot call reset, which shrinks o while v is still used: it may hold a reference or slice into it (§5.1)
// A struct holding a grow-only tail, itself the tail of another, assigned
// whole through a reference to the outer one shrinks the outermost storage
// (§5.1).
struct S { n: i64, items: i64[>..] }
struct O { k: i64, s: S }
fn reset(o: O&) { o.s = S { n: 1, items: [4] }; }
fn main() {
    var o = O { k: 1, s: S { n: 1, items: [1, 2, 3] } };
    let v = o.s.items[..];
    reset(o);
    print(v[0]);
}
