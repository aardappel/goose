// error: cannot assign t while v is still used
// Through a reference read back out of a holder, the assignment shrinks the
// storage the reference may name (§5.1, §9.5).
struct S { n: i64, items: i64[>..] }
struct W { q: S& }
fn main() {
    var t = S { n: 1, items: [1, 2, 3] };
    let v = t.items[..];
    var w = W { q: t };
    w.q.items = [];
    print(v[0]);
}
