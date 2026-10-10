// error: cannot pop o while v is still used
// A tail field of a struct that is itself a tail field shrinks the
// outermost storage (§5.1).
struct S { n: i64, a: u8[>..] }
struct O { k: i64, s: S }
fn main() {
    var o = O { k: 1, s: S { n: 1, a: [1, 2, 3] } };
    let v = o.s.a[..];
    o.s.a.pop();
    print(v[0]);
}
