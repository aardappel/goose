// error: cannot call reset, which shrinks s while v is still used: it may hold a reference or slice into it (§5.1)
// Assigning a struct's grow-only tail whole through a reference parameter
// shrinks the caller's array: each call judges it against the caller's views
// (§4.4, §5.1).
struct S { n: i64, items: i64[>..] }
fn reset(s: S&) { s.items = []; }
fn main() {
    var s = S { n: 3, items: [1, 2] };
    let v = s.items[..];
    reset(s);
    print(v[0]);
}
