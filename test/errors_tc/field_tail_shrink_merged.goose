// error: cannot pop u (which q.a may point at) while v is still used
// Through a reference that may name either of two structs, a tail field's
// shrink is one of each (§5.1).
struct S { n: i64, a: u8[>..] }
fn main() {
    var t = S { n: 1, a: [1, 2, 3] };
    var u = S { n: 1, a: [1, 2, 3] };
    let v = u.a[..];
    let q: S& = if t.n > 0 { t } else { u };
    q.a.pop();
    print(v[0]);
}
