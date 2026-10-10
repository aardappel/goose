// error: cannot call shrink, which shrinks t while v is still used
// A shrink of a tail field through a reference parameter is on the callee's
// record, which each call judges against the caller's views (§5.1).
struct S { n: i64, a: u8[>..] }
fn shrink(s: S&) { s.a.resize(0); }
fn main() {
    var t = S { n: 1, a: [1, 2, 3] };
    let v = t.a[..];
    shrink(t);
    print(v[0]);
}
