// error: cannot assign u (which q.items may point at) while v is still used
// Through a reference whose storage is one of two structs, the assignment
// shrinks each of them (§5.1).
struct S { n: i64, items: i64[>..] }
fn main() {
    var t = S { n: 1, items: [1, 2, 3] };
    var u = S { n: 1, items: [1, 2, 3] };
    let v = u.items[..];
    let q: S& = if t.n > 0 { t } else { u };
    q.items = [];
    print(v[0]);
}
