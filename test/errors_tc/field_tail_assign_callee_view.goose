// error: cannot assign s while v is still used: it may hold a reference or slice into it (§5.1)
// A slice of the tail taken through the reference and used after the
// assignment views the elements it frees (§5.1).
struct S { n: i64, items: i64[>..] }
fn reset(s: S&) {
    let v = s.items[..];
    s.items = [];
    print(v[0]);
}
fn main() {
    var s = S { n: 3, items: [1, 2] };
    reset(s);
}
